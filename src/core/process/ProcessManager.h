// =============================================================================
// Judecoin Wallet - ProcessManager (st-006, st-113)
//
// Owns the lifecycle of two child processes:
//
//   - judecoind          (V1.0 daemon) - launched with QProcess::startDetached
//                         so the UI can exit without killing the daemon. The
//                         UI never holds a QProcess* to it (we keep a pid).
//   - judecoin-wallet-rpc (V1.0 wallet-rpc) - launched as a QProcess child
//                         of the UI. The UI kills it on shutdown.
//
// Plus a 5s heartbeat timer that pings each process via QProcess stateChanged
// + (when applicable) an HTTP RPC probe. This is the "double insurance"
// mentioned in devplan/st-006.
//
// Restart policy (V5.4.4 #2 D)
// ----------------------------
// On a process exit / state-down event, ProcessManager:
//
//   1. Captures QProcess state + (if available) the last stderr lines.
//   2. Asks ErrorClassifier for a Category.
//   3. Asks RestartStrategy for the next backoff.
//   4. Schedules QTimer::singleShot(backoff, restart).
//   5. Emits lockOperations() before the restart so the UI can disable
//      wallet-side action buttons; emits unlockOperations() after the
//      restart succeeds or after restartExhausted() so the UI can
//      either re-enable actions or surface a fatal dialog.
//
// st-113 addition: local-daemon detection
// ----------------------------------------
// If `setDaemonProbeEndpoint()` is configured, startDaemon() runs a 2-step
// detection step *before* spawning:
//
//   1. PortConflictDetector::isPortBound() - TCP probe, 1 s timeout.
//      Returns immediately when nothing is listening.
//   2. HttpRpcClient::get_info() - RPC probe, ~5 s timeout. Only fires
//      if step 1 saw a listener.
//
// On success startDaemon() adopts the discovered PID (no spawn), sets
// State::Running, and emits externalDaemonDetected(host, port, version).
// On RPC success with version < v3.2.0 we instead emit versionMismatch
// and stay in NotStarted so the UI can prompt the user.
//
// V1.0 scope
// ----------
// This module only owns process lifecycle. RPC connectivity is the
// HttpRpcClient / DaemonClient / WalletRpcClient responsibility. The
// ProcessManager does not parse JSON apart from the minimal version
// probe payload (covered in .cpp).
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_PROCESS_PROCESS_MANAGER_H
#define JUDECOIN_WALLET_CORE_PROCESS_PROCESS_MANAGER_H

#include "core/process/ErrorClassifier.h"

#include <QObject>
#include <QProcess>
#include <QScopedPointer>
#include <QString>
#include <QTimer>
#include <QStringList>

#include <chrono>
#include <memory>

namespace judecoin_wallet::core::process {

class RestartStrategy;

// -----------------------------------------------------------------------------
// ProcessSpec - declarative description of a process to manage.
//
// For the daemon we set detached = true and the manager calls
// QProcess::startDetached(); we do NOT keep a QProcess* for detached
// processes (Qt does not expose one after startDetached), only the PID.
//
// For wallet-rpc we set detached = false and the manager keeps a real
// QProcess* member, so it can read stderr / kill on shutdown.
// -----------------------------------------------------------------------------
struct ProcessSpec {
    enum class Kind {
        Daemon,       // QProcess::startDetached; survives UI exit
        WalletRpc,    // QProcess child of UI; killed on UI exit
    };

    Kind        kind = Kind::WalletRpc;
    QString     name;                 // human label, e.g. "judecoind"
    QString     program;              // absolute path to binary
    QStringList arguments;            // CLI arguments
    QString     workingDirectory;     // empty = inherit
    QStringList environment;          // extra env vars in KEY=VALUE form
    qint64      expectedPid = 0;      // for ad-hoc externally-launched
                                      // processes (e.g. an already-running
                                      // daemon detected at startup); 0 if
                                      // we are launching from scratch.
};

class ProcessManager : public QObject
{
    Q_OBJECT

public:
    // Spec constants used in devplan/st-006. Production values;
    // tests override via setters or by retiming QTimers directly.
    static constexpr std::chrono::milliseconds kHeartbeatInterval{5000};  // 5 s heartbeat
    static constexpr std::chrono::milliseconds kLockGraceWindow{2000};   // 2 s after restart
    static constexpr int                      kMaxStderrLines = 32;      // ring buffer size

    explicit ProcessManager(QObject* parent = nullptr);
    ~ProcessManager() override;

    // -- Configuration ---------------------------------------------------------

    void setDaemonSpec(const ProcessSpec& spec);
    ProcessSpec daemonSpec() const { return daemonSpec_; }

    void setWalletRpcSpec(const ProcessSpec& spec);
    ProcessSpec walletRpcSpec() const { return walletRpcSpec_; }

    // st-135 (Fix D, A-1): parse --rpc-bind-port from spec.arguments so the
    // spawn-time TCP ping (ProcessManager.cpp::startDaemon ~line 200) can
    // hit the daemon's actual bind port instead of the stale
    // daemonProbePort_ (which StartupManager pins to 19061 at the head of
    // stepStartDaemon for the pre-spawn adopt probe). See
    // audit/2026-07-17/2026-07-17-fix-d-bug-a-and-c8-pid-clear.md §1 for
    // the failure mode. Returns 0 if the flag is absent or unparseable
    // (caller treats as "ping 0, fail closed"; in V1.0 the flag is always
    // injected by StartupManager, so the 0 branch is defensive only).
    static quint16 extractRpcBindPort(const ProcessSpec& spec);

    // Set the wallet-rpc HTTP endpoint. The heartbeat probe issues a
    // get_version call to detect when the process is up; before the
    // endpoint is set, the probe is skipped.
    void setWalletRpcProbeEndpoint(const QString& url);
    QString walletRpcProbeEndpoint() const { return walletRpcProbeEndpoint_; }

    // -- st-113 daemon probe endpoint -----------------------------------------
    //
    // When configured, startDaemon() will probe (host, port) for an
    // already-running daemon before spawning its own. Set host to an
    // empty string to disable the probe (the original behaviour).
    //
    // The probe is two-step:
    //   (a) PortConflictDetector::isPortBound(host, port)  - TCP probe
    //   (b) HttpRpcClient::get_info() RPC probe
    //
    // On (a)+(b) success the manager emits externalDaemonDetected and
    // sets daemonPid_ to the discovered PID (from
    // PortConflictDetector::findPidByPort()).
    void setDaemonProbeEndpoint(const QString& host, quint16 port);
    QString daemonProbeHost() const { return daemonProbeHost_; }
    quint16 daemonProbePort() const { return daemonProbePort_; }
    bool    daemonProbeEnabled() const
    {
        return !daemonProbeHost_.isEmpty() && daemonProbePort_ != 0;
    }

    // -- st-113 helpers ------------------------------------------------------
    //
    // Synchronously probe the configured daemon endpoint. Returns one of:
    //   ProbeAdopted     - external daemon found + version OK. State is
    //                     Running and externalDaemonDetected was emitted.
    //   ProbeVersionMismatch - external daemon found but version is too
    //                     old. State is NotStarted and versionMismatch
    //                     was emitted; caller must NOT spawn.
    //   ProbeNoExisting  - nothing listening (or listener did not answer
    //                     get_info). State untouched; caller may spawn.
    //
    // Called by StartupManager::stepStartDaemon() (st-133g) to
    // skip spawn when an existing daemon already owns the hardcoded ports.
    enum class ProbeResult {
        ProbeAdopted,
        ProbeVersionMismatch,
        ProbeNoExisting,
    };
    ProbeResult probeExistingDaemon();

    // st-136 (Bug B + C unified): doStartOnce() returns StartResult so
    // startDaemon() can wrap it in an attempt loop. Permanent != retry;
    // Transient = backoff + retry; NotStarted = nothing to do (caller
    // passed an idle/invalid state and there is nothing to start).
    enum class StartResult {
        Success,
        Permanent,
        Transient,
        NotStarted,
    };

    // Override the heartbeat interval (default 5s). Used by tests to
    // keep the suite fast.
    void setHeartbeatInterval(std::chrono::milliseconds t);
    std::chrono::milliseconds heartbeatInterval() const { return heartbeatInterval_; }

    // -- Lifecycle -------------------------------------------------------------

    // Start the daemon. If detached, calls QProcess::startDetached()
    // and remembers the PID only. Returns true on successful spawn.
    bool startDaemon();

    // Start wallet-rpc as a child process. Returns true on successful
    // spawn.
    bool startWalletRpc();

    // Stop wallet-rpc gracefully (SIGTERM then SIGKILL after gracefulDelay).
    // Detached daemons are NOT killed - they survive the UI.
    //
    // FIX-WR1 (2026-07-19): after st-143i (bash -c "... &" spawn)
    // and st-143L (walletRpcProcess_.reset() once the port probe
    // passes), the QProcess wrapper only ever tracks the short-lived
    // bash forker - the REAL judecoin-wallet-rpc is an orphan. The old
    // implementation early-returned when walletRpcProcess_ was null,
    // so app exit left wallet-rpc alive holding the wallet2 keys-file
    // lock -> the next launch failed open_wallet with "wallet already
    // open / opened by another wallet program" (manual test
    // 2026-07-19 "close wallet and reopen"). stopWalletRpc now also kills the
    // real process by PID (walletRpcPid_, captured at spawn time via
    // findPidByPort; falls back to resolving the probe-endpoint port
    // at stop time). Every PID kill is guarded by
    // pidLooksLikeWalletRpc() so a recycled PID is never signalled.
    void stopWalletRpc(std::chrono::milliseconds gracefulDelay = std::chrono::milliseconds(3000));
    void stopAll();

    // FIX-WR3 (2026-07-19): kill stale judecoin-wallet-rpc
    // processes left over by a previous UI session (clean exit before
    // FIX-WR1, or a crash where aboutToQuit never ran) that still
    // listen on our dedicated wallet-rpc port range
    // [rangeStart, rangeEnd]. Only PIDs whose /proc/<pid>/cmdline
    // looks like the wallet-rpc binary are touched: SIGTERM first
    // (wallet-rpc stores the wallet + releases the wallet2 keys-file
    // lock), SIGKILL for survivors after a ~3s grace window. Returns
    // the number of reaped processes. Called by
    // StartupManager::stepStartWalletRpc() BEFORE port selection so
    // (a) the keys-file lock is released and open_wallet stops
    // failing with "wallet already open", and (b) the canonical port
    // 19071 becomes reusable instead of drifting up the range on
    // every relaunch.
    int reapStaleWalletRpc(const QString& host,
                           quint16 rangeStart,
                           quint16 rangeEnd);

    // st-133b (2026-07-17 01:42): kill the detached daemon
    // explicitly. The default stopAll() leaves the daemon alive on
    // purpose (so st-133g adoption logic on the NEXT launch can pick
    // up an already-running daemon), but when the user EXPLICITLY
    // closes the UI we want every spawned child to die so:
    //   - pgrep -af judecoind returns 0 lines after SIGTERM
    //     (the real-test st-133b verify)
    //   - /tmp/.mount_Judeco* FUSE mounts can be unmounted cleanly
    //     (the squashfs-root has no open file handles)
    //   - ~/.judecoin/judecoind.pid does not linger and mislead the
    //     next launch into "adopting" a dead PID
    //
    // Sequence (matches stopWalletRpc's pattern):
    //   1. If daemonPid_ <= 0 OR !isDaemonProcessAlive(), return.
    //   2. ::kill(daemonPid_, SIGTERM).
    //   3. Poll isDaemonProcessAlive() every 100 ms, up to gracefulDelay.
    //   4. If still alive after gracefulDelay, ::kill(daemonPid_, SIGKILL).
    //   5. setDaemonState(State::NotStarted); daemonPid_ = 0.
    //
    // safe to call when no daemon was ever started (no-op). safe to
    // call twice (second call is a no-op after daemonPid_ is cleared).
    void killDetachedDaemon(std::chrono::milliseconds gracefulDelay = std::chrono::milliseconds(5000));

    // -- Status ----------------------------------------------------------------

    enum class State {
        NotStarted,    // never started (or explicitly reset)
        Starting,      // spawn requested, awaiting state change
        Running,       // up and accepting connections
        Stopping,      // SIGTERM sent, awaiting exit
        Crashed,       // died before restart could happen, or restart
                       // budget exhausted
        Restarting,    // backoff timer is counting down
    };
    Q_ENUM(State)

    State daemonState() const    { return daemonState_; }
    State walletRpcState() const { return walletRpcState_; }

    // True if the daemon PID is alive in the OS (kill(pid, 0) == 0).
    bool isDaemonProcessAlive() const;

    // Number of stderr lines retained from wallet-rpc (the only process
    // we have a QProcess* for). Useful for crash reports.
    QStringList walletRpcStderrTail() const;

    // True iff operations (transfers, etc.) are currently locked.
    // Locked means: a process crashed and is restarting, OR the wallet-rpc
    // is currently down. Cleared once the process is back up and the
    // grace window has elapsed.
    bool operationsLocked() const { return operationsLocked_; }

    // Lock / unlock the operations signal pair. lockOps() is called on
    // every crash event (concurrent crashes increment the counter);
    // unlockOps() balances the count. When the count returns to 0 the
    // unlockOperations() signal is emitted.
    //
    // Public so tests, CrashRecoveryManager (st-013) and any future
    // external coordinator can drive the same signal pair.
    // Test/diagnostic accessor: direct pointer to the underlying QProcess
    // wrapper for wallet-rpc. Returns nullptr when startWalletRpc() has
    // not yet been called (or after the process has been torn down).
    // Production code should use walletRpcStderrTail() / walletRpcState()
    // instead. Not part of the documented stable API.
    QProcess* walletRpcQProcess() const { return walletRpcProcess_.data(); }

    void lockOps(const QString& reason);
    void unlockOps();

public slots:
    // Force a heartbeat tick right now (mostly used by tests).
    void heartbeatTick();

signals:
    // Emitted whenever State changes. The bool is "isHealthy" - true for
    // Running, false for everything else (used by UI status bar).
    void daemonStateChanged(judecoin_wallet::core::process::ProcessManager::State newState,
                            bool isHealthy);
    void walletRpcStateChanged(judecoin_wallet::core::process::ProcessManager::State newState,
                               bool isHealthy);

    // Emitted immediately before a process is restarted. UI should
    // disable action buttons (Send, Receive, etc.) to prevent the user
    // from initiating transfers against a wallet-rpc that may not yet
    // be reachable.
    void lockOperations(const QString& reason);

    // Emitted once the process is back up AND the grace window has
    // elapsed. UI re-enables action buttons.
    void unlockOperations();

    // Emitted when a restart attempt has been scheduled. argument is
    // ms-to-wait before the next start().
    void restartScheduled(int delayMs,
                          judecoin_wallet::core::process::ErrorClassifier::Category category);

    // Emitted when the restart budget is exhausted. UI must surface a
    // fatal dialog (RestartStrategy will not be called again until the
    // user manually resets via reset()).
    void restartExhausted(judecoin_wallet::core::process::ErrorClassifier::Category category,
                          const QString& lastReason);

    // Emitted on stderr output from wallet-rpc (last line). Used by
    // the log panel. Empty if nothing new since the last emit.
    void walletRpcStderrLine(const QString& line);

    // -- st-113 local-daemon detection ----------------------------------------

    // Emitted when startDaemon() discovered an already-running daemon at
    // the configured probe endpoint and adopted it (no spawn). UI can
    // show a "Using existing daemon <version> at <host>:<port>" banner.
    void externalDaemonDetected(const QString& host,
                                quint16 port,
                                const QString& versionString);

    // Emitted when the probe found an existing daemon but its
    // version is older than the lowest supported version (v3.2.0).
    // The manager stays NotStarted so the UI can prompt the user.
    void versionMismatch(const QString& versionString);

    // st-136 (Bug C): emitted by the heartbeat RPC-fallback when it
    // detects the adopted daemon is no longer this judecoind (port
    // unbound, RPC unreachable, or version drift). ProcessManager's
    // ctor wires this to a lambda that clears the adopt probe
    // endpoint and re-invokes startDaemon() so the spawn-retry loop
    // (Bug B) takes over. Reason is a short diagnostic string.
    void adoptionRevoked(const QString& reason);

    // st-136 (Bug B): startDaemon() now wraps doStartOnce() in a
    // kMaxAttempts=3 retry loop. Emitted before each attempt with
    // 1..kMaxAttempts so the UI can display "attempt 2 of 3". On
    // exhaustion emits attemptChanged(0, kMaxAttempts) so the UI
    // can settle into the failure path. attempt==0 = exhausted.
    void attemptChanged(int attempt, int max);

private slots:
    // Internal: heartbeat timer fires this. Walks both child processes
    // and updates their state.
    void onHeartbeat();

    // Internal: wallet-rpc QProcess signals.
    void onWalletRpcError(QProcess::ProcessError err);
    void onWalletRpcStateChanged(QProcess::ProcessState newState);
    void onWalletRpcFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onWalletRpcReadyReadStderr();

    // st-143d: daemon QProcess signals (same pattern as wallet-rpc).
    void onDaemonError(QProcess::ProcessError err);
    void onDaemonFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDaemonReadyReadStderr();

private:
    // Transition helpers (also fire signals).
    void setDaemonState(State s);
    void setWalletRpcState(State s);

    // Schedule a restart attempt using the configured strategy.
    // attemptsDone = number of attempts that have already happened
    // (>= 1 since this is called after a failure).
    void scheduleRestart(const QString& reason,
                         ErrorClassifier::Category category,
                         int attemptsDone);

    // Actually start the restart attempt (called by the QTimer::singleShot).
    void doRestart();

    // Heartbeat probe - currently a no-op stub that just observes
    // QProcess state; later versions can issue a tiny HTTP request to
    // wallet-rpc to confirm it is responding.
    void probeWalletRpc();

    // Read the daemon /proc/<pid>/stat to check liveness.
    bool pingPid(qint64 pid) const;

    // FIX-WR1 (2026-07-19): true iff /proc/<pid>/cmdline looks like our
    // wallet-rpc binary (contains "judecoin-wallet-rpc" or the basename
    // of walletRpcSpec_.program). Guards every PID-based kill in
    // stopWalletRpc() / reapStaleWalletRpc() so a recycled PID
    // belonging to an unrelated process is never signalled. Returns
    // false when /proc is unreadable (conservative: no kill).
    bool pidLooksLikeWalletRpc(qint64 pid) const;

    // st-143d (2026-07-17 14:55): replaced startDetachedAndCapturePid
    // with QProcess::start() so we capture stderr and avoid the
    // startDetached+setsid+FUSE exec failure.

    // st-136 (Bug B): the actual one-shot daemon-start body extracted
    // from startDaemon() so the public startDaemon() can wrap it in
    // a kMaxAttempts=3 retry loop. Returns StartResult categorising
    // the failure (Permanent vs Transient) so the loop can decide
    // whether to backoff+retry or short-circuit. Public-callers stick
    // to startDaemon() (returns bool, same external semantics).
    StartResult doStartOnce();

    // FIX-G2 (2026-07-17 17:33): one-shot wallet-rpc spawn body,
    // mirroring doStartOnce() above. Public startWalletRpc() wraps it
    // in the same 3-attempt retry loop; doStartWalletRpcOnce() does
    // one spawn attempt and returns StartResult so the wrapper can
    // categorise Permanent vs Transient failures (the inner method
    // must NOT call itself or any retry helper directly).
    StartResult doStartWalletRpcOnce();

    ProcessSpec daemonSpec_;
    ProcessSpec walletRpcSpec_;

    State daemonState_    = State::NotStarted;
    State walletRpcState_ = State::NotStarted;

    qint64 daemonPid_           = 0;  // 0 if not started / not detached
    // FIX-WR1 (2026-07-19): PID of the REAL judecoin-wallet-rpc process
    // (NOT the bash forker tracked by walletRpcProcess_ - see st-143i).
    // Captured in doStartWalletRpcOnce() via
    // PortConflictDetector::findPidByPort() right after the RPC port
    // probe succeeds; consumed by stopWalletRpc() so app exit actually
    // terminates wallet-rpc and releases the wallet2 keys-file lock.
    // 0 = unknown / not running.
    qint64 walletRpcPid_        = 0;
    // walletRpcProcess_ is owned by this ProcessManager via QScopedPointer
    // with the QScopedPointerDeleteLater deleter. We deliberately do NOT
    // parent() it to `this`: that would create dual ownership with the
    // Qt parent-child teardown path, which would risk a double-delete
    // race on ProcessManager destruction. The deleter ensures the wrapper
    // is removed via QObject::deleteLater() (next event-loop tick),
    // giving in-flight signals a chance to land before the wrapper goes
    // away. See V5.4.5 gf-008 ownership-tightening note.
    QScopedPointer<QProcess, QScopedPointerDeleteLater> walletRpcProcess_;
    // st-143d: daemon now uses QProcess (not startDetached) like wallet-rpc.
    QScopedPointer<QProcess, QScopedPointerDeleteLater> daemonProcess_;

    QStringList walletRpcStderrBuffer_;
    QStringList daemonStderrBuffer_;
    QString walletRpcProbeEndpoint_;          // URL for HTTP probe (optional)

    // st-113: local-daemon probe endpoint. Empty host / 0 port means
    // "probing disabled, fall through to spawn unconditionally".
    QString daemonProbeHost_;
    quint16 daemonProbePort_ = 0;
    // st-135 (Fix D, A-1): the actual --rpc-bind-port pulled from
    // daemonSpec_.arguments at the head of startDaemon(). The spawn-time
    // ping uses THIS port, not daemonProbePort_ (which is for the adopt
    // probe pre-spawn). See ProcessManager.cpp::startDaemon and
    // extractRpcBindPort().
    quint16 boundPort_ = 0;
    // st-136 (Bug C): version_string captured at probeExistingDaemon()
    // ProbeAdopted time, used by the heartbeat RPC-fallback to detect
    // imposter or version drift. Empty when the daemon was spawned
    // (not adopted). See ProcessManager.cpp::onHeartbeat.
    QString adoptedVersionString_;
    std::chrono::milliseconds heartbeatInterval_{kHeartbeatInterval};

    // Restart bookkeeping. attemptsDone_ counts how many times the
    // current restart cycle has run; lastCategory_ records the most
    // recent classification so the grace timer can decide whether to
    // fire unlockOperations().
    int attemptsDone_ = 0;
    ErrorClassifier::Category lastCategory_ = ErrorClassifier::Category::Unknown;
    QString lastReason_;

    // Lock counter. Every lock increments, every unlock decrements.
    // State is "locked" iff the counter is > 0.
    int lockCount_             = 0;
    bool operationsLocked_     = false;

    // Heartbeat timer.
    QTimer* heartbeatTimer_ = nullptr;

    // Pending restart QTimer::singleShot handle, so we can cancel it
    // when the user requests a reset.
    QTimer* pendingRestartTimer_ = nullptr;

    Q_DISABLE_COPY(ProcessManager)
};

} // namespace judecoin_wallet::core::process

#endif // JUDECOIN_WALLET_CORE_PROCESS_PROCESS_MANAGER_H
