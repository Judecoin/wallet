// =============================================================================
// Judecoin Wallet - StartupManager (st-015)
//
// Implements the V5.4.8 5-step startup flow that brings the UI to a
// "Ready" state where the rest of the application can talk to a known-
// good daemon + wallet-rpc pair and an authoritative command set has
// been loaded.
//
// Five steps, in order:
//   1. Read QSettings (host/port + persistence)  -> SettingsManager
//   2. Start daemon                              -> ProcessManager
//                                                   (PortConflictDetector
//                                                    picks a free port)
//   3. Start wallet-rpc                          -> ProcessManager
//                                                   (follows daemon
//                                                    because it depends
//                                                    on daemon RPC)
//   4. daemon.get_info + VersionChecker >= v3.2.0
//                                                -> DaemonClient + VersionChecker
//   5. Load v<version>.json                      -> CommandSetLoader
//
// The 5-step state machine (and 8 actual enum states - the two
// "ready/failed" terminators are part of the machine too):
//
//   Idle
//     -> LoadingSettings
//        -> StartingDaemon
//           -> StartingWalletRpc
//              -> VerifyingVersion
//                 -> LoadingCommands
//                    -> Ready                       (terminal success)
//
//   any non-terminal -> Failed                      (terminal error)
//
// V5.4.2 mainnet-only: every step is single-purpose; we never branch
// on nettype.
//
// V5.4.7 compliance: no key files are touched, no key derivation
// happens. The CommandSet we hand to the UI is just the parsed JSON
// from audit/cli_commands/v<version>.json.
//
// Why this is sequential and not parallel
// ---------------------------------------
//   Step 3 (wallet-rpc) must follow step 2 (daemon) - wallet-rpc
//   connects to the daemon by URL. Step 4 (get_info) must follow
//   step 3 (need a running daemon to talk to). Step 5 (load command
//   set) must follow step 4 (need to know the daemon version). So
//   the state machine is intrinsically serial; we drive it with
//   QTimer::singleShot(0, ...) so each step is one event-loop tick
//   and the QML loader spinner can repaint between steps.
//
// Signal contract:
//   stateChanged(State)         - emitted on every state transition.
//   startupProgress(int step,   - emitted as each step begins. step
//                  QString msg)    is 1..5 (matches the devplan
//                                  numbering), msg is human-friendly.
//   startupComplete(CommandSet) - emitted exactly once, on transition
//                                  to Ready. The CommandSet is the
//                                  authoritative artefact the rest
//                                  of the UI binds to.
//   startupFailed(QString       - emitted exactly once, on transition
//                  reason,          to Failed. `reason` is human
//                  int step)        -readable, `step` is the 1..5
//                                  number of the step that failed
//                                  (0 if failure happened in the
//                                  Idle pre-state).
//
// Lifetime:
//   QObject parent / heap-allocated. start() is one-shot; calling it
//   again after Ready/Failed is a no-op (returns false). reset() puts
//   the manager back to Idle so start() can be tried again after a
//   fix-it dialog.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_STARTUP_STARTUP_MANAGER_H
#define JUDECOIN_WALLET_CORE_STARTUP_STARTUP_MANAGER_H

#include "core/commands/CommandSetLoader.h"

#include <QObject>
#include <QString>

class QTimer;

namespace judecoin_wallet::core::settings {
class SettingsManager;
} // namespace judecoin_wallet::core::settings

namespace judecoin_wallet::core::process {
class ProcessManager;
} // namespace judecoin_wallet::core::process

namespace judecoin_wallet::core::network {
class PortConflictDetector;
class RemoteNodeManager;
} // namespace judecoin_wallet::core::network

namespace judecoin_wallet::core::rpc {
class DaemonClient;
} // namespace judecoin_wallet::core::rpc

namespace judecoin_wallet::core::wallet {
class WalletManager;
} // namespace judecoin_wallet::core::wallet

namespace judecoin_wallet::core::startup {

// -----------------------------------------------------------------------------
// StartupManager
//
// Drives the 5-step boot flow. Owns a single QTimer (0-ms, singleShot)
// used to hop between states. Holds non-owning pointers to the
// SettingsManager, ProcessManager, PortConflictDetector, DaemonClient;
// the caller wires them in via the constructor or setters and is
// responsible for their lifetime.
//
// The 5-step state machine is exposed via the State enum and the
// Q_PROPERTY state so QML can render "step 3 of 5: starting wallet-rpc".
// -----------------------------------------------------------------------------
class StartupManager : public QObject
{
    Q_OBJECT

    // Q_PROPERTY state - QML binds to "state" / "currentState" for the
    // boot progress UI. NOTIFY stateChanged.
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    // Top-level coordinator state machine.
    //
    // The 5 in-flight states (LoadingSettings through LoadingCommands)
    // correspond 1:1 to the 5 V5.4.8 boot steps. Idle is the pre-start
    // state; Ready / Failed are the two terminators.
    //
    // The enum is exposed as Q_ENUM so QML can write
    // startupManager.state == StartupManager.Ready without a string
    // compare.
    enum class State {
        Idle = 0,                      // initial; nothing has been done
        LoadingSettings = 1,           // step 1: reading QSettings
        VerifyingBinaryReady = 2,      // step 1.5: async FUSE squashfs binary check (st-142)
        StartingDaemon = 3,            // step 2: ProcessManager::startDaemon
        StartingWalletRpc = 4,         // step 3: ProcessManager::startWalletRpc
        VerifyingVersion = 5,          // step 4: daemon.get_info + VersionChecker
        LoadingCommands = 6,           // step 5: CommandSetLoader::load
        Ready = 7,                     // terminal success
        Failed = 8,                    // terminal failure
        // st-143 (FUSE race fix v2, 2026-07-17 14:02):
        // inserted BETWEEN VerifyingBinaryReady (state=2) and
        // StartingDaemon (state=3) so the QML scene has fully loaded
        // (FUSE cold->warm) before we exec judecoind. State numerical
        // value 9 is appended after the terminal Failed (8) so the
        // canonical step numbers 1..6 (LoadCommandSet..VerifyBinaryReady)
        // stay aligned with the Step enum.
        WaitingForUiReady = 9,
    };
    Q_ENUM(State)

    // Step numbering for the 5 V5.4.8 boot steps. These are the
    // 1..5 numbers emitted in startupProgress() and stored in
    // startupFailed(). They are NOT the State enum values (LoadingX
    // etc. happen to line up numerically, but a future step insertion
    // would break the alignment; keep these as the canonical step
    // number).
    enum class Step {
        ReadSettings      = 1,
        StartDaemon       = 2,
        StartWalletRpc    = 3,
        VerifyVersion     = 4,
        LoadCommandSet    = 5,
        // st-142 (2026-07-17, FUSE cold-start fix): VerifyBinaryReady
        // is added as an internal step that sits between ReadSettings
        // and StartDaemon. Its canonical number (6) is appended at the
        // END of the enum to preserve the 1..5 step numbers emitted to
        // QML startupProgress signals — see comment above on the Step
        // vs. State numbering convention.
        VerifyBinaryReady = 6,
        // st-143: WaitForUiReady is the QML-blocking step that sits
        // AFTER VerifyBinaryReady and BEFORE StartDaemon. Its canonical
        // step number 7 is appended at the end of the enum to preserve
        // the 1..5 numbers emitted to QML startupProgress signals.
        WaitForUiReady    = 7,
    };
    Q_ENUM(Step)

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    // Empty ctor: no dependencies wired. The manager will refuse to
    // start() until all four setters below have been called.
    explicit StartupManager(QObject* parent = nullptr);

    // Convenience ctor: wires all four dependencies at once. Caller
    // retains ownership.
    StartupManager(judecoin_wallet::core::settings::SettingsManager* settings,
                   judecoin_wallet::core::process::ProcessManager*   process,
                   judecoin_wallet::core::network::PortConflictDetector* portDetector,
                   judecoin_wallet::core::rpc::DaemonClient*        daemon,
                   QObject* parent = nullptr);

    ~StartupManager() override;

    // -------------------------------------------------------------------------
    // Dependency wiring (only valid before start() is called)
    // -------------------------------------------------------------------------

    void setSettingsManager(judecoin_wallet::core::settings::SettingsManager* s);
    void setProcessManager(judecoin_wallet::core::process::ProcessManager*   p);
    void setPortConflictDetector(judecoin_wallet::core::network::PortConflictDetector* p);
    void setDaemonClient(judecoin_wallet::core::rpc::DaemonClient* d);
    void setRemoteNodeManager(judecoin_wallet::core::network::RemoteNodeManager* r);

    // FIX-G7 (2026-07-17 20:43): wire WalletManager so that
    // stepStartWalletRpc() can update its WalletRpcClient endpoint to
    // the actual negotiated port (not the hardcoded 19071 default).
    // Without this, PortConflictDetector may pick port 19072 but the
    // WalletRpcClient stays on 19071 → timeout (-32001).
    void setWalletManager(judecoin_wallet::core::wallet::WalletManager* wm);

    // Override the daemon / wallet-rpc binary path. Production sets
    // these via QSettings or environment; tests override to /bin/sleep
    // (or any long-lived process) so stepStartDaemon / stepStartWalletRpc
    // can succeed without the real Judecoin binaries.
    void setDaemonProgramPath(const QString& path)    { daemonProgramPath_    = path; }
    void setWalletRpcProgramPath(const QString& path) { walletRpcProgramPath_ = path; }

    // Override the daemon / wallet-rpc argv. When non-empty, these
    // replace the V5.4.8 production flags (--p2p-bind, --rpc-bind, etc.)
    // that stepStartDaemon / stepStartWalletRpc would otherwise pass.
    // Tests use this to point at /bin/sleep and pass "60" so the
    // process actually stays alive for the duration of the test.
    void setDaemonArgumentsOverride(const QStringList& args)
    {
        daemonArgumentsOverride_ = args;
    }
    void setWalletRpcArgumentsOverride(const QStringList& args)
    {
        walletRpcArgumentsOverride_ = args;
    }

    // st-143 (FUSE race fix v2, 14:02): Q_INVOKABLE so QML's
    // main.qml Component.onCompleted can call this once the QML tree
    // is fully constructed. By then the FUSE squashfs page cache is
    // warm and exec(judecoind) no longer races with QML loading.
    //
    // CRITICAL: must be `public` + `Q_INVOKABLE` or the QML meta-object
    // system won't expose the method (st-143 v1 bug: was placed in
    // the private section without Q_INVOKABLE → main.qml:329 threw
    // `TypeError: Property 'notifyUiReady' is not a function`).
    Q_INVOKABLE void notifyUiReady();

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    // Drive the 5-step boot. One-shot: a second call after Ready/Failed
    // is a no-op (returns false) - call reset() first if you want to
    // try again. Returns true on the first invocation only.
    bool start();

    // Reset to Idle so start() can be called again. The cached
    // CommandSet is dropped. Pending QTimer hops are cancelled.
    void reset();

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    State state() const noexcept { return state_; }

    // The 1..5 step number the manager is currently executing (or 0
    // if Idle / Ready / Failed). Mirrors the `state_` mapping.
    int currentStep() const noexcept;

    // The loaded CommandSet. Valid only when state() == Ready. Empty
    // / fallback otherwise.
    const judecoin_wallet::core::commands::CommandSet& commandSet() const
    {
        return commandSet_;
    }

    // Last error message, populated when state() == Failed. Empty
    // otherwise.
    QString lastError() const noexcept { return lastError_; }

    // The step number (1..5) at which the most recent failure
    // happened. 0 if no failure yet. Mirrors the value emitted in
    // startupFailed.
    int lastFailedStep() const noexcept { return lastFailedStep_; }

    // Pure helpers (no Qt side effects) - exposed for tests + the
    // QML layer's "show what was loaded" diagnostics page.

    // Map a V5.4.4-valid daemon version_string (e.g.
    // "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)") to the
    // canonical "vMAJOR.MINOR.PATCH" filename stem. Thin wrapper
    // around VersionChecker::parse().toVersionString() so the
    // StartupManager and its tests don't have to include
    // VersionChecker.h directly.
    static QString canonicalVersionString(const QString& versionString);

    // True iff the supplied version string is supported
    // (>= v3.2.0). Wraps VersionChecker::isSupported().
    static bool isVersionSupported(const QString& versionString);

    // Human-readable label for each of the 5 V5.4.8 boot steps.
    // Used by the QML progress UI to render "Step 3 of 5: <label>".
    static QString stepLabel(int step);

    // Human-readable label for each State enum value. Used by the
    // QML "current state" badge.
    static QString stateName(State s);

signals:
    // Fired on every state transition. The argument is the NEW state.
    void stateChanged(judecoin_wallet::core::startup::StartupManager::State state);

    // Fired as each of the 5 steps BEGINS. `step` is 1..5, `message`
    // is the human label (see stepLabel). Listeners can use this to
    // advance a progress bar; stateChanged will ALSO fire on the
    // same transition (because the state changed) so the two
    // signals are paired.
    void startupProgress(int step, const QString& message);

    // Fired exactly once, on the Idle->LoadingSettings transition
    // implied by start(). `message` is the welcome banner ("Starting
    // judecoin-wallet-ui"). Optional - QML can listen to this to
    // show the splash; the per-step progress is the same idea but
    // for each step.
    void startupStarted();

    // Fired exactly once, on the LoadingCommands->Ready transition.
    // The CommandSet is the authoritative artefact; UI layer should
    // bind to this and stop the splash.
    void startupComplete(
        judecoin_wallet::core::commands::CommandSet set);

    // Fired exactly once, on any->Failed transition. `reason` is
    // human-readable, `step` is the 1..5 number of the step that
    // failed (or 0 if failure happened pre-start, e.g. missing
    // dependencies). After this fires the manager stays in Failed
    // until reset() is called.
    void startupFailed(const QString& reason, int step);

private slots:
    // Drive the state machine: one QTimer::singleShot(0) hop per
    // step. runStateMachine() looks at state_ and dispatches to the
    // right step function, then either advances state_ and re-arms
    // the timer or transitions to a terminal state.
    void runStateMachine();

private:
    // Step 1: read SettingsManager. Caches the host/port on this
    // object so the ProcessManager::setDaemonSpec() call can pull
    // from it. Returns false on failure (with lastError_ set).
    bool stepReadSettings();
    // st-142: async FUSE squashfs binary readiness probe; re-runs
    // via scheduleNextStep() while the AppImage mount is cold.
    bool stepVerifyBinaryReady();

    // st-143: notifyUiReady() declaration moved to public section
    // above (line ~234) with Q_INVOKABLE — QML meta-object requires both.

    // Step 2: pick a free port (PortConflictDetector) and start
    // the daemon. Returns false on failure.
    bool stepStartDaemon();

    // Step 3: start wallet-rpc. The wallet-rpc binary reads its
    // daemon URL from settings (we set it during step 1). Returns
    // false on failure.
    bool stepStartWalletRpc();

    // Step 4: call DaemonClient::fetchInfo() and validate the
    // version. Returns false on failure (unsupported version or
    // RPC error).
    bool stepVerifyVersion();

    // Step 5: load v<version>.json via CommandSetLoader. Returns
    // false on failure (no JSON for the version we just validated).
    bool stepLoadCommandSet();

    // Helper: transition to a new non-terminal state. Emits
    // stateChanged and re-arms the QTimer for the next hop.
    void enterState(State s);

    // Helper: transition to Failed. Emits stateChanged and
    // startupFailed; stops the QTimer.
    void enterFailed(const QString& reason, int step);

    // Helper: transition to Ready. Emits stateChanged and
    // startupComplete; stops the QTimer.
    void enterReady();

    // Helper: arm a 0-ms singleShot timer that calls
    // runStateMachine(). Used between steps so QML can repaint.
    void scheduleNextStep();

    // Dependency pointers (caller-owned).
    judecoin_wallet::core::settings::SettingsManager* settings_ = nullptr;
    judecoin_wallet::core::process::ProcessManager*   process_  = nullptr;
    judecoin_wallet::core::network::PortConflictDetector* portDetector_ = nullptr;
    judecoin_wallet::core::rpc::DaemonClient*        daemon_   = nullptr;
    judecoin_wallet::core::network::RemoteNodeManager* remoteNode_ = nullptr;
    bool                                              useRemoteDaemon_ = false;

    // Binary paths (overridable for tests).
    QString  daemonProgramPath_    = QStringLiteral("judecoind");

    // st-143: flipped by notifyUiReady() when QML signals it has
    // finished loading. runStateMachine() polls this in the
    // State::WaitingForUiReady branch and only advances to
    // State::StartingDaemon once it is true.
    bool     uiReadyReceived_      = false;
    QString  walletRpcProgramPath_ = QStringLiteral("judecoin-wallet-rpc");

    // Binary arguments (overridable for tests). When non-empty, replace
    // the production flags so tests can spawn /bin/sleep with "60".
    QStringList daemonArgumentsOverride_;
    QStringList walletRpcArgumentsOverride_;

    // Cached values from step 1 (used by step 2 to build
    // ProcessSpec and step 3 to wire wallet-rpc).
    QString  cachedHost_         = QStringLiteral("127.0.0.1");
    // st-172 (2026-07-19): local bind host for the wallet-rpc child.
    // cachedHost_ is REPLACED by the remote daemon host when the user
    // picks a Custom/preset remote node (stepReadSettings st-023), but
    // wallet-rpc is ALWAYS spawned locally: it must bind, be probed,
    // be reaped, and be RPC-addressed on the LOCAL host. Using
    // cachedHost_ for those broke custom-remote mode (wallet-rpc tried
    // to bind the remote IP -> spawn failed).
    QString  cachedWalletRpcHost_ = QStringLiteral("127.0.0.1");
    int      cachedDaemonPort_   = 19061;
    int      cachedWalletRpcPort_= 19071;
    int      cachedP2pPort_      = 19060;
    int      cachedZmqPort_      = 19062;
    int      cachedZmqPubPort_   = 19063;
    QString  cachedDataDir_;
    QString  cachedLogLevel_     = QStringLiteral("info");

    // The CommandSet we produced in step 5.
    judecoin_wallet::core::commands::CommandSet commandSet_;

    // The daemon version_string we observed in step 4 (cached so
    // step 5 + the SettingsManager "About" page can both consume
    // it without re-querying).
    QString  observedVersionString_;
    QString  observedCanonicalVersion_;

    State  state_          = State::Idle;
    QString lastError_;
    int    lastFailedStep_ = 0;

    // One QTimer is enough - we set singleShot(true) and call
    // start() between steps.
    QTimer* stepTimer_     = nullptr;

    // FIX-G7: non-owning pointer to WalletManager; used to update
    // its WalletRpcClient endpoint after wallet-rpc starts on the
    // negotiated port.
    judecoin_wallet::core::wallet::WalletManager* walletManager_ = nullptr;

    // True between start() and the next terminal (Ready / Failed)
    // transition. We refuse to start() twice within the same run.
    bool   inFlight_       = false;

    Q_DISABLE_COPY(StartupManager)
};

} // namespace judecoin_wallet::core::startup

#endif // JUDECOIN_WALLET_CORE_STARTUP_STARTUP_MANAGER_H
