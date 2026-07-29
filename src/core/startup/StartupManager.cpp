// =============================================================================
// Judecoin Wallet - StartupManager (st-015) implementation
//
// One StartupManager, one boot. The state machine is driven via
// QTimer::singleShot(0, ...) so each step lands in its own event-loop
// tick - this lets the QML loader spinner repaint between steps and
// keeps the dependencies (ProcessManager, PortConflictDetector,
// DaemonClient) all event-driven, not blocking.
// =============================================================================

#include "core/startup/StartupManager.h"

#include "core/commands/CommandSetLoader.h"
#include "core/network/PortConflictDetector.h"
#include "core/network/RemoteNodeManager.h"
#include "core/process/ProcessManager.h"
#include "core/rpc/DaemonClient.h"
#include "core/settings/SettingsManager.h"
#include "core/version/VersionChecker.h"
#include "core/wallet/WalletManager.h"

#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QDir>
#include <chrono>
#include <thread>

namespace judecoin_wallet::core::startup {

// We use these names a lot; alias them once for readability.
using settings::SettingsManager;
using process::ProcessManager;
using network::PortConflictDetector;
using process::ProcessSpec;
using network::ServiceKind;
using rpc::DaemonClient;
using rpc::DaemonInfo;
using version::VersionChecker;
using commands::CommandSet;
using commands::CommandSetLoader;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

StartupManager::StartupManager(QObject* parent)
    : QObject(parent)
{
    // singleShot timer is reused between steps.
    stepTimer_ = new QTimer(this);
    stepTimer_->setSingleShot(true);
    stepTimer_->setInterval(0);
    connect(stepTimer_, &QTimer::timeout,
            this, &StartupManager::runStateMachine);
}

StartupManager::StartupManager(SettingsManager* settings,
                               ProcessManager*   process,
                               PortConflictDetector* portDetector,
                               DaemonClient*     daemon,
                               QObject* parent)
    : StartupManager(parent)
{
    settings_      = settings;
    process_       = process;
    portDetector_  = portDetector;
    daemon_        = daemon;
}

StartupManager::~StartupManager() = default;

// ---------------------------------------------------------------------------
// Wiring setters (only valid before start())
// ---------------------------------------------------------------------------

void StartupManager::setSettingsManager(SettingsManager* s)        { settings_     = s; }
void StartupManager::setProcessManager(ProcessManager* p)           { process_      = p; }
void StartupManager::setPortConflictDetector(PortConflictDetector* p){ portDetector_ = p; }
void StartupManager::setDaemonClient(DaemonClient* d)              { daemon_       = d; }
void StartupManager::setRemoteNodeManager(network::RemoteNodeManager* r){ remoteNode_   = r; }
void StartupManager::setWalletManager(wallet::WalletManager* wm)    { walletManager_ = wm; }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool StartupManager::start()
{
    // One-shot: if we're already in a run (in_flight), or in a
    // terminal state, refuse. The caller must reset() first.
    if (inFlight_) {
        return false;
    }
    if (state_ == State::Ready || state_ == State::Failed) {
        return false;
    }
    if (state_ != State::Idle) {
        // Mid-run: also refuse.
        return false;
    }

    // Sanity-check the four dependencies. We don't take ownership;
    // null here means the caller forgot to wire us up.
    if (settings_     == nullptr ||
        process_      == nullptr ||
        portDetector_ == nullptr ||
        daemon_       == nullptr) {
        enterFailed(QStringLiteral(
            "StartupManager: missing dependency (settings, process, "
            "portDetector, or daemon not wired)"), 0);
        return false;
    }

    inFlight_ = true;
    emit startupStarted();
    // Step 1 begins: LoadingSettings.
    enterState(State::LoadingSettings);
    emit startupProgress(
        static_cast<int>(Step::ReadSettings),
        stepLabel(static_cast<int>(Step::ReadSettings)));
    scheduleNextStep();
    return true;
}

void StartupManager::reset()
{
    // Cancel any pending step hop.
    if (stepTimer_ != nullptr) {
        stepTimer_->stop();
    }
    state_             = State::Idle;
    inFlight_          = false;
    lastError_.clear();
    lastFailedStep_    = 0;
    commandSet_        = CommandSet{};
    observedVersionString_.clear();
    observedCanonicalVersion_.clear();
    // Cached settings values are kept - they are not state, just a
    // cache. Next start() re-reads them in step 1 anyway.
    emit stateChanged(state_);
}

int StartupManager::currentStep() const noexcept
{
    switch (state_) {
        case State::LoadingSettings:     return static_cast<int>(Step::ReadSettings);
        case State::VerifyingBinaryReady: return static_cast<int>(Step::VerifyBinaryReady);
        case State::WaitingForUiReady:   return static_cast<int>(Step::WaitForUiReady);
        case State::StartingDaemon:      return static_cast<int>(Step::StartDaemon);
        case State::StartingWalletRpc:   return static_cast<int>(Step::StartWalletRpc);
        case State::VerifyingVersion:    return static_cast<int>(Step::VerifyVersion);
        case State::LoadingCommands:     return static_cast<int>(Step::LoadCommandSet);
        default:                         return 0;
    }
}

QString StartupManager::canonicalVersionString(const QString& versionString)
{
    const auto info = VersionChecker::parse(versionString);
    if (!info.valid) {
        return QString();
    }
    return info.toVersionString();
}

bool StartupManager::isVersionSupported(const QString& versionString)
{
    return VersionChecker::isSupported(versionString);
}

QString StartupManager::stepLabel(int step)
{
    switch (step) {
        case static_cast<int>(Step::ReadSettings):     return QStringLiteral("Reading settings");
        case static_cast<int>(Step::StartDaemon):      return QStringLiteral("Starting daemon");
        case static_cast<int>(Step::StartWalletRpc):   return QStringLiteral("Starting wallet-rpc");
        case static_cast<int>(Step::VerifyVersion):    return QStringLiteral("Verifying daemon version");
        case static_cast<int>(Step::LoadCommandSet):   return QStringLiteral("Loading command set");
        case static_cast<int>(Step::VerifyBinaryReady): return QStringLiteral("Verifying daemon binary");
        case static_cast<int>(Step::WaitForUiReady):    return QStringLiteral("Waiting for UI ready");
        default:                                        return QStringLiteral("Unknown step");
    }
}

QString StartupManager::stateName(State s)
{
    switch (s) {
        case State::Idle:               return QStringLiteral("Idle");
        case State::LoadingSettings:     return QStringLiteral("LoadingSettings");
        case State::StartingDaemon:      return QStringLiteral("StartingDaemon");
        case State::StartingWalletRpc:   return QStringLiteral("StartingWalletRpc");
        case State::VerifyingVersion:    return QStringLiteral("VerifyingVersion");
        case State::LoadingCommands:     return QStringLiteral("LoadingCommands");
        case State::Ready:               return QStringLiteral("Ready");
        case State::Failed:              return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

// ---------------------------------------------------------------------------
// State-machine driver
// ---------------------------------------------------------------------------

void StartupManager::runStateMachine()
{
    // A single dispatch table: each step owns its work, returns false
    // on failure, returns true on success (so we advance).
    bool ok = false;
    switch (state_) {
        case State::LoadingSettings:
            ok = stepReadSettings();
            if (ok) {
                // st-142 (FUSE cold-start fix): after reading settings, do NOT
                // jump straight to StartingDaemon. Insert VerifyingBinaryReady
                // between LoadingSettings and StartingDaemon so that the
                // FUSE squashfs mount has a chance to warm before we exec
                // judecoind. Without this, the UI's own Qt6/QML FUSE reads
                // race against judecoind's first exec and the daemon's 8s
                // waitForConnected(19061) window expires before bind.
                enterState(State::VerifyingBinaryReady);
                emit startupProgress(
                    static_cast<int>(Step::VerifyBinaryReady),
                    stepLabel(static_cast<int>(Step::VerifyBinaryReady)));
            }
            break;

        case State::VerifyingBinaryReady:
            ok = stepVerifyBinaryReady();
            if (ok) {
                // st-143 (FUSE race fix v2, 14:02):
                // After confirming the daemon binary exists (stat-only,
                // cheap), do NOT spawn judecoind immediately. Park in
                // WaitingForUiReady until main.qml's Component.onCompleted
                // calls notifyUiReady() - by that point the QML tree is
                // fully loaded and all Qt6 .so + QML files have been
                // pulled through FUSE, so exec(judecoind) no longer races
                // with QML loading for the FUSE driver.
                enterState(State::WaitingForUiReady);
                emit startupProgress(
                    static_cast<int>(Step::WaitForUiReady),
                    stepLabel(static_cast<int>(Step::WaitForUiReady)));
            }
            break;

        case State::WaitingForUiReady:
            // st-143: poll uiReadyReceived_ on each event-loop tick.
            // scheduleNextStep() retries with 0 ms interval; QML's
            // Component.onCompleted fires once the QML tree is built,
            // which is typically tens of milliseconds after engine.load.
            if (uiReadyReceived_) {
                qInfo().noquote()
                    << "st-143: uiReadyReceived=true, advancing to StartingDaemon";
                enterState(State::StartingDaemon);
                emit startupProgress(
                    static_cast<int>(Step::StartDaemon),
                    stepLabel(static_cast<int>(Step::StartDaemon)));
                // st-143b (2026-07-17 14:31): must set ok=true so
                // runStateMachine does NOT fall through to the
                // if(!ok) enterFailed() guard below.
                ok = true;
            } else {
                qInfo().noquote()
                    << "st-143: WaitingForUiReady - uiReadyReceived=false, retrying next tick";
                scheduleNextStep();
            }
            break;

        case State::StartingDaemon:
            ok = stepStartDaemon();
            if (ok) {
                enterState(State::StartingWalletRpc);
                emit startupProgress(
                    static_cast<int>(Step::StartWalletRpc),
                    stepLabel(static_cast<int>(Step::StartWalletRpc)));
            }
            break;

        case State::StartingWalletRpc:
            ok = stepStartWalletRpc();
            if (ok) {
                enterState(State::VerifyingVersion);
                emit startupProgress(
                    static_cast<int>(Step::VerifyVersion),
                    stepLabel(static_cast<int>(Step::VerifyVersion)));
            }
            break;

        case State::VerifyingVersion:
            ok = stepVerifyVersion();
            if (ok) {
                enterState(State::LoadingCommands);
                emit startupProgress(
                    static_cast<int>(Step::LoadCommandSet),
                    stepLabel(static_cast<int>(Step::LoadCommandSet)));
            }
            break;

        case State::LoadingCommands:
            ok = stepLoadCommandSet();
            if (ok) {
                enterReady();
                return;  // enterReady stops the timer + emits signals.
            }
            break;

        // Terminal states and the Idle pre-state: nothing to do.
        case State::Idle:
        case State::Ready:
        case State::Failed:
        default:
            return;
    }

    if (!ok) {
        // The failing step function already populated lastError_;
        // the only thing it didn't know is its own step number, so
        // we attach it now via enterFailed().
        const int failedStep = currentStep();
        enterFailed(lastError_.isEmpty()
                        ? QStringLiteral("StartupManager: step failed")
                        : lastError_,
                    failedStep);
        return;
    }

    // Successful step - hop to the next one on the next event-loop tick.
    scheduleNextStep();
}

void StartupManager::enterState(State s)
{
    state_ = s;
    emit stateChanged(s);
}

void StartupManager::enterFailed(const QString& reason, int step)
{
    if (stepTimer_ != nullptr) {
        stepTimer_->stop();
    }
    lastError_      = reason;
    lastFailedStep_ = step;
    state_          = State::Failed;
    inFlight_       = false;
    emit stateChanged(State::Failed);
    emit startupFailed(reason, step);
}

void StartupManager::enterReady()
{
    if (stepTimer_ != nullptr) {
        stepTimer_->stop();
    }
    state_    = State::Ready;
    inFlight_ = false;
    emit stateChanged(State::Ready);
    emit startupComplete(commandSet_);
}

void StartupManager::scheduleNextStep()
{
    if (stepTimer_ == nullptr) return;
    stepTimer_->start();  // 0 ms - next event-loop iteration.
}

// ---------------------------------------------------------------------------
// Step 1: read SettingsManager
// ---------------------------------------------------------------------------

bool StartupManager::stepReadSettings()
{
    // SettingsManager already coerces invalid values; we just trust it.
    // V1.0 default: read the LOCAL daemon host/port (start our own
    // judecoind). V1.1 (st-023): if the user picked a remote node via
    // RemoteNodeManager, override with the resolved (host, port).
    cachedHost_          = settings_->networkDaemonHost();
    cachedDaemonPort_    = settings_->networkDaemonPort();
    // st-172: freeze the LOCAL host for wallet-rpc BEFORE the st-023
    // remote override below can replace cachedHost_. wallet-rpc always
    // runs on this machine regardless of where the daemon lives.
    cachedWalletRpcHost_ = cachedHost_.isEmpty()
        ? QStringLiteral("127.0.0.1") : cachedHost_;
    // Wallet-rpc port - SettingsManager doesn't expose a network-wallet-rpc
    // host/port in V1.0 (wallet-rpc is local-only). The default
    // 19071 (V5.4.2 mainnet-only) is hard-coded here. st-023 may
    // surface a configurable wallet-rpc port.
    cachedWalletRpcPort_ = 19071;
    cachedP2pPort_       = 19060;
    cachedZmqPort_       = 19062;
    cachedZmqPubPort_    = 19063;
    cachedLogLevel_      = settings_->advancedLogLevel();

    // st-133c (P0 NEW) + st-134 (XDG migration): where judecoind keeps
    // blockchain + wallet state. SettingsManager::dataDirectory()
    // returns:
    //   (1) the stored QSettings value (e.g. from a previous Settings
    //       page edit), OR
    //   (2) the JUDECOIN_DATA_DIR env override (consumed by main.cpp
    //       -> settingsManager.setDataDirectory() at startup), OR
    //   (3) the XDG-aware default
    //       ($XDG_DATA_HOME/judecoin-mainnet/, fallback
    //       $HOME/.local/share/judecoin-mainnet/).
    // We cache it here and create the directory eagerly so step 2
    // can pass --data-dir without race-y "create or fail" handling.
    cachedDataDir_       = settings_->dataDirectory();
    if (cachedDataDir_.isEmpty()) {
        // Defensive: SettingsManager should always return a non-empty
        // path (its XDG-aware fallback never returns empty), but a
        // broken .conf or future refactor could produce empty. Mirror
        // SettingsManager::dataDirectory() so the two stay in sync,
        // and emit a clear warning so the diagnostic surface points
        // back at SettingsManager if they ever drift.
        qWarning("StartupManager: SettingsManager::dataDirectory() "
                 "returned empty; applying XDG fallback manually");
        const QString xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
        const QString base = xdgDataHome.isEmpty()
                                 ? (QDir::homePath() + QStringLiteral("/.local/share"))
                                 : xdgDataHome;
        cachedDataDir_ = base + QStringLiteral("/judecoin-mainnet");
    }
    // mkpath is idempotent and returns true if the directory already
    // exists. We don't fail the boot if it can't be created here --
    // step 2 will surface a clearer error if judecoind actually
    // refuses to write to it.
    QDir().mkpath(cachedDataDir_);

    if (cachedHost_.isEmpty()) {
        lastError_ = QStringLiteral("SettingsManager: daemon host is empty");
        return false;
    }
    if (cachedDaemonPort_ <= 0 || cachedDaemonPort_ > 65535) {
        lastError_ = QStringLiteral(
            "SettingsManager: daemon port out of range (%1)")
            .arg(cachedDaemonPort_);
        return false;
    }
    if (cachedLogLevel_.isEmpty()) {
        cachedLogLevel_ = QStringLiteral("info");
    }

    // st-023: layer the remote-node decision on top. If the user picked
    // a remote node (preset OR custom), RemoteNodeManager hands us the
    // resolved host/port; we use those instead of spawning a local
    // daemon. The local daemon (step 2) only runs when no remote node
    // is active.
    if (remoteNode_ != nullptr) {
        remoteNode_->refresh();
        const QString rh = remoteNode_->activeHost();
        const int     rp = remoteNode_->activePort();
        if (!rh.isEmpty() && rp > 0 && rp <= 65535) {
            cachedHost_       = rh;
            cachedDaemonPort_ = rp;
            useRemoteDaemon_  = true;
        } else {
            useRemoteDaemon_  = false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Step 1.5 (st-142): verify FUSE squashfs daemon binary is ready
// ---------------------------------------------------------------------------
//
// Symptom this fixes (2026-07-17 13:36 observation):
//   - Manual spawn of $APPDIR/usr/bin/judecoind from a warm FUSE mount
//     listens on 19061 in +4s.
//   - UI spawn from a *cold* FUSE mount (AppImage just launched, QML
//     loader still pulling Qt6 .so via FUSE) takes >8s and times out
//     in ProcessManager::waitForConnected(8000).
//   - QFileInfo::exists() / QFileInfo::isExecutable() trigger a FUSE
//     read on first call, so this is a synchronous (but cheap) probe
//     that returns false until the squashfs metadata + ELF header are
//     actually readable.
//
// Why we use scheduleNextStep() (not QThread::sleep_for / std::sleep_for):
//   - The step function returns false; runStateMachine's "if (!ok) ..." branch
//     in turn calls enterFailed() ONLY if the step has populated lastError_.
//     stepVerifyBinaryReady deliberately does NOT touch lastError_ on the
//     not-ready path; instead it calls scheduleNextStep() directly so the
//     stepTimer fires on the next event-loop tick and re-enters this case
//     for another probe. This keeps the QML loader spinner repainting
//     between probes and never blocks the UI thread.
//   - 0 ms interval = effectively "try again on the next tick"; FUSE
//     cold reads complete in tens of ms at most, so this converges in
//     1-3 ticks even on the slowest AppImage starts.

bool StartupManager::stepVerifyBinaryReady()
{
    const QString path = daemonProgramPath_;
    if (path.isEmpty()) {
        // No daemon path configured yet (e.g. tests); treat as permanent
        // failure rather than spinning forever.
        lastError_ = QStringLiteral(
            "StartupManager: daemonProgramPath_ is empty (st-142)");
        qWarning().noquote()
            << "st-142: daemonProgramPath_ is empty - cannot probe";
        return false;
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isExecutable()) {
        // FUSE squashfs not ready yet (cold start). Re-probe on next tick.
        qWarning().noquote()
            << "st-142: daemon binary not ready yet (" << path
            << "), rescheduling via stepTimer";
        scheduleNextStep();
        return false;
    }

    // st-143c (2026-07-17 14:42): pre-read the daemon binary to warm
    // the FUSE squashfs page cache. QFileInfo::exists() only does
    // stat(2) — it returns the inode/mode without ever reading file
    // content. When ProcessManager later calls exec(2) on the binary,
    // the kernel needs the full ELF content from FUSE, which can take
    // >8 s on a cold squashfs mount and time out in
    // ProcessManager::waitForConnected(8000).
    //
    // Doing a QFile::readAll() here forces squashfuse to decompress +
    // serve the entire binary (~18 MB) so it's in the kernel page
    // cache before exec(). This is a one-time blocking read on the
    // main thread; at startup the UI is showing a spinner anyway, and
    // the read time (~2-4 s warm, ~5-8 s cold) is still shorter than
    // the 8 s timeout that would otherwise fail and force a retry.
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray content = f.readAll();
            f.close();
            qInfo().noquote()
                << "st-143c: daemon binary pre-read complete:" << path
                << "size=" << content.size() << "bytes";
        } else {
            qWarning().noquote()
                << "st-143c: daemon binary open failed:" << path
                << "error=" << f.errorString();
        }
    }

    qInfo().noquote()
        << "st-142: daemon binary ready:" << path;
    return true;
}

// ---------------------------------------------------------------------------
// st-143: notifyUiReady() - called from QML (main.qml Component.onCompleted)
// ---------------------------------------------------------------------------
//
// Purpose: unblock the State::WaitingForUiReady branch in runStateMachine()
// so the state machine can advance to StartingDaemon.
//
// Why a separate Q_INVOKABLE instead of having runStateMachine() poll on
// a QTimer: QML already knows when its tree is fully constructed
// (Component.onCompleted on the root object fires AFTER every QML
// component is instantiated and every Qt6/QML plugin is loaded). Reusing
// this signal avoids both a thread-unsafe busy-poll and a fragile
// "engine has done all its work" heuristic.

void StartupManager::notifyUiReady()
{
    if (uiReadyReceived_) {
        // Idempotent: QML may call this twice (e.g. from a reload path).
        return;
    }
    uiReadyReceived_ = true;
    qInfo().noquote()
        << "st-143: notifyUiReady() called from QML - unblocking WaitingForUiReady";
    scheduleNextStep();
}

// ---------------------------------------------------------------------------
// Step 2: start daemon (PortConflictDetector picks a port)
// ---------------------------------------------------------------------------

bool StartupManager::stepStartDaemon()
{
    if (portDetector_ == nullptr || process_ == nullptr) {
        lastError_ = QStringLiteral("StartupManager: missing port/process dependency");
        return false;
    }

    // st-023: when a remote node is active we don't need a local
    // judecoind at all - the wallet-rpc will be pointed at the
    // remote daemon's host:port via --daemon-address.
    if (useRemoteDaemon_) {
        // st-173 (2026-07-19, flow spec "remote nodes only target the daemon; probe
        // daemon connection healthy before starting local wallet-rpc"): the remote node
        // applies ONLY to the daemon. PROBE the remote daemon's RPC
        // link FIRST and only proceed to step 3 (local wallet-rpc
        // spawn, --daemon-address = this remote) once the link is
        // confirmed healthy. Before st-173 this branch returned true
        // blindly, so a dead / unreachable / wrong-version remote was
        // only discovered at step 4 (VerifyVersion) - AFTER wallet-rpc
        // had already been spawned against it, in the wrong order
        // relative to the required flow.
        //
        // Probe = RemoteNodeManager::testConnection (st-111): TCP
        // connect + get_info + VersionChecker::isSupported, 1500ms
        // budget per attempt. 3 attempts with a 500ms pause cover
        // transient network blips; a VersionMismatch short-circuits
        // (retrying cannot change the remote's version). Blocking
        // here is the established step pattern (stepVerifyVersion
        // already blocks up to ~6s in its own retry loop).
        if (remoteNode_ == nullptr) {
            // Defensive: useRemoteDaemon_ is only ever set in
            // stepReadSettings when remoteNode_ was non-null; keep the
            // pre-st-173 behaviour if a future refactor breaks that
            // invariant instead of hard-failing startup.
            qWarning().noquote()
                << "StartupManager: st-173 remote daemon probe skipped -"
                << "remoteNode_ is null";
            return true;
        }

        using ProbeResult = network::RemoteNodeManager::ConnectionTestResult;
        constexpr int kProbeAttempts = 3;
        ProbeResult pr = ProbeResult::ConnectionRefused;
        for (int attempt = 1; attempt <= kProbeAttempts; ++attempt) {
            pr = remoteNode_->testConnection(cachedHost_, cachedDaemonPort_);
            if (pr == ProbeResult::Ok) {
                qInfo().noquote()
                    << "StartupManager: st-173 remote daemon probe OK at"
                    << QStringLiteral("%1:%2 (attempt %3/%4)")
                           .arg(cachedHost_).arg(cachedDaemonPort_)
                           .arg(attempt).arg(kProbeAttempts);
                return true;  // link healthy -> proceed to wallet-rpc
            }
            if (pr == ProbeResult::VersionMismatch) {
                break;
            }
            if (attempt < kProbeAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        switch (pr) {
            case ProbeResult::VersionMismatch:
                lastError_ = QStringLiteral(
                    "Remote daemon %1:%2 is reachable but its version is "
                    "unsupported (need >= v3.2.0) - not starting wallet-rpc")
                    .arg(cachedHost_).arg(cachedDaemonPort_);
                break;
            case ProbeResult::Timeout:
                lastError_ = QStringLiteral(
                    "Remote daemon %1:%2 did not answer RPC within the "
                    "probe window after %3 attempts - not starting wallet-rpc")
                    .arg(cachedHost_).arg(cachedDaemonPort_).arg(kProbeAttempts);
                break;
            default:
                lastError_ = QStringLiteral(
                    "Remote daemon %1:%2 refused the connection after %3 "
                    "attempts - not starting wallet-rpc")
                    .arg(cachedHost_).arg(cachedDaemonPort_).arg(kProbeAttempts);
                break;
        }
        qWarning().noquote() << "StartupManager:" << lastError_;
        return false;
    }

    // st-P1 (2026-07-16 23:16): before spawning, probe
    // the hardcoded RPC port 19061 for an existing daemon. If one
    // is already listening AND its RPC get_info responds with a
    // supported version, ADOPT it instead of spawning a duplicate.
    // This prevents the "second start fails" scenario:
    // the existing daemon owns the hardcoded p2p (19060)
    // + zmq (19062/19063) ports, so a fresh spawn would fail to
    // bind those and the 6 s fetchInfo retry would burn the budget
    // before the new daemon could finish initialising.
    //
    // ProcessManager::probeExistingDaemon() does the full 2-step
    // probe (TCP bind check + RPC get_info + version check + PID
    // lookup + emit externalDaemonDetected + setDaemonState(Running))
    // so a successful return means we can skip startDaemon() entirely.
    process_->setDaemonProbeEndpoint(cachedHost_, 19061);
    const auto pr = process_->probeExistingDaemon();
    if (pr == ProcessManager::ProbeResult::ProbeAdopted) {
        cachedDaemonPort_ = 19061;
        cachedP2pPort_    = 19060;
        cachedZmqPort_    = 19062;
        cachedZmqPubPort_ = 19063;
        qInfo().noquote()
            << "StartupManager: adopted existing daemon at"
            << cachedHost_ << ":" << cachedDaemonPort_;
        portDetector_->rememberSuccessfulPort(
            ServiceKind::Daemon, static_cast<quint16>(cachedDaemonPort_));
        return true;
    }
    if (pr == ProcessManager::ProbeResult::ProbeVersionMismatch) {
        lastError_ = QStringLiteral(
            "ProcessManager: existing daemon at %1:%2 has an "
            "unsupported version (refusing to spawn a second one)")
            .arg(cachedHost_).arg(19061);
        return false;
    }
    // pr == ProbeNoExisting: continue to spawn below.

    // Pick a free port. PortConflictDetector tries the
    // "remembered-port" first, then the range.
    const quint16 port = portDetector_->detectAvailablePort(
        ServiceKind::Daemon);
    if (port == 0) {
        lastError_ = QStringLiteral(
            "PortConflictDetector: no free port in daemon range (19061-19070)");
        return false;
    }
    cachedDaemonPort_ = static_cast<int>(port);

    // Build the ProcessSpec. We don't have the daemon binary path
    // in the SettingsManager (V1.0 mainnet-only has the binary
    // shipped beside the UI binary). For V1.0 we use a
    // "judecoind" name; the actual path resolution is
    // ProcessManager's job (it expects an absolute path or a name
    // resolvable via PATH). The QML layer will set an absolute
    // path via setDaemonSpec() before calling start() in the
    // AppImage / production layout.
    ProcessSpec spec;
    spec.kind    = ProcessSpec::Kind::Daemon;
    spec.name    = QStringLiteral("judecoind");
    spec.program = daemonProgramPath_;
    // V5.4.x daemon flags: judgecoind v3.2.1 no longer accepts the
    // old combined "--p2p-bind HOST:PORT" / "--rpc-bind HOST:PORT"
    // syntax -- those short forms are ambiguous and the daemon now
    // requires the explicit "--p2p-bind-ip" + "--p2p-bind-port"
    // (and "--rpc-bind-ip" + "--rpc-bind-port") pair. V1.0 mainnet
    // defaults baked in.
    //
    // "--non-interactive" is mandatory: without it, the daemon tries
    // to read commands from stdin (the QProcess pipe) and stalls or
    // exits when the pipe closes.
    //
    // NOTE: --detach is intentionally OMITTED. With --detach, the
    // daemon forks a child and the parent exits, which makes
    // QProcess believe the daemon has terminated; the forked child
    // then has its stdout/stderr pipes closed and crashes silently
    // (no log output past "New log categories"). The ProcessManager
    // owns the daemon lifecycle for the UI's lifetime, so we keep
    // the daemon attached to QProcess and let the AppRun + UI
    // exit handlers (later) handle the daemon shutdown via the
    // shutdown path on UI exit.
    if (daemonArgumentsOverride_.isEmpty()) {
        spec.arguments = {
            QStringLiteral("--non-interactive"),
            // st-133c (P0 NEW): explicit data directory. Without
            // this, judecoind resolves $HOME/.judecoin itself, and
            // inside the AppImage sandbox $HOME is the mount point
            // (e.g. /tmp/.mount_JudecoXXX/), so the lmdb ends up
            // inside the mount and gets wiped on every launch --
            // forcing a full blockchain re-sync. Passing the user's
            // real home explicitly keeps the data across launches.
            QStringLiteral("--data-dir"),
            cachedDataDir_,
            QStringLiteral("--p2p-bind-ip"),
            cachedHost_,
            QStringLiteral("--p2p-bind-port"),
            QString::number(cachedP2pPort_),
            QStringLiteral("--rpc-bind-ip"),
            cachedHost_,
            QStringLiteral("--rpc-bind-port"),
            QString::number(cachedDaemonPort_),
            QStringLiteral("--no-igd"),
            QStringLiteral("--log-level"),
            cachedLogLevel_,
            QStringLiteral("--zmq-rpc-bind-port"),
            QString::number(cachedZmqPort_),
            // --zmq-pub takes an ADDRESS (tcp://ip:port or ipc://path),
            // not a bare port. Passing the bare port makes the daemon
            // throw "Unable to initialize ZMQ_XPUB socket" and exit
            // from main() before it binds the RPC port, which is
            // exactly why the fetchInfo retry loop at step 4 hung
            // for 6 seconds with "unknown exception" on the first
            // test runs.
            QStringLiteral("--zmq-pub"),
            QStringLiteral("tcp://%1:%2")
                .arg(cachedHost_).arg(cachedZmqPubPort_),
        };
    } else {
        spec.arguments = daemonArgumentsOverride_;
    }
    process_->setDaemonSpec(spec);

    if (!process_->startDaemon()) {
        lastError_ = QStringLiteral(
            "ProcessManager: failed to start judecoind (port %1)")
            .arg(cachedDaemonPort_);
        return false;
    }

    // Remember the port we picked so the next boot tries it first.
    portDetector_->rememberSuccessfulPort(
        ServiceKind::Daemon,
        port);
    return true;
}

// ---------------------------------------------------------------------------
// Step 3: start wallet-rpc (follows daemon)
// ---------------------------------------------------------------------------

bool StartupManager::stepStartWalletRpc()
{
    if (process_ == nullptr) {
        lastError_ = QStringLiteral("StartupManager: missing process dependency");
        return false;
    }

    // FIX-WR3 (2026-07-19): reap stale wallet-rpc from a previous
    // UI session BEFORE picking a port. Root cause of manual-test
    // bug "close wallet and reopen -> wallet already open": st-143i spawns
    // wallet-rpc through `bash -c "... &"` (QProcess only tracks the
    // bash forker) and st-143L drops the QProcess wrapper after the
    // port probe, so the pre-FIX-WR1 ProcessManager::stopWalletRpc()
    // no-op'd on app exit. The real judecoin-wallet-rpc survived the
    // UI, kept the wallet open and held the wallet2 keys-file lock;
    // the NEXT launch picked a fresh port (19072, because the orphan
    // still owned 19071), spawned a second wallet-rpc, and its
    // open_wallet failed with "wallet already open / opened by another
    // wallet program". Reaping here (a) releases the keys-file lock so
    // open_wallet works again and (b) frees 19071 so the detector's
    // port choice stops drifting upward across relaunches. Paired with
    // FIX-WR1 (stopWalletRpc now kills the real PID on exit) this
    // covers both the clean-exit and the crash/legacy-session paths.
    // Gated on the production-args path: test harnesses that inject
    // walletRpcArgumentsOverride_ manage their own fixture processes.
    if (walletRpcArgumentsOverride_.isEmpty()) {
        const int reaped = process_->reapStaleWalletRpc(
            cachedWalletRpcHost_,  // st-172: wallet-rpc is local-only
            PortConflictDetector::kWalletRpcRangeStart,
            PortConflictDetector::kWalletRpcRangeEnd);
        if (reaped > 0) {
            qInfo().noquote()
                << "StartupManager: FIX-WR3 reaped" << reaped
                << "stale judecoin-wallet-rpc process(es) from a previous session";
        }
    }

    // Pick a wallet-rpc port too. Detector returns 0 on no-port-found.
    const quint16 wport = portDetector_->detectAvailablePort(
        ServiceKind::WalletRpc);
    if (wport == 0) {
        lastError_ = QStringLiteral(
            "PortConflictDetector: no free port in wallet-rpc range (19071-19080)");
        return false;
    }
    cachedWalletRpcPort_ = static_cast<int>(wport);

    ProcessSpec spec;
    spec.kind    = ProcessSpec::Kind::WalletRpc;
    spec.name    = QStringLiteral("judecoin-wallet-rpc");
    spec.program = walletRpcProgramPath_;
    if (walletRpcArgumentsOverride_.isEmpty()) {
        // V5.4.x: same split as daemon -- "--rpc-bind HOST:PORT" is
        // ambiguous on the wallet-rpc side too; use the explicit
        // -ip / -port pair.
        spec.arguments = {
            QStringLiteral("--daemon-address"),
            // st-023/st-172: cachedHost_:cachedDaemonPort_ correctly
            // points at the Custom/preset REMOTE daemon when active -
            // this is the ONLY wallet-rpc argument that may be remote.
            QStringLiteral("%1:%2").arg(cachedHost_).arg(cachedDaemonPort_),
            QStringLiteral("--rpc-bind-ip"),
            // st-172: ALWAYS the local host - wallet-rpc runs locally
            // even when the daemon is remote. Binding the remote IP
            // here made the spawn fail in custom-remote mode.
            cachedWalletRpcHost_,
            QStringLiteral("--rpc-bind-port"),
            QString::number(cachedWalletRpcPort_),
            QStringLiteral("--log-level"),
            cachedLogLevel_,
            // FIX-G5 (2026-07-17 18:55): v3.2.0 wallet-rpc
            // ("Chamaeleo dilepis") REQUIRES --wallet-dir (or
            // --wallet-file / --generate-from-json) — without one of
            // these it exits immediately with:
            //   E Wallet initialization failed: Must specify
            //     --wallet-file or --generate-from-json or --wallet-dir
            // Discovered during manual test of fix-g4 AppImage
            // (DISPLAY=:0 launch on judecoin WSL). We pass
            // --wallet-dir = cachedDataDir_ + "/wallets" which is the
            // same parent dir the daemon uses (--data-dir cachedDataDir_).
            // wallet-rpc will auto-create the /wallets/ subdir on first
            // use. If we later switch to --generate-from-json or a
            // fixed --wallet-file, this changes.
            QStringLiteral("--wallet-dir"),
            cachedDataDir_ + QStringLiteral("/wallets"),
            // FIX-G6 (2026-07-17 20:23): --disable-rpc-login IS
            // supported by v3.2.0 wallet-rpc (verified via `--help` — the
            // flag is listed as "Disable HTTP authentication for RPC
            // connections"). FIX-G4 incorrectly claimed the flag was not
            // recognized (likely confused with an earlier build). Without
            // this flag, wallet-rpc generates random credentials on
            // startup, which causes HttpRpcClient to receive HTTP 401 →
            // transport error -32005 → create_wallet/open_wallet fail
            // after 3 retries. The daemon side (judecoind) also uses
            // --disable-rpc-login; wallet-rpc must match.
            QStringLiteral("--disable-rpc-login"),
        };
    } else {
        spec.arguments = walletRpcArgumentsOverride_;
    }
    process_->setWalletRpcSpec(spec);

    // FIX-G3 (2026-07-17 18:01): setWalletRpcProbeEndpoint MUST be
    // called BEFORE startWalletRpc. The QTcpSocket port poll inside
    // doStartWalletRpcOnce() reads walletRpcProbeEndpoint_ to pick host
    // + port. If we set the endpoint AFTER startWalletRpc (as we did
    // before this fix), the poll runs against an empty string ->
    // QUrl::port() returns -1 -> cast to quint16 = 65535 ->
    // "wallet-rpc RPC port 65535 not listening after 30000 ms".
    // Found during manual test of fix-g1 AppImage (DISPLAY=:0 launch
    // on judecoin WSL). ProcessManager.cpp adds a defensive fallback
    // that also handles empty endpoint, but the root cause is this
    // ordering bug.
    process_->setWalletRpcProbeEndpoint(
        QStringLiteral("http://%1:%2/health")
            .arg(cachedWalletRpcHost_)  // st-172: probe the LOCAL rpc
            .arg(cachedWalletRpcPort_));

    if (!process_->startWalletRpc()) {
        lastError_ = QStringLiteral(
            "ProcessManager: failed to start judecoin-wallet-rpc (port %1)")
            .arg(cachedWalletRpcPort_);
        return false;
    }

    portDetector_->rememberSuccessfulPort(
        ServiceKind::WalletRpc,
        wport);

    // FIX-G7 (2026-07-17 20:43): update WalletManager's
    // WalletRpcClient endpoint to the negotiated port. Without this,
    // the WalletRpcClient stays on the hardcoded 19071 default while
    // the wallet-rpc process is on port 19072 → timeout (-32001).
    if (walletManager_) {
        QUrl ep;
        ep.setScheme(QStringLiteral("http"));
        ep.setHost(cachedWalletRpcHost_);  // st-172: wallet-rpc is local
        ep.setPort(static_cast<quint16>(cachedWalletRpcPort_));
        ep.setPath(QStringLiteral("/json_rpc"));
        walletManager_->rpc()->setEndpoint(ep);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Step 4: daemon.get_info + VersionChecker
// ---------------------------------------------------------------------------

bool StartupManager::stepVerifyVersion()
{
    if (daemon_ == nullptr) {
        lastError_ = QStringLiteral("StartupManager: missing daemon dependency");
        return false;
    }
    if (settings_ == nullptr) {
        lastError_ = QStringLiteral("StartupManager: missing settings dependency");
        return false;
    }

    DaemonInfo info;
    // Set the RPC endpoint to the actual port the daemon is bound to.
    // DaemonClient's default ctor hardcodes http://127.0.0.1:19061/json_rpc,
    // but PortConflictDetector may have picked a different port (e.g. 19064
    // if 19061 was in TIME_WAIT from a previous AppImage) -- PortConflictDetector
    // stores the picked port in cachedDaemonPort_, but the DaemonClient has
    // no way to know about it. Without this setEndpoint() call, fetchInfo()
    // hits 127.0.0.1:19061 which is empty, the 30-attempt retry loop times
    // out for 6 s, and StartupManager::start() returns false at step 4 with
    // "DaemonClient::fetchInfo failed after 30 attempts: unknown exception".
    {
        QUrl ep;
        ep.setScheme(QStringLiteral("http"));
        // st-172 (2026-07-19): use cachedHost_, NOT a hardcoded
        // 127.0.0.1. In local mode cachedHost_ IS the local host
        // (settings default 127.0.0.1, validated non-empty in
        // stepReadSettings), so behaviour is unchanged. In Custom
        // remote mode cachedHost_ holds the REMOTE daemon host
        // (st-023 override) - the hardcoded literal made fetchInfo
        // poll a daemon-less localhost for 30 attempts and fail
        // step 4, so remote mode could never finish startup.
        ep.setHost(cachedHost_);
        ep.setPort(static_cast<quint16>(cachedDaemonPort_));
        ep.setPath(QStringLiteral("/json_rpc"));
        daemon_->setEndpoint(ep);
    }

    // Retry loop: judecoind v3.2.1 launched with --detach takes 1-3
    // seconds after process_->startDaemon() returns to finish
    // initialising core RPC + binding on the RPC port. The old code
    // called fetchInfo() once immediately and hit
    // "connection refused" wrapped in a non-std exception that the
    // catch(...) branch swallowed as "unknown exception". Poll
    // get_info with a short backoff for up to ~6 seconds before
    // declaring the daemon unreachable.
    //
    // st-133e: do NOT wait for synchronized=true here.
    // The UI enters Ready state immediately after this step so the
    // user can navigate (Send / Receive / Subaddress) while the
    // daemon is still syncing; live sync progress is rendered in the
    // Dashboard TopBar ProgressBar (st-133d) and wallet actions are
    // gated by Dashboard.qml::_requireSynced() which shows a Toast
    // when info.synchronized === false. Single fetchInfo() + version
    // check is intentional -- any "wait for synchronized" loop would
    // freeze the splash screen for minutes on a fresh chain.
    constexpr int  kMaxAttempts  = 30;
    constexpr auto kRetryDelay   = std::chrono::milliseconds(200);
    bool           infoFetched   = false;
    QString        lastFetchErr;
    for (int attempt = 1; attempt <= kMaxAttempts && !infoFetched; ++attempt) {
        try {
            info       = daemon_->fetchInfo();
            infoFetched = true;
        } catch (const std::exception& e) {
            lastFetchErr = QString::fromUtf8(e.what());
        } catch (...) {
            lastFetchErr = QStringLiteral("unknown exception");
        }
        if (!infoFetched && attempt < kMaxAttempts) {
            std::this_thread::sleep_for(kRetryDelay);
        }
    }
    if (!infoFetched) {
        lastError_ = QStringLiteral(
            "DaemonClient::fetchInfo failed after %1 attempts: %2")
            .arg(kMaxAttempts).arg(lastFetchErr);
        return false;
    }

    if (!info.isValid()) {
        lastError_ = QStringLiteral(
            "DaemonClient::fetchInfo returned invalid info (no version, no nettype)");
        return false;
    }

    // Prefer version_string (the free-form human-readable form the
    // binary prints), fall back to the integer version + a synthesised
    // string.
    //
    // The integer version from get_info is a packed 3-byte (major,
    // minor, patch) value in the low 24 bits -- e.g. v3.2.1 comes
    // back as 197121 (= 3*65536 + 2*256 + 1 = 0x030201). Just
    // interpolating that as "v197121" trips the VersionChecker
    // regex; decode the bytes to "vMAJOR.MINOR.PATCH" first.
    QString versionString;
    if (!info.version_string.isEmpty()) {
        versionString = info.version_string;
    } else {
        const int v = info.version;
        const int major = (v >> 16) & 0xFF;
        const int minor = (v >>  8) & 0xFF;
        const int patch =  v        & 0xFF;
        versionString = QStringLiteral("v%1.%2.%3")
            .arg(major).arg(minor).arg(patch);
    }

    observedVersionString_ = versionString;
    // st-143j (2026-07-17 16:07): daemon get_info sometimes appends
    // network status to version_string (e.g. "v3.2.0; Height: 283200").
    // Strip everything after the first ';' before parsing.
    {
        const int semi = versionString.indexOf(QLatin1Char(';'));
        if (semi >= 0) {
            versionString = versionString.left(semi).trimmed();
        }
    }
    const auto parsed = VersionChecker::parse(versionString);
    if (!parsed.valid) {
        lastError_ = QStringLiteral(
            "VersionChecker: could not parse daemon version string '%1'")
            .arg(versionString);
        return false;
    }
    if (!parsed.isSupported()) {
        lastError_ = QStringLiteral(
            "VersionChecker: daemon version '%1' is below the "
            "minimum supported version (v3.2.0)")
            .arg(parsed.toVersionString());
        return false;
    }
    observedCanonicalVersion_ = parsed.toVersionString();

    // Publish the canonical version to SettingsManager so the About
    // page can render "Daemon: v3.2.0" without re-querying.
    settings_->setAboutDaemonVersion(observedCanonicalVersion_);

    // FIX-G8 (2026-07-17 21:00): start the DaemonClient
    // heartbeat NOW that we know the daemon is responding AND on
    // the negotiated port (cachedDaemonPort_, set above via
    // daemon_->setEndpoint(ep)). Without this, DaemonClient's 5 s
    // QTimer never starts and the `infoUpdated(info)` signal never
    // fires even though Dashboard.qml wired up `daemonClientObj`
    // (see companion change in src/main.cpp). Stop is unnecessary
    // for the app lifetime — the heartbeat matches the daemon
    // lifecycle (ProcessManager restarts crash the wallet-rpc too).
    if (daemon_ != nullptr) {
        daemon_->startHeartbeat();
    }

    return true;
}

// ---------------------------------------------------------------------------
// Step 5: load v<version>.json via CommandSetLoader
// ---------------------------------------------------------------------------

bool StartupManager::stepLoadCommandSet()
{
    const auto result = CommandSetLoader::load(observedCanonicalVersion_);
    if (!result.ok) {
        lastError_ = QStringLiteral(
            "CommandSetLoader: load failed (kind=%1, message=%2)")
            .arg(result.error_kind)
            .arg(result.error_message);
        return false;
    }
    commandSet_ = result.set;
    // Fallback (newer daemon than we have a JSON for) is NOT a
    // failure - we already warned via the error_message. UI layer
    // sees fallback_used == true on the returned CommandSet and
    // can decide to show a soft warning.
    return true;
}

} // namespace judecoin_wallet::core::startup
