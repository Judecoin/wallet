// =============================================================================
// Judecoin Wallet - ProcessManager (st-006) implementation
//
// All QProcess / QTimer work happens on the main thread (the thread that
// owns the ProcessManager instance). The QObject parent system takes care
// of moving child QObjects to the correct thread.
// =============================================================================

#include "core/process/ProcessManager.h"

#include "core/process/RestartStrategy.h"
#include "core/network/PortConflictDetector.h"
#include "core/rpc/HttpRpcClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>  // FIX-G4: std::cout for wallet-rpc spawn/shell command log (stdout, not stderr)

#include <signal.h>
#include <sys/types.h>
#include <thread>  // st-133b: std::this_thread::sleep_for in killDetachedDaemon
#include <unistd.h>  // st-133f: usleep between SIGTERM and SIGKILL fallback

namespace judecoin_wallet::core::process {

namespace {
using RpcClient    = judecoin_wallet::core::rpc::HttpRpcClient;
using RpcException = judecoin_wallet::core::rpc::RpcException;
using PortDetector = judecoin_wallet::core::network::PortConflictDetector;

// =============================================================================
// P0 #2 v3 fix (2026-07-23): double-fork child-process modifier.
//
// QProcess fork()s a child, calls this modifier in the child BEFORE execve().
// The modifier fork()s again:
//   - Parent (original QProcess child) exits immediately so QProcess sees the
//     child exit and sets state to NotRunning. This makes st-143L wrapper reset
//     clean — no "Destroyed while process still running" warning, no kill().
//   - Grandchild calls ::setsid() to detach from the UI session, then returns
//     so QProcess continues with execve() in the grandchild context.
//
// This EXACTLY replicates the `bash -c "binary args &"` pattern (P01 st-143i):
//   - bash forks a background child (as we fork a grandchild)
//   - bash exits (as our QProcess child exits)
//   - background child is adopted by init (as our grandchild is)
//   - shell "&" does implicit setsid (as we call setsid() explicitly)
//
// Without the double-fork, QProcess::reset() at st-143L calls ~QProcess()
// which sees the child still running → "QProcess: Destroyed while process
// (\"...\") is still running." → kill() on daemon/wallet-rpc → cascade failure:
// RPC timeout, -32001 transient error, UI freeze. That was the P02 and P02-v2
// (X1) failure mode. X1 only called ::setsid() in the modifier without the
// double-fork, so the QProcess wrapper still tracked a live process at reset
// time and the destructor killed it.
//
// POSIX-only; guarded by #ifdef Q_OS_UNIX. Non-Unix platforms (not a V1.0
// target) skip the modifier entirely — same as P02 5d785ea behaviour.
// Requires: <unistd.h> (fork, setsid, _exit, write), <sys/types.h> (pid_t).
// Both headers are already included in this translation unit.
// =============================================================================
#ifdef Q_OS_UNIX
void doubleForkModifier()
{
    pid_t grandchild = ::fork();
    if (grandchild > 0) {
        // Original QProcess child: exit immediately.
        // QProcess detects this exit → sets processState to NotRunning.
        // st-143L reset is then clean — no warning, no kill.
        ::_exit(0);
    }
    if (grandchild < 0) {
        // fork() failed (ENOMEM / EAGAIN). Continue in the current
        // (QProcess child) process without double-fork protection.
        // The daemon/wallet-rpc will still be spawned, but the QProcess
        // wrapper will track it directly → st-143L reset will trigger
        // the "Destroyed while running" warning and kill the process.
        // This is the degrade-to-P02 path; unlikely in practice.
        static constexpr char kMsg[] =
            "ProcessManager: double-fork failed in child modifier - "
            "daemon/wallet-rpc will run in UI session; expect P0 #2 "
            "symptoms on shutdown.\n";
        (void)::write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
        return;
    }
    // Grandchild: detach from the UI session + terminal.
    // setsid() atomically makes this process a session leader + new
    // process group leader + detaches from any controlling terminal.
    if (::setsid() == static_cast<pid_t>(-1)) {
        static constexpr char kMsg[] =
            "ProcessManager: setsid() failed in grandchild modifier - "
            "daemon/wallet-rpc will still run (no session isolation).\n";
        (void)::write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
    }
    // Return so QProcess continues with execve() in the grandchild.
    // The grandchild inherits the stdin/stdout/stderr redirects set up
    // by setStandardInputFile / setStandardOutputFile / setProcessChannelMode.
}
#endif // Q_OS_UNIX
} // namespace (anonymous)

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ProcessManager::ProcessManager(QObject* parent)
    : QObject(parent)
{
    // Heartbeat timer - non-recurring-but-restarted pattern is fine because
    // QTimer is recurring by default.
    heartbeatTimer_ = new QTimer(this);
    heartbeatTimer_->setInterval(static_cast<int>(heartbeatInterval_.count()));
    heartbeatTimer_->setSingleShot(false);
    connect(heartbeatTimer_, &QTimer::timeout,
            this, &ProcessManager::onHeartbeat);

    // Slot for the pending restart - we own this timer and reuse it.
    pendingRestartTimer_ = new QTimer(this);
    pendingRestartTimer_->setSingleShot(true);
    pendingRestartTimer_->setInterval(0);
    connect(pendingRestartTimer_, &QTimer::timeout,
            this, &ProcessManager::doRestart);

    // st-136 (Bug B + C unified): if heartbeat detects the adopted
    // daemon is no longer the real judecoind, clear the adopt probe
    // so the next startDaemon() falls through to spawn (instead of
    // looping on the same imposter listener), and re-invoke
    // startDaemon() so the new spawn-retry loop (kMaxAttempts=3,
    // exponential backoff 1s/2s/4s) takes over. Two layers cooperate:
    // Bug C heartbeat detects; Bug B retry handles.
    connect(this, &ProcessManager::adoptionRevoked, this,
            [this](const QString& reason) {
                if (daemonState_ != State::Running) {
                    // Not currently adopted (e.g. startDaemon() already
                    // moved state away); nothing to do.
                    return;
                }
                qInfo().noquote()
                    << "ProcessManager: adoptionRevoked (" << reason
                    << ") - clearing adopt probe + spawning our own";
                setDaemonState(State::NotStarted);
                daemonProbeHost_.clear();   // disable future adopt
                daemonProbePort_ = 0;
                startDaemon();               // Bug B retry loop
            });
}

ProcessManager::~ProcessManager()
{
    // Best-effort: kill wallet-rpc before the wrapper goes away.
    if (!walletRpcProcess_.isNull()) {
        if (walletRpcProcess_->state() != QProcess::NotRunning) {
            walletRpcProcess_->terminate();
            if (!walletRpcProcess_->waitForFinished(1000)) {
                walletRpcProcess_->kill();
                walletRpcProcess_->waitForFinished(500);
            }
        }
        walletRpcProcess_.reset();
    }
    // st-143d: daemon QProcess cleanup (same pattern as wallet-rpc).
    if (!daemonProcess_.isNull()) {
        if (daemonProcess_->state() != QProcess::NotRunning) {
            daemonProcess_->terminate();
            if (!daemonProcess_->waitForFinished(1000)) {
                daemonProcess_->kill();
                daemonProcess_->waitForFinished(500);
            }
        }
        daemonProcess_.reset();
    }
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

void ProcessManager::setDaemonSpec(const ProcessSpec& spec)
{
    daemonSpec_ = spec;
}

void ProcessManager::setWalletRpcSpec(const ProcessSpec& spec)
{
    walletRpcSpec_ = spec;
}

void ProcessManager::setWalletRpcProbeEndpoint(const QString& url)
{
    walletRpcProbeEndpoint_ = url;
}

void ProcessManager::setDaemonProbeEndpoint(const QString& host, quint16 port)
{
    daemonProbeHost_ = host;
    daemonProbePort_ = port;
}

void ProcessManager::setHeartbeatInterval(std::chrono::milliseconds t)
{
    heartbeatInterval_ = t;
    if (heartbeatTimer_ != nullptr) {
        heartbeatTimer_->setInterval(static_cast<int>(heartbeatInterval_.count()));
    }
}

quint16 ProcessManager::extractRpcBindPort(const ProcessSpec& spec)
{
    // st-135 (Fix D, A-1): parse "--rpc-bind-port <port>" from
    // spec.arguments so startDaemon() can ping the port the daemon
    // actually binds (StartupManager injects this flag with the
    // PortConflictDetector-selected port, e.g. 19064 when 19061 is in
    // TIME_WAIT). Returns 0 if the flag is absent or unparseable; in
    // V1.0 the flag is always injected (StartupManager.cpp ~line 519
    // constructs it explicitly), so 0 is a defensive fail-closed
    // fallback only.
    const QStringList& a = spec.arguments;
    for (int i = 0; i + 1 < a.size(); ++i) {
        if (a[i] == QLatin1String("--rpc-bind-port")) {
            bool ok = false;
            const quint16 p = a[i + 1].toUShort(&ok);
            if (ok && p != 0) return p;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// st-143d: startDetachedAndCapturePid REMOVED — daemon spawning now uses
// QProcess::start() (inline in doStartOnce) for stderr capture and to
// avoid the FUSE+setsid exec failure with QProcess::startDetached().

bool ProcessManager::startDaemon()
{
    // st-136 (Bug B): outer attempt loop wrapping doStartOnce() with
    // exponential backoff. Permanent -> short-circuit (no retry -
    // binary missing or version mismatch won't fix itself); Transient
    // -> backoff + retry; Success -> return true; NotStarted ->
    // nothing to do (caller passed an idle/invalid state).
    constexpr int kMaxAttempts = 3;
    constexpr std::chrono::milliseconds kBackoffBase{1000};  // 1s, 2s, 4s

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        emit attemptChanged(attempt, kMaxAttempts);
        const StartResult r = doStartOnce();
        if (r == StartResult::Success) {
            return true;
        }
        if (r == StartResult::NotStarted) {
            // Already running or invalid spec - caller had a state
            // machine problem; do not retry.
            return false;
        }
        if (r == StartResult::Permanent) {
            // Binary missing or version mismatch - retrying won't
            // help. Surface exhausted to UI immediately.
            emit attemptChanged(0, kMaxAttempts);
            return false;
        }
        // r == StartResult::Transient: port contention, spawn-time
        // RPC listen timeout, etc. - worth a backoff + retry.
        if (attempt < kMaxAttempts) {
            std::this_thread::sleep_for(
                kBackoffBase * (1 << (attempt - 1)));
        }
    }
    emit attemptChanged(0, kMaxAttempts);
    return false;
}

ProcessManager::StartResult ProcessManager::doStartOnce()
{
    if (daemonState_ == State::Running || daemonState_ == State::Starting) {
        // Already running - refuse to start a second instance.
        return StartResult::NotStarted;
    }

    if (daemonSpec_.kind != ProcessSpec::Kind::Daemon) {
        qWarning("ProcessManager: startDaemon() called with non-daemon spec");
        return StartResult::Permanent;
    }

    // st-113: if a probe endpoint is configured, look for an already-
    // running daemon before spawning our own. Three outcomes:
    //   - ProbeAdopted           : external daemon found + version OK
    //                              -> emit signal, return Success.
    //   - ProbeVersionMismatch   : external daemon found but old
    //                              -> emit versionMismatch, return
    //                              Permanent (UI must prompt user).
    //   - ProbeNoExisting        : port free or listener non-RPC
    //                              -> fall through to spawn.
    if (daemonProbeEnabled()) {
        const ProbeResult pr = probeExistingDaemon();
        if (pr == ProbeResult::ProbeAdopted) {
            return StartResult::Success;
        }
        if (pr == ProbeResult::ProbeVersionMismatch) {
            // versionMismatch was emitted; do NOT spawn.
            return StartResult::Permanent;
        }
        // ProbeNoExisting: continue to spawn path below.
    }

    setDaemonState(State::Starting);

    // st-143d (2026-07-17 14:55): use QProcess::start() instead of
    // QProcess::startDetached(). On FUSE mounts (AppImage),
    // startDetached fork+s+setsid+exec silently fails — the fork
    // succeeds (PID returned) but exec of a FUSE binary fails inside
    // the detached child, so the daemon dies without writing a single
    // log line. QProcess::start() avoids setsid and allows us to
    // capture stderr for diagnostics.
    if (daemonProcess_.isNull()) {
        daemonProcess_.reset(new QProcess());
        daemonProcess_->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_UNIX
        // P0 #2 v3 fix (2026-07-23): install the double-fork
        // child-process modifier that replicates the P01 st-143i
        // `bash -c "... &"` session-detachment semantics without any
        // shell injection. See doubleForkModifier() in the anonymous
        // namespace above for the full rationale.
        daemonProcess_->setChildProcessModifier(&doubleForkModifier);
#endif
        connect(daemonProcess_.data(), &QProcess::errorOccurred,
                this, &ProcessManager::onDaemonError);
        connect(daemonProcess_.data(),
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ProcessManager::onDaemonFinished);
        connect(daemonProcess_.data(), &QProcess::readyReadStandardError,
                this, &ProcessManager::onDaemonReadyReadStderr);
    }

    // st-143e (2026-07-17 15:51 REVERTED): environment cleanup removed.
    // The user confirmed manual spawn from FUSE mount works with the
    // full AppImage environment. Daemon inherits parent env — same as
    // when run manually from a shell inside the AppImage session.

    // st-143f (2026-07-17 15:21): redirect daemon stdout to /dev/null.
    daemonProcess_->setStandardOutputFile(QProcess::nullDevice());

    // st-143h (2026-07-17 15:42): redirect daemon stdin from /dev/null.
    // QProcess::start() connects stdin to a pipe; if the daemon tries
    // to read stdin (even with --non-interactive), it blocks forever
    // waiting for pipe data. In a shell with "&", stdin is /dev/null
    // (read returns EOF immediately). This is the key difference
    // between manual shell spawn and QProcess spawn.
    daemonProcess_->setStandardInputFile(QProcess::nullDevice());

    // st-143g: log the full daemon command for manual repro
    {
        QString cmdLine = daemonSpec_.program;
        for (const QString& arg : daemonSpec_.arguments) {
            cmdLine += QStringLiteral(" ") + arg;
        }
        qInfo().noquote()
            << "st-143g: daemon spawn command:" << cmdLine;
    }

    // P0 #2 v3 fix (2026-07-23): remove the st-143i `bash -c
    // "<cmd> &"` wrapper and replace it with a direct QProcess::start()
    // combined with a double-fork child-process modifier. See
    // doubleForkModifier() in the anonymous namespace above for the
    // full rationale.
    //
    // Summary: the modifier (called by QProcess in the child process
    // BEFORE execve()) fork()s again. The original QProcess child exits
    // immediately; the grandchild calls ::setsid() to detach from the UI
    // session, then QProcess continues with execve() in the grandchild.
    //
    // This gives us BOTH properties of the original bash wrapper:
    //   (a) st-143L reset is clean (QProcess child is dead -> NotRunning)
    //   (b) daemon runs in its own session (setsid in grandchild)
    // WITHOUT any shell injection surface (P0 #2 audit hard constraint).
    {
        // st-143k (2026-07-17 16:08): disconnect onDaemonFinished before
        // starting the daemon. The double-fork modifier causes the
        // QProcess-tracked child to exit immediately (after forking the
        // grandchild), and onDaemonFinished would otherwise fire on that
        // immediate exit and misdiagnose a crash. Crash detection is
        // handled by the 5s heartbeat RPC/port probe (st-143M / Bug C).
        disconnect(daemonProcess_.data(),
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   this, &ProcessManager::onDaemonFinished);

        daemonProcess_->start(daemonSpec_.program, daemonSpec_.arguments);
    }
    if (!daemonProcess_->waitForStarted(5000)) {
        // Drain stderr so the error message below is informative.
        onDaemonReadyReadStderr();
        const QString reason = QStringLiteral(
            "daemon failed to start (exec): %1 (stderr: %2)")
            .arg(daemonSpec_.program)
            .arg(daemonStderrBuffer_.join(' '));
        qWarning().noquote() << "ProcessManager:" << reason;
        lastReason_ = reason;
        attemptsDone_ = 1;
        const ErrorClassifier::Category cat =
            ErrorClassifier::Category::Permanent;
        lastCategory_ = cat;
        emit restartExhausted(cat, reason);
        setDaemonState(State::Crashed);
        return StartResult::Permanent;
    }

    daemonPid_ = daemonProcess_->processId();
    qInfo().noquote()
        << "ProcessManager: daemon QProcess started, pid =" << daemonPid_;

    // st-143M (2026-07-17 16:17): do NOT set daemonState to Running
    // before waitForConnected. The heartbeat fires every 5s and would
    // poll daemonProcess_->state() — the double-fork modifier causes
    // the QProcess-tracked child to exit immediately, so state is
    // NotRunning. Setting Running only after the port is confirmed
    // keeps the heartbeat idle during the probe window.

    // st-135 (Fix D, A-1): pull the actual --rpc-bind-port from
    // daemonSpec_.arguments BEFORE the spawn-time TCP ping.
    boundPort_ = extractRpcBindPort(daemonSpec_);

    const QString probeHost =
        daemonProbeHost_.isEmpty() ? QStringLiteral("127.0.0.1") : daemonProbeHost_;

    // st-143N (2026-07-17 16:27): poll-loop instead of single
    // waitForConnected call. QTcpSocket::waitForConnected returns
    // IMMEDIATELY with "connection refused" when the port is not
    // yet bound (TCP RST on localhost), so the timeout parameter
    // (30000) never actually kicks in — the call fails in <1 ms.
    // Poll every 500 ms for up to 30 s so the daemon has time to
    // initialise and bind.
    constexpr int kMaxWaitMs   = 30000;
    constexpr int kPollMs      = 500;
    bool          portReady    = false;
    int           waitedMs     = 0;
    while (!portReady && waitedMs < kMaxWaitMs) {
        QTcpSocket probe;
        probe.connectToHost(probeHost, boundPort_);
        if (probe.waitForConnected(kPollMs)) {
            portReady = true;
            probe.disconnectFromHost();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        waitedMs += kPollMs;
    }

    if (!portReady) {
        onDaemonReadyReadStderr();  // drain any daemon stderr
        qWarning().noquote()
            << "ProcessManager: daemon QProcess (pid" << daemonPid_
            << ") RPC port" << boundPort_
            << "not listening after 8s, stderr:"
            << daemonStderrBuffer_.join(' ');
        lastReason_ = QStringLiteral(
            "daemon RPC port not listening after 8s (pid %1, stderr: %2)")
            .arg(daemonPid_)
            .arg(daemonStderrBuffer_.join(' '));
        setDaemonState(State::Crashed);
        // st-143d: use QProcess terminate()/kill() instead of raw ::kill
        daemonProcess_->terminate();
        if (!daemonProcess_->waitForFinished(1000)) {
            daemonProcess_->kill();
            daemonProcess_->waitForFinished(500);
        }
        daemonPid_ = 0;
        return StartResult::Transient;
    }

    // st-143M (2026-07-17 16:17): now that the port is confirmed,
    // mark the daemon as Running so the heartbeat can monitor it.
    setDaemonState(State::Running);

    // st-143L (2026-07-17 16:17): reset daemonProcess_ + daemonPid_.
    // The double-fork modifier causes the QProcess-tracked child to
    // exit immediately (after forking the grandchild daemon), so
    // daemonProcess_->state() is NotRunning — the reset is clean
    // (no "Destroyed while running" warning, no kill()). After
    // reset the heartbeat falls through to daemonPid_==0 path which
    // probes via RPC (Bug C fallback).
    daemonProcess_.reset();
    daemonPid_ = 0;

    return StartResult::Success;
}

ProcessManager::ProbeResult ProcessManager::probeExistingDaemon()
{
    const QString host = daemonProbeHost_.isEmpty()
                             ? QStringLiteral("127.0.0.1")
                             : daemonProbeHost_;
    const quint16 port = daemonProbePort_;

    // Step 1: TCP probe - is something already listening on the port?
    // 1 s timeout (a single listen/close cycle on loopback is sub-ms,
    // so this is essentially "fail-open" for permission issues).
    PortDetector detector;  // alias for judecoin_wallet::core::network::PortConflictDetector
    if (!detector.isPortBound(host, port)) {
        return ProbeResult::ProbeNoExisting;  // port is free - fall through to spawn
    }

    // Step 2: RPC probe - is the thing listening actually a judecoind
    // with a recent-enough get_info? Use the existing HttpRpcClient
    // so we don't duplicate TCP/JSON parsing; ~5s timeout is plenty
    // for a local same-machine probe.
    RpcClient::Config cfg;
    cfg.endpoint = QUrl(QStringLiteral("http://%1:%2/json_rpc")
                            .arg(host)
                            .arg(port));
    cfg.timeout = std::chrono::milliseconds(5000);
    cfg.retryCount = 0;
    RpcClient client(cfg);
    QJsonObject result;
    try {
        result = client.call(QStringLiteral("get_info"));
    } catch (const RpcException& ex) {
        // Not an RPC endpoint we can talk to. Either the listener is
        // some other service or the daemon is half-up. Return
        // ProbeNoExisting so the caller falls through to spawn; the
        // spawn itself will then fail with EADDRINUSE and the user will
        // see the explicit error.
        qWarning("ProcessManager: probe RPC failed (%s)",
                 qUtf8Printable(ex.toString()));
        return ProbeResult::ProbeNoExisting;
    } catch (...) {
        return ProbeResult::ProbeNoExisting;
    }

    // Extract version + status. Mirror the parsing DaemonClient does -
    // keep this local because ProcessManager does not depend on
    // DaemonClient to stay symmetric for the future wallet-rpc probe.
    QString versionString;
    const QJsonValue versionVal = result.value(QStringLiteral("version"));
    const QJsonValue vsVal =
        result.value(QStringLiteral("version_string"));
    if (vsVal.isString() && !vsVal.toString().isEmpty()) {
        versionString = vsVal.toString();
    } else if (versionVal.isDouble()) {
        // Heuristic: monero-fork daemons encode
        // `major*1000000 + minor*1000 + patch` as the `version` field.
        // We surface only the major.minor so the UI banner stays short.
        const int versionNum = versionVal.toInt(-1);
        const int major = versionNum / 1000000;
        const int minor = (versionNum / 1000) % 1000;
        const int patch = versionNum % 1000;
        if (major > 0) {
            versionString = QStringLiteral("v%1.%2.%3")
                                .arg(major).arg(minor).arg(patch);
        }
    }

    // Lowest supported version per V5.4.18 spec: v3.2.0.
    // If we could not extract a version at all but the RPC answered,
    // we accept it (the JSON-RPC handshake itself is enough proof of
    // liveness for the adopt path; otherwise the heartbeat timer will
    // detect a stale daemon soon enough).
    const bool versionOk =
        versionString.isEmpty()
        || versionString.startsWith(QStringLiteral("v3.2."))
        || versionString.startsWith(QStringLiteral("v3.3."))
        || versionString.startsWith(QStringLiteral("v4."))
        || versionString.startsWith(QStringLiteral("v5."));
    if (!versionOk) {
        // Version too old. Refuse to spawn our own and let the UI
        // prompt the user. Stay NotStarted so a later restart does not
        // stumble on the same outcome.
        emit versionMismatch(versionString);
        return ProbeResult::ProbeVersionMismatch;
    }

    // Adopt the discovered PID. findPidByPort returns 0 when /proc or
    // lsof cannot resolve it (e.g. shell-only container, no symlinks).
    // PID 0 means "port is bound but we couldn't find the owner" - we
    // still consider the daemon adopted; the heartbeat timer just
    // cannot verify it. UI logs the missing PID and operators check
    // manually.
    daemonPid_ = detector.findPidByPort(host, port);
    if (daemonPid_ <= 0) {
        qWarning("ProcessManager: probe adopted daemon but PID lookup "
                 "returned %lld (port %s:%u). External daemon assumed "
                 "running; heartbeat cannot verify death.",
                 static_cast<long long>(daemonPid_),
                 qUtf8Printable(host),
                 static_cast<unsigned int>(port));
    }
    setDaemonState(State::Running);
    // st-136 (Bug C): capture the version_string at adopt time so
    // the heartbeat RPC-fallback (daemonPid_ == 0 path) can detect
    // version drift on subsequent ticks. Empty when adoption was
    // not used (spawn path).
    adoptedVersionString_ = versionString;
    emit externalDaemonDetected(host, port, versionString);
    return ProbeResult::ProbeAdopted;
}

bool ProcessManager::startWalletRpc()
{
    // FIX-G2 (2026-07-17 17:24): outer attempt loop wrapping
    // doStartWalletRpcOnce() with exponential backoff. Mirrors
    // startDaemon() (st-136 Bug B). Decision: apply the same fix as daemon.
    //
    // Bug-D (st-135) deferred the rewrite because nobody had adopted
    // the wallet-rpc spawn flow at the time. It reverses that
    // decision now that we have a real signal (create_wallet RPC
    // Connection refused -> core dump) showing the gap is blocking
    // manual testing.

    constexpr int kMaxAttempts = 3;
    constexpr std::chrono::milliseconds kBackoffBase{1000};  // 1s, 2s, 4s

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        emit attemptChanged(attempt, kMaxAttempts);
        const StartResult r = doStartWalletRpcOnce();
        if (r == StartResult::Success) {
            return true;
        }
        if (r == StartResult::NotStarted) {
            // Already running or invalid spec - caller had a state
            // machine problem; do not retry.
            return false;
        }
        if (r == StartResult::Permanent) {
            // Binary missing or version mismatch - retrying won't
            // help. Surface exhausted to UI immediately.
            emit attemptChanged(0, kMaxAttempts);
            return false;
        }
        // r == StartResult::Transient: port contention, spawn-time
        // RPC listen timeout, etc. - worth a backoff + retry.
        if (attempt < kMaxAttempts) {
            std::this_thread::sleep_for(
                kBackoffBase * (1 << (attempt - 1)));
        }
    }
    emit attemptChanged(0, kMaxAttempts);
    return false;
}

ProcessManager::StartResult ProcessManager::doStartWalletRpcOnce()
{
    if (walletRpcState_ == State::Running || walletRpcState_ == State::Starting) {
        // Already running - refuse to start a second instance.
        return StartResult::NotStarted;
    }

    if (walletRpcSpec_.kind != ProcessSpec::Kind::WalletRpc) {
        qWarning("ProcessManager: doStartWalletRpcOnce() called with non-wallet-rpc spec");
        return StartResult::Permanent;
    }

    if (walletRpcProcess_.isNull()) {
        // QScopedPointer owns the wrapper; we do NOT pass `this` as parent to
        // avoid dual ownership with Qt's parent-child teardown. Connections
        // below are still safe because both QObjects live on the same thread
        // (the main thread of the ProcessManager owner).
        walletRpcProcess_.reset(new QProcess());
        walletRpcProcess_->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_UNIX
        // P0 #2 v3 fix (2026-07-23): install the double-fork
        // child-process modifier — mirror of the daemon fix above.
        // See doubleForkModifier() in the anonymous namespace for
        // the full rationale.
        walletRpcProcess_->setChildProcessModifier(&doubleForkModifier);
#endif

        connect(walletRpcProcess_.data(), &QProcess::errorOccurred,
                this, &ProcessManager::onWalletRpcError);
        connect(walletRpcProcess_.data(), &QProcess::stateChanged,
                this, &ProcessManager::onWalletRpcStateChanged);
        connect(walletRpcProcess_.data(),
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ProcessManager::onWalletRpcFinished);
        connect(walletRpcProcess_.data(), &QProcess::readyReadStandardError,
                this, &ProcessManager::onWalletRpcReadyReadStderr);
    }

    setWalletRpcState(State::Starting);

    if (walletRpcSpec_.workingDirectory.isEmpty() == false) {
        walletRpcProcess_->setWorkingDirectory(walletRpcSpec_.workingDirectory);
    }
    if (walletRpcSpec_.environment.isEmpty() == false) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString& kv : walletRpcSpec_.environment) {
            const int eq = kv.indexOf('=');
            if (eq > 0) {
                env.insert(kv.left(eq), kv.mid(eq + 1));
            }
        }
        walletRpcProcess_->setProcessEnvironment(env);
    }

    // st-143f (FIX-G2): redirect stdout to /dev/null. QProcess::start()
    // connects stdout to a pipe; if wallet-rpc writes enough output the
    // pipe buffer fills and wallet-rpc blocks on the next write.
    walletRpcProcess_->setStandardOutputFile(QProcess::nullDevice());

    // st-143h (FIX-G2): redirect stdin from /dev/null. QProcess::start()
    // connects stdin to a pipe; if wallet-rpc ever reads stdin (even
    // with --non-interactive), it blocks forever waiting for pipe
    // data. In a shell with "&", stdin is /dev/null (read returns
    // EOF immediately). This was the key difference between manual
    // shell spawn and QProcess spawn for judecoind (fix-e).
    walletRpcProcess_->setStandardInputFile(QProcess::nullDevice());

    // st-143g (FIX-G2): log the full wallet-rpc command for manual repro.
    {
        QString cmdLine = walletRpcSpec_.program;
        for (const QString& arg : walletRpcSpec_.arguments) {
            cmdLine += QStringLiteral(" ") + arg;
        }
        std::cout << "FIX-G2: wallet-rpc spawn command: " << cmdLine.toStdString() << std::endl;
    }

    // P0 #2 v3 fix (2026-07-23): remove the st-143i / FIX-G2
    // `bash -c "<cmd> &"` wrapper and replace it with a direct
    // QProcess::start() combined with the double-fork child-process
    // modifier. Mirror of the daemon fix in doStartOnce() above.
    // See doubleForkModifier() in the anonymous namespace for the
    // full rationale.
    {
        // st-143k (FIX-G2): disconnect onWalletRpcFinished before
        // starting wallet-rpc. The double-fork modifier causes the
        // QProcess-tracked child to exit immediately (after forking
        // the grandchild), and onWalletRpcFinished would otherwise
        // fire on that immediate exit and misdiagnose a crash.
        // Liveness monitoring is handled by the 5s heartbeat
        // (FIX-G1 port ping).
        disconnect(walletRpcProcess_.data(),
                   QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   this, &ProcessManager::onWalletRpcFinished);

        walletRpcProcess_->start(walletRpcSpec_.program,
                                 walletRpcSpec_.arguments);
    }

    if (!walletRpcProcess_->waitForStarted(5000)) {
        // Drain stderr so the error message below is informative.
        onWalletRpcReadyReadStderr();
        const QString reason = QStringLiteral(
            "wallet-rpc failed to start (exec): %1 (stderr: %2)")
            .arg(walletRpcSpec_.program)
            .arg(walletRpcStderrBuffer_.join(' '));
        qWarning().noquote() << "ProcessManager:" << reason;
        lastReason_ = reason;
        attemptsDone_ = 1;
        const ErrorClassifier::Category cat =
            ErrorClassifier::Category::Permanent;
        lastCategory_ = cat;
        emit restartExhausted(cat, reason);
        setWalletRpcState(State::Crashed);
        return StartResult::Permanent;
    }

    // st-143m (FIX-G2): do NOT set walletRpcState to Running before
    // waitForConnected. The heartbeat fires every 5s and would poll
    // walletRpcProcess_->state() -- the double-fork modifier causes
    // the QProcess-tracked child to exit immediately, so state is
    // NotRunning. Setting Running only after the port is confirmed
    // keeps the heartbeat idle during the probe window.

    // st-143n (FIX-G2): poll-loop instead of single waitForConnected
    // call. QTcpSocket::waitForConnected returns IMMEDIATELY with
    // "connection refused" when the port is not yet bound (TCP RST on
    // localhost), so the timeout parameter (30000) never actually
    // kicks in -- the call fails in <1 ms. Poll every 500 ms for up
    // to 30 s so wallet-rpc has time to initialise and bind.
    constexpr int kMaxWaitMs   = 30000;
    constexpr int kPollMs      = 500;
    bool          portReady    = false;
    int           waitedMs     = 0;

    // Parse host:port from walletRpcProbeEndpoint_ (set by
    // StartupManager::stepStartWalletRpc to "http://127.0.0.1:19071/health").
    //
    // FIX-G3 (2026-07-17 18:01): read port as int (not quint16)
    // because QUrl::port() returns -1 for empty/malformed URLs, and
    // casting -1 to quint16 gives 65535 (looks "valid" to the previous
    // `if (probePort == 0)` check but points nowhere -> 30s timeout ->
    // "wallet-rpc RPC port 65535 not listening"). Discovered during manual
    // test of fix-g1 AppImage. Even though StartupManager.cpp now sets
    // the endpoint BEFORE startWalletRpc (the root cause fix), we still
    // add a defensive fallback chain (extract from spec.arguments, then
    // 19071 default) so future regressions don't silently poll the
    // wrong port again.
    QString probeHost;
    quint16 probePort = 0;
    {
        const QUrl url(walletRpcProbeEndpoint_);
        probeHost = url.host();
        int probePortInt = url.port();
        if (probeHost.isEmpty()) probeHost = QStringLiteral("127.0.0.1");
        if (probePortInt <= 0) {
            // Fallback 1: extract --rpc-bind-port from walletRpcSpec_.arguments
            // (StartupManager builds spec.arguments with "--rpc-bind-port 19071"
            // so this should always succeed).
            for (int i = 0; i + 1 < walletRpcSpec_.arguments.size(); ++i) {
                if (walletRpcSpec_.arguments.at(i) == QLatin1String("--rpc-bind-port")) {
                    probePortInt = walletRpcSpec_.arguments.at(i + 1).toInt();
                    break;
                }
            }
            if (probePortInt <= 0) {
                // Fallback 2: WalletRpcClient.h:101 default.
                probePortInt = 19071;
            }
        }
        probePort = static_cast<quint16>(probePortInt);
    }

    while (!portReady && waitedMs < kMaxWaitMs) {
        QTcpSocket probe;
        probe.connectToHost(probeHost, probePort);
        if (probe.waitForConnected(kPollMs)) {
            portReady = true;
            probe.disconnectFromHost();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        waitedMs += kPollMs;
    }

    if (!portReady) {
        onWalletRpcReadyReadStderr();  // drain any wallet-rpc stderr
        qWarning().noquote()
            << "ProcessManager: wallet-rpc RPC port" << probePort
            << "not listening after" << kMaxWaitMs << "ms, stderr:"
            << walletRpcStderrBuffer_.join(' ');
        lastReason_ = QStringLiteral(
            "wallet-rpc RPC port not listening after %1ms (port %2, stderr: %3)")
            .arg(kMaxWaitMs)
            .arg(probePort)
            .arg(walletRpcStderrBuffer_.join(' '));
        setWalletRpcState(State::Crashed);
        // st-143d (FIX-G2): use QProcess terminate()/kill() instead of raw ::kill
        walletRpcProcess_->terminate();
        if (!walletRpcProcess_->waitForFinished(1000)) {
            walletRpcProcess_->kill();
            walletRpcProcess_->waitForFinished(500);
        }
        return StartResult::Transient;
    }

    // FIX-WR1 (2026-07-19): remember the REAL wallet-rpc PID.
    // The double-fork modifier causes the QProcess-tracked child to
    // exit immediately, and st-143L resets the wrapper below — without
    // this PID lookup NOBODY knows the actual PID and stopWalletRpc()
    // cannot terminate wallet-rpc on app exit. The orphan then
    // survives the UI holding the wallet2 keys-file lock, and the
    // NEXT launch's open_wallet fails with "wallet already open /
    // opened by another wallet program" (manual test 2026-07-19
    // "close wallet and reopen"). findPidByPort resolves the listener
    // we just confirmed on probeHost:probePort.
    {
        PortDetector detector;
        walletRpcPid_ = detector.findPidByPort(probeHost, probePort);
        if (walletRpcPid_ > 0) {
            qInfo().noquote()
                << "FIX-WR1: wallet-rpc real PID" << walletRpcPid_
                << "(port" << probePort << ")";
        } else {
            qWarning().noquote()
                << "FIX-WR1: could not resolve wallet-rpc PID on port"
                << probePort
                << "- stopWalletRpc() will fall back to a port lookup at stop time";
        }
    }

    // st-143m (FIX-G2): now that the port is confirmed, mark wallet-rpc
    // as Running so the heartbeat can monitor it.
    setWalletRpcState(State::Running);

    // st-143L (FIX-G2): reset walletRpcProcess_. The double-fork
    // modifier causes the QProcess-tracked child to exit immediately
    // (after forking the grandchild wallet-rpc), so the wrapper state
    // is NotRunning — the reset is clean (no "Destroyed while
    // running" warning, no kill()). After reset, heartbeat falls
    // through to probeWalletRpc() which does QTcpSocket port pings
    // (FIX-G1).
    walletRpcProcess_.reset();

    // First successful start after a restart: clear the failure counter
    // and the lock (after the grace window).
    attemptsDone_ = 0;
    QTimer::singleShot(static_cast<int>(kLockGraceWindow.count()), this, [this]() {
        unlockOps();
    });

    return StartResult::Success;
}

void ProcessManager::stopWalletRpc(std::chrono::milliseconds gracefulDelay)
{
    // FIX-WR1 (2026-07-19): the pre-fix body early-returned when
    // walletRpcProcess_ was null. After the double-fork modifier +
    // st-143L (wrapper reset once the port probe passes) that null
    // wrapper is the NORMAL steady state, so app exit never terminated
    // the real wallet-rpc -> it survived holding the wallet open + the
    // wallet2 keys-file lock -> the next launch's open_wallet failed
    // with "wallet already open / opened by another wallet program".
    // We now stop BOTH layers:
    //   (a) the QProcess wrapper, when it still tracks a live child
    //       (spawn window before the st-143L reset, or test doubles);
    //   (b) the real wallet-rpc by PID - walletRpcPid_ captured at
    //       spawn time, with a stop-time findPidByPort fallback - via
    //       SIGTERM (wallet-rpc stores the wallet and releases the
    //       keys-file lock on SIGTERM), escalating to SIGKILL after
    //       gracefulDelay.

    // (a) QProcess wrapper (immediate child of double-fork / injected test process).
    if (!walletRpcProcess_.isNull()
        && walletRpcProcess_->state() != QProcess::NotRunning) {
        setWalletRpcState(State::Stopping);
        walletRpcProcess_->terminate();
        if (!walletRpcProcess_->waitForFinished(
                static_cast<int>(gracefulDelay.count()))) {
            walletRpcProcess_->kill();
            walletRpcProcess_->waitForFinished(500);
        }
    }

    // (b) the real (orphaned) wallet-rpc process.
    qint64 pid = walletRpcPid_;
    if (pid <= 0 || !pingPid(pid)) {
        // Fallback: resolve the listener on the probe-endpoint port at
        // stop time (covers findPidByPort failing at spawn time).
        const QUrl url(walletRpcProbeEndpoint_);
        const int port = url.port();
        if (port > 0) {
            const QString host = url.host().isEmpty()
                ? QStringLiteral("127.0.0.1") : url.host();
            PortDetector detector;
            pid = detector.findPidByPort(host, static_cast<quint16>(port));
        }
    }
    if (pid > 0 && pid != static_cast<qint64>(::getpid())
        && pingPid(pid) && pidLooksLikeWalletRpc(pid)) {
        setWalletRpcState(State::Stopping);
        qInfo().noquote()
            << "FIX-WR1: stopWalletRpc: SIGTERM real wallet-rpc pid" << pid;
        ::kill(static_cast<pid_t>(pid), SIGTERM);
        std::chrono::milliseconds waited{0};
        const std::chrono::milliseconds step{100};
        while (waited < gracefulDelay && pingPid(pid)) {
            std::this_thread::sleep_for(step);
            waited += step;
        }
        if (pingPid(pid)) {
            qWarning().noquote()
                << "FIX-WR1: stopWalletRpc: wallet-rpc pid" << pid
                << "still alive after" << static_cast<qint64>(gracefulDelay.count())
                << "ms - SIGKILL";
            ::kill(static_cast<pid_t>(pid), SIGKILL);
        }
    }

    walletRpcPid_ = 0;
    setWalletRpcState(State::NotStarted);
}

void ProcessManager::stopAll()
{
    stopWalletRpc();
    // st-143d: daemon is now a regular QProcess (not detached).
    // Keep the old policy: do NOT kill daemon so the next launch
    // can reconnect. The daemon QProcess becomes orphaned on UI
    // exit and continues running (Linux does not auto-kill children).
}

void ProcessManager::killDetachedDaemon(std::chrono::milliseconds gracefulDelay)
{
    // st-133b + st-143d: daemon is now a regular QProcess (not detached).
    // Use QProcess terminate/kill instead of raw ::kill.
    if (daemonProcess_.isNull() || daemonProcess_->state() == QProcess::NotRunning) {
        if (daemonPid_ <= 0) return;
        // Fallback: PID exists but QProcess is gone (adopted daemon path).
        if (!isDaemonProcessAlive()) {
            daemonPid_ = 0;
            setDaemonState(State::NotStarted);
            return;
        }
        ::kill(static_cast<pid_t>(daemonPid_), SIGTERM);
        std::this_thread::sleep_for(gracefulDelay);
        if (isDaemonProcessAlive()) {
            ::kill(static_cast<pid_t>(daemonPid_), SIGKILL);
        }
        daemonPid_ = 0;
        setDaemonState(State::NotStarted);
        return;
    }

    qInfo().noquote()
        << "ProcessManager: killDetachedDaemon: terminate daemon QProcess, pid"
        << daemonProcess_->processId();
    daemonProcess_->terminate();
    if (!daemonProcess_->waitForFinished(
            static_cast<int>(gracefulDelay.count()))) {
        daemonProcess_->kill();
        daemonProcess_->waitForFinished(1000);
    }
    daemonPid_ = 0;
    setDaemonState(State::NotStarted);
}

// ---------------------------------------------------------------------------
// FIX-WR (2026-07-19): stale wallet-rpc reaper + cmdline guard
// ---------------------------------------------------------------------------

bool ProcessManager::pidLooksLikeWalletRpc(qint64 pid) const
{
    if (pid <= 0) return false;
    QFile f(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) {
        // Cannot inspect (procfs missing / permission denied) - be
        // conservative: refuse to treat it as ours so we never signal
        // an unknown PID.
        return false;
    }
    const QByteArray raw = f.readAll();  // NUL-separated argv
    f.close();
    QString cmdline = QString::fromLocal8Bit(raw);
    cmdline.replace(QChar(u'\0'), QChar(u' '));

    if (cmdline.contains(QLatin1String("judecoin-wallet-rpc"))) {
        return true;
    }
    // Custom binary name via JUDECOIN_WALLET_RPC_BIN override: accept
    // the basename of the configured program too.
    const QString configured = QFileInfo(walletRpcSpec_.program).fileName();
    return !configured.isEmpty() && cmdline.contains(configured);
}

int ProcessManager::reapStaleWalletRpc(const QString& host,
                                       quint16 rangeStart,
                                       quint16 rangeEnd)
{
    // FIX-WR3 (2026-07-19): a previous UI session could not kill
    // its wallet-rpc (stopWalletRpc was a no-op after st-143L - see
    // FIX-WR1 note) or crashed before cleanup ran. That orphan still
    // holds (a) a port in our dedicated range and (b) the wallet2
    // keys-file lock, which makes every subsequent open_wallet fail
    // with "wallet already open / opened by another wallet program".
    // Reap every listener in [rangeStart, rangeEnd] whose cmdline
    // looks like judecoin-wallet-rpc: SIGTERM first (graceful wallet
    // store + lock release), SIGKILL survivors after a ~3s grace
    // window. The port range is exclusive to this application
    // (V5.4.2), so nothing else legitimately lives there.
    PortDetector detector;
    QList<qint64> victims;
    for (int p = rangeStart; p <= static_cast<int>(rangeEnd); ++p) {
        const quint16 port = static_cast<quint16>(p);
        if (!detector.isPortBound(host, port)) continue;
        const qint64 pid = detector.findPidByPort(host, port);
        if (pid <= 0) {
            qWarning().noquote()
                << "FIX-WR3: port" << port << "is bound but PID lookup"
                << "failed - cannot reap (open_wallet may still fail"
                << "with 'wallet already open')";
            continue;
        }
        if (pid == static_cast<qint64>(::getpid())) continue;
        if (victims.contains(pid)) continue;
        if (!pidLooksLikeWalletRpc(pid)) {
            qInfo().noquote()
                << "FIX-WR3: port" << port << "held by non-wallet-rpc pid"
                << pid << "- leaving it alone";
            continue;
        }
        qWarning().noquote()
            << "FIX-WR3: reaping stale judecoin-wallet-rpc pid" << pid
            << "on port" << port << "(SIGTERM)";
        ::kill(static_cast<pid_t>(pid), SIGTERM);
        victims.append(pid);
    }
    if (victims.isEmpty()) return 0;

    // Grace window: wait up to 3s for ALL victims to exit, then
    // SIGKILL whatever survived.
    constexpr std::chrono::milliseconds kGrace{3000};
    constexpr std::chrono::milliseconds kStep{100};
    std::chrono::milliseconds waited{0};
    while (waited < kGrace) {
        bool anyAlive = false;
        for (const qint64 pid : victims) {
            if (pingPid(pid)) { anyAlive = true; break; }
        }
        if (!anyAlive) break;
        std::this_thread::sleep_for(kStep);
        waited += kStep;
    }
    for (const qint64 pid : victims) {
        if (pingPid(pid)) {
            qWarning().noquote()
                << "FIX-WR3: stale wallet-rpc pid" << pid
                << "survived SIGTERM grace window - SIGKILL";
            ::kill(static_cast<pid_t>(pid), SIGKILL);
        }
    }
    return static_cast<int>(victims.size());
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

void ProcessManager::setDaemonState(State s)
{
    if (s == daemonState_) return;
    daemonState_ = s;
    const bool healthy = (s == State::Running);
    emit daemonStateChanged(s, healthy);
}

void ProcessManager::setWalletRpcState(State s)
{
    if (s == walletRpcState_) return;
    walletRpcState_ = s;
    const bool healthy = (s == State::Running);
    emit walletRpcStateChanged(s, healthy);
}

bool ProcessManager::isDaemonProcessAlive() const
{
    return pingPid(daemonPid_);
}

QStringList ProcessManager::walletRpcStderrTail() const
{
    return walletRpcStderrBuffer_;
}

void ProcessManager::heartbeatTick()
{
    onHeartbeat();
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

void ProcessManager::onHeartbeat()
{
    // st-143d: daemon is now a regular QProcess (not detached).
    // Check QProcess state instead of pingPid().
    if (!daemonProcess_.isNull()
        && daemonProcess_->state() == QProcess::NotRunning
        && daemonState_ == State::Running) {
        // Daemon QProcess exited unexpectedly.
        onDaemonReadyReadStderr();
        const QString reason = QStringLiteral("daemon process died (pid %1, stderr: %2)")
            .arg(daemonPid_)
            .arg(daemonStderrBuffer_.join(' '));
        lastReason_ = reason;
        attemptsDone_ += 1;
        const ErrorClassifier::Category cat =
            ErrorClassifier::classifyText(reason);
        lastCategory_ = cat;
        setDaemonState(State::Crashed);
        lockOps(reason);
        scheduleRestart(reason, cat, attemptsDone_);
    } else if (daemonPid_ > 0 && daemonState_ == State::Running
               && daemonProcess_.isNull()) {
        // Fallback: daemonPid_ set but QProcess is gone (adopted path).
        if (!pingPid(daemonPid_)) {
            const QString reason = QStringLiteral("daemon process died (pid %1)")
                .arg(daemonPid_);
            lastReason_ = reason;
            attemptsDone_ += 1;
            const ErrorClassifier::Category cat =
                ErrorClassifier::classifyText(reason);
            lastCategory_ = cat;
            setDaemonState(State::Crashed);
            lockOps(reason);
            scheduleRestart(reason, cat, attemptsDone_);
        }
    }

    // st-136 (Bug C): PID==0 case (adopted but findPidByPort failed -
    // container PID namespace / shell-only envs / permissions).
    // PID-based liveness check above is silently skipped for this
    // branch, so without this fallback the imposter listener would
    // stay adopted forever. Use RPC over the adopt-probe endpoint
    // (still configured at this point; cleared only after adoption
    // is revoked) to verify the listener is still responding + the
    // version hasn't drifted. 5s cadence matches the existing
    // heartbeat; no new QTimer / QObject.
    if (daemonPid_ == 0 && daemonState_ == State::Running
        && !daemonProbeHost_.isEmpty() && daemonProbePort_ != 0) {
        using judecoin_wallet::core::network::PortConflictDetector;
        PortConflictDetector detector;
        if (!detector.isPortBound(daemonProbeHost_, daemonProbePort_)) {
            // Port no longer bound - listener went away.
            emit adoptionRevoked(QStringLiteral("port no longer bound"));
        } else {
            // Port still bound; verify RPC identity + version drift.
            RpcClient::Config cfg;
            cfg.endpoint = QUrl(
                QStringLiteral("http://%1:%2/json_rpc")
                    .arg(daemonProbeHost_).arg(daemonProbePort_));
            cfg.timeout = std::chrono::milliseconds(2000);
            cfg.retryCount = 0;
            RpcClient client(cfg);
            try {
                const QJsonObject r =
                    client.call(QStringLiteral("get_info"));
                const QString vs = r.value(
                    QStringLiteral("version_string")).toString();
                if (!adoptedVersionString_.isEmpty() && !vs.isEmpty()
                    && vs != adoptedVersionString_) {
                    // Adopted daemon restarted with a different version.
                    emit adoptionRevoked(
                        QStringLiteral("version drift: %1 -> %2")
                            .arg(adoptedVersionString_, vs));
                }
                // else: silent pass - still listening + same version
            } catch (...) {
                // RPC unreachable - imposter dropped or daemon died.
                emit adoptionRevoked(QStringLiteral("rpc ping failed"));
            }
        }
    }

    // wallet-rpc check via QProcess state. If the QProcess is in the
    // NotRunning state but we believe we should be running, schedule
    // a restart.
    if (!walletRpcProcess_.isNull()
        && walletRpcProcess_->state() == QProcess::NotRunning
        && walletRpcState_ == State::Running) {
        // QProcess reported a finish we somehow missed. Run the same
        // restart path.
        const QString reason = QStringLiteral(
            "wallet-rpc exited unexpectedly (last exit code %1)")
            .arg(walletRpcProcess_->exitCode());
        lastReason_ = reason;
        attemptsDone_ += 1;
        const ErrorClassifier::Category cat = ErrorClassifier::classify(
            static_cast<int>(walletRpcProcess_->error()),
            static_cast<int>(walletRpcProcess_->exitStatus()),
            walletRpcProcess_->exitCode(),
            walletRpcStderrBuffer_);
        lastCategory_ = cat;
        setWalletRpcState(State::Crashed);
        lockOps(reason);
        scheduleRestart(reason, cat, attemptsDone_);
    }

    // Probe wallet-rpc if a probe endpoint was configured. Currently
    // we only do an HTTP HEAD; detailed RPC health comes from
    // DaemonClient.get_info / WalletRpcClient.get_version elsewhere.
    probeWalletRpc();
}

void ProcessManager::probeWalletRpc()
{
    // FIX-G1 (2026-07-17 17:24): replace stub with real
    // QTcpSocket port ping. Was: "Stub: an HTTP HEAD against
    // walletRpcProbeEndpoint_ would go here." Now: parse endpoint URL
    // -> host/port -> QTcpSocket::waitForConnected poll 500ms x 3 = 1.5s.
    // Decision: UI startup and monitoring should check wallet-rpc port.
    //
    // The dedicated WalletRpcClient runs its own RPC-level probes
    // (this method does NOT replace those). We just want to know if
    // the wallet-rpc TCP port is reachable from the UI side, which is
    // a prerequisite for any RPC to succeed.

    if (walletRpcState_ != State::Running) {
        return;
    }
    if (walletRpcProbeEndpoint_.isEmpty()) {
        return;
    }

    // Parse URL to host:port. StartupManager::stepStartWalletRpc sets
    // walletRpcProbeEndpoint_ to "http://127.0.0.1:19071/health"; if a
    // future caller passes an empty host or different scheme, fall back
    // to the defaults rather than silently skipping.
    const QUrl url(walletRpcProbeEndpoint_);
    const QString host = url.host().isEmpty() ? QStringLiteral("127.0.0.1") : url.host();
    quint16 port = url.port();
    if (port == 0) {
        // WalletRpcClient.h:101 default.
        port = 19071;
    }

    // Short poll: 500ms x 3 = 1.5s. heartbeat fires every 5s so we
    // don't want to block long. Three polls gives wallet-rpc a chance
    // to recover from transient stalls without spamming log output.
    constexpr int kPollMs = 500;
    constexpr int kMaxPolls = 3;
    bool portReady = false;
    for (int i = 0; i < kMaxPolls; ++i) {
        QTcpSocket probe;
        probe.connectToHost(host, port);
        if (probe.waitForConnected(kPollMs)) {
            portReady = true;
            probe.disconnectFromHost();
            break;
        }
    }

    if (!portReady) {
        qWarning().noquote()
            << "FIX-G1: wallet-rpc port" << port
            << "unreachable from probeWalletRpc, marking Crashed";
        emit adoptionRevoked(
            QStringLiteral("wallet-rpc port %1 unreachable").arg(port));
        setWalletRpcState(State::Crashed);
        lockOps("wallet-rpc port unreachable");
    }
}

// ---------------------------------------------------------------------------
// QProcess signal handlers
// ---------------------------------------------------------------------------

void ProcessManager::onWalletRpcError(QProcess::ProcessError err)
{
    // QProcess::errorOccurred fires before finished for FailedToStart.
    // For everything else, the finished handler does the heavy lifting.
    if (err == QProcess::FailedToStart) {
        const QString reason = QStringLiteral(
            "wallet-rpc failed to start: %1")
            .arg(walletRpcSpec_.program);
        lastReason_ = reason;
        attemptsDone_ = 1;
        const ErrorClassifier::Category cat =
            ErrorClassifier::Category::Permanent;
        lastCategory_ = cat;
        setWalletRpcState(State::Crashed);
        lockOps(reason);
        emit restartExhausted(cat, reason);
    }
}

void ProcessManager::onWalletRpcStateChanged(QProcess::ProcessState newState)
{
    switch (newState) {
        case QProcess::NotRunning:
            // finished() will follow (if it was running) or this is
            // initial state. Handled elsewhere.
            break;
        case QProcess::Starting:
            setWalletRpcState(State::Starting);
            break;
        case QProcess::Running:
            setWalletRpcState(State::Running);
            break;
    }
}

void ProcessManager::onWalletRpcFinished(int exitCode,
                                         QProcess::ExitStatus exitStatus)
{
    // If we asked for a stop (Stopping state), this is expected.
    if (walletRpcState_ == State::Stopping) {
        setWalletRpcState(State::NotStarted);
        return;
    }

    // Otherwise this is an unexpected exit. Drain stderr first.
    onWalletRpcReadyReadStderr();

    const QString reason = QStringLiteral(
        "wallet-rpc exited (code=%1, status=%2)")
        .arg(exitCode)
        .arg(exitStatus == QProcess::NormalExit ? "normal" : "crash");

    lastReason_ = reason;
    attemptsDone_ += 1;
    const ErrorClassifier::Category cat = ErrorClassifier::classify(
        static_cast<int>(walletRpcProcess_
                            ? walletRpcProcess_->error()
                            : QProcess::UnknownError),
        static_cast<int>(exitStatus),
        exitCode,
        walletRpcStderrBuffer_);
    lastCategory_ = cat;
    setWalletRpcState(State::Crashed);
    lockOps(reason);
    scheduleRestart(reason, cat, attemptsDone_);
}

void ProcessManager::onWalletRpcReadyReadStderr()
{
    if (walletRpcProcess_.isNull()) return;
    const QByteArray data = walletRpcProcess_->readAllStandardError();
    if (data.isEmpty()) return;

    // Split on newlines, push to ring buffer, emit each line.
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& line : lines) {
        if (line.isEmpty()) continue;
        const QString s = QString::fromUtf8(line).trimmed();
        if (s.isEmpty()) continue;
        walletRpcStderrBuffer_.append(s);
        if (walletRpcStderrBuffer_.size() > kMaxStderrLines) {
            walletRpcStderrBuffer_.removeFirst();
        }
        emit walletRpcStderrLine(s);
    }
}

// ---------------------------------------------------------------------------
// st-143d: daemon QProcess signal handlers (same pattern as wallet-rpc)
// ---------------------------------------------------------------------------

void ProcessManager::onDaemonError(QProcess::ProcessError err)
{
    if (err == QProcess::FailedToStart) {
        onDaemonReadyReadStderr();
        const QString reason = QStringLiteral(
            "daemon failed to start: %1 (stderr: %2)")
            .arg(daemonSpec_.program)
            .arg(daemonStderrBuffer_.join(' '));
        qWarning().noquote() << "ProcessManager:" << reason;
        lastReason_ = reason;
        attemptsDone_ = 1;
        const ErrorClassifier::Category cat =
            ErrorClassifier::Category::Permanent;
        lastCategory_ = cat;
        setDaemonState(State::Crashed);
        lockOps(reason);
        emit restartExhausted(cat, reason);
    }
}

void ProcessManager::onDaemonFinished(int exitCode,
                                       QProcess::ExitStatus exitStatus)
{
    // If we asked for a stop (Stopping state), this is expected.
    if (daemonState_ == State::Stopping) {
        setDaemonState(State::NotStarted);
        return;
    }

    onDaemonReadyReadStderr();
    const QString reason = QStringLiteral(
        "daemon exited (code=%1, status=%2, stderr=%3)")
        .arg(exitCode)
        .arg(exitStatus == QProcess::NormalExit ? "normal" : "crash")
        .arg(daemonStderrBuffer_.join(' '));
    qWarning().noquote() << "ProcessManager:" << reason;
    lastReason_ = reason;
    attemptsDone_ += 1;
    const ErrorClassifier::Category cat = ErrorClassifier::classify(
        static_cast<int>(daemonProcess_
                            ? daemonProcess_->error()
                            : QProcess::UnknownError),
        static_cast<int>(exitStatus),
        exitCode,
        daemonStderrBuffer_);
    lastCategory_ = cat;
    setDaemonState(State::Crashed);
    lockOps(reason);
    scheduleRestart(reason, cat, attemptsDone_);
}

void ProcessManager::onDaemonReadyReadStderr()
{
    if (daemonProcess_.isNull()) return;
    const QByteArray data = daemonProcess_->readAllStandardError();
    if (data.isEmpty()) return;

    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& line : lines) {
        if (line.isEmpty()) continue;
        const QString s = QString::fromUtf8(line).trimmed();
        if (s.isEmpty()) continue;
        daemonStderrBuffer_.append(s);
        if (daemonStderrBuffer_.size() > kMaxStderrLines) {
            daemonStderrBuffer_.removeFirst();
        }
    }
}

// ---------------------------------------------------------------------------
// Restart scheduling
// ---------------------------------------------------------------------------

void ProcessManager::scheduleRestart(const QString& reason,
                                     ErrorClassifier::Category category,
                                     int attemptsDone)
{
    const RestartDecision d =
        RestartStrategy::decideNextAttempt(category, attemptsDone, reason);

    if (!d.shouldRetry) {
        // Budget exhausted.
        emit restartExhausted(category, reason);
        // Stay in Crashed state - the UI will prompt the user.
        return;
    }

    emit restartScheduled(static_cast<int>(d.delay.count()), category);

    if (pendingRestartTimer_ != nullptr) {
        pendingRestartTimer_->setInterval(static_cast<int>(d.delay.count()));
        pendingRestartTimer_->start();
    }
}

void ProcessManager::doRestart()
{
    // Decide which child to restart based on lastCategory_ + which state
    // is in Crashed. Currently the spec only calls for restarting the
    // wallet-rpc child process; the detached daemon is the user's
    // responsibility once it has crashed.
    if (walletRpcState_ == State::Crashed) {
        startWalletRpc();
        return;
    }
    if (daemonState_ == State::Crashed) {
        startDaemon();
        return;
    }
    // Nothing to do.
}

// ---------------------------------------------------------------------------
// Lock / unlock
// ---------------------------------------------------------------------------

void ProcessManager::lockOps(const QString& reason)
{
    ++lockCount_;
    if (!operationsLocked_) {
        operationsLocked_ = true;
        emit lockOperations(reason);
    }
}

void ProcessManager::unlockOps()
{
    if (lockCount_ > 0) --lockCount_;
    if (lockCount_ == 0 && operationsLocked_) {
        operationsLocked_ = false;
        emit unlockOperations();
    }
}

// ---------------------------------------------------------------------------
// PID liveness check (kill(pid, 0))
// ---------------------------------------------------------------------------

bool ProcessManager::pingPid(qint64 pid) const
{
    if (pid <= 0) return false;
    // kill(pid, 0) returns 0 if the process exists and we have permission
    // to signal it (or it is a zombie and we own it), -1 with errno=EPERM
    // if it exists but we lack permission (still alive), or -1 with
    // errno=ESRCH if it does not exist.
    const int r = ::kill(static_cast<pid_t>(pid), 0);
    if (r == 0) return true;
    if (errno == EPERM) return true;  // exists but not ours; treat as alive
    return false;
}

} // namespace judecoin_wallet::core::process
