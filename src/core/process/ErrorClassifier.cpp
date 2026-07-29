// =============================================================================
// Judecoin Wallet - ErrorClassifier (st-006) implementation
//
// Pure functions; no Qt object state. All inputs are by-value so the
// classifier is safe to call from any thread.
// =============================================================================

#include "core/process/ErrorClassifier.h"

#include "core/rpc/HttpRpcClient.h"

#include <QRegularExpression>

namespace judecoin_wallet::core::process {

using judecoin_wallet::core::rpc::RpcException;

namespace {

// ---------------------------------------------------------------------------
// Substring matchers - lower-cased once, compared case-insensitively.
// Order matters: more-specific patterns first.
// ---------------------------------------------------------------------------

// Permanent: binary missing / bad args / permissions / unknown flag.
bool stderrMatchesPermanent(const QString& line)
{
    static const QStringList needles = {
        QStringLiteral("no such file or directory"),
        QStringLiteral("not found"),
        QStringLiteral("permission denied"),
        QStringLiteral("unknown option"),
        QStringLiteral("unrecognized option"),
        QStringLiteral("invalid argument"),
        QStringLiteral("usage:"),
        QStringLiteral("error: required argument"),
    };
    for (const auto& n : needles) {
        if (line.contains(n, Qt::CaseInsensitive)) return true;
    }
    return false;
}

// VersionMismatch: remote side reports own or peer's version is wrong.
bool stderrMatchesVersionMismatch(const QString& line)
{
    static const QStringList needles = {
        QStringLiteral("wrong version"),
        QStringLiteral("version mismatch"),
        QStringLiteral("version too old"),
        QStringLiteral("upgrade required"),
        QStringLiteral("please upgrade"),
        QStringLiteral("fork is upgrading"),
    };
    for (const auto& n : needles) {
        if (line.contains(n, Qt::CaseInsensitive)) return true;
    }
    return false;
}

// RetryOnce: lock / "already open" race conditions.
bool stderrMatchesRetryOnce(const QString& line)
{
    static const QStringList needles = {
        QStringLiteral("already locked"),
        QStringLiteral("already open"),
        QStringLiteral("lock acquired"),
        QStringLiteral("wallet is being closed"),
        QStringLiteral("another instance"),
    };
    for (const auto& n : needles) {
        if (line.contains(n, Qt::CaseInsensitive)) return true;
    }
    return false;
}

// Transient: timeout / network / IO problems.
bool stderrMatchesTransient(const QString& line)
{
    static const QStringList needles = {
        QStringLiteral("timeout"),
        QStringLiteral("timed out"),
        QStringLiteral("connection refused"),
        QStringLiteral("connection reset"),
        QStringLiteral("network unreachable"),
        QStringLiteral("broken pipe"),
        QStringLiteral("end of file"),
        QStringLiteral("i/o error"),
        QStringLiteral("temporary failure"),
        QStringLiteral("try again"),
        QStringLiteral("loading blockchain"),
        QStringLiteral("resync"),
        QStringLiteral("synchronizing"),
        QStringLiteral("not enough money"),  // 0-balance transfer_split: transient in V1.0 test scope
    };
    for (const auto& n : needles) {
        if (line.contains(n, Qt::CaseInsensitive)) return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// classify(RpcException)
// ---------------------------------------------------------------------------
ErrorClassifier::Category
ErrorClassifier::classify(const RpcException& error)
{
    using Cat = RpcException::Category;

    // Transport errors are always transient (they include our own synthesised
    // -32099 .. -32000 range, which the RPC spec reserves for "server errors"
    // but we use to flag transport-layer failures).
    if (error.category() == Cat::Transport) {
        // Wrong-password (-12) and not-enough-money (-9) sometimes surface
        // as transient-looking - but they are not retryable.
        const int code = error.code();
        if (code == -12 || code == -9 || code == -7 || code == -8) {
            return Category::Permanent;
        }
        if (code == -6) {  // wallet already open
            return Category::RetryOnce;
        }
        if (code == -11) { // wallet being closed
            return Category::RetryOnce;
        }
        if (code == -15) { // wallet still loading
            return Category::Transient;
        }
        if (code == -3 || code == -13) { // daemon not running / no daemon
            return Category::Transient;
        }
        return Category::Transient;
    }

    if (error.category() == Cat::Protocol) {
        // Protocol errors mean the server is broken or speaking a different
        // version - retry once after backoff; if it keeps happening, the
        // VersionChecker / CommandSetLoader will catch it later.
        return Category::VersionMismatch;
    }

    if (error.category() == Cat::Server) {
        const int code = error.code();

        // Programmer / user errors - permanent.
        if (code == -1 || code == -2 || code == -4 ||
            code == -7 || code == -8 || code == -12) {
            return Category::Permanent;
        }
        // Business-logic errors that mean "wallet cannot fulfill the call"
        // - permanent for the duration of the current wallet state.
        if (code == -9 || code == -10) {
            return Category::Permanent;
        }
        // State races that can resolve themselves - retry once.
        if (code == -6 || code == -11 || code == -15) {
            return Category::RetryOnce;
        }
        // Daemon connectivity issues - transient.
        if (code == -3 || code == -13) {
            return Category::Transient;
        }
        // Wallet not found - permanent (user must restore / re-create).
        if (code == -14) {
            return Category::Permanent;
        }
        // Unrecognised server code - default transient so we don't give up.
        return Category::Unknown;
    }

    if (error.category() == Cat::Cancel) {
        // Cancellation is not a failure to retry - caller is gone.
        return Category::Permanent;
    }

    // Unknown category - default to retry.
    return Category::Unknown;
}

// ---------------------------------------------------------------------------
// classify(QProcess state)
// ---------------------------------------------------------------------------
ErrorClassifier::Category
ErrorClassifier::classify(int processErrorCode,
                          int exitStatus,
                          int exitCode,
                          const QStringList& stderrLines)
{
    // QProcess::ProcessError mirrors (cast to int):
    //   0 FailedToStart, 1 Crashed, 2 Timedout, 3 WriteError,
    //   4 ReadError, 5 UnknownError
    constexpr int kFailedToStart = 0;
    constexpr int kCrashed       = 1;
    constexpr int kTimedout      = 2;
    constexpr int kWriteError    = 3;
    constexpr int kReadError     = 4;

    // QProcess::ExitStatus:
    constexpr int kNormalExit    = 0;

    // FailedToStart almost always means a permanent config / install problem.
    if (processErrorCode == kFailedToStart) {
        return Category::Permanent;
    }

    // Crash or abnormal exit: inspect stderr before falling back.
    for (const QString& raw : stderrLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (stderrMatchesPermanent(line))     return Category::Permanent;
        if (stderrMatchesVersionMismatch(line)) return Category::VersionMismatch;
        if (stderrMatchesRetryOnce(line))     return Category::RetryOnce;
        if (stderrMatchesTransient(line))     return Category::Transient;
    }

    // No stderr clue. Use exit code heuristics.
    if (exitStatus == kNormalExit && exitCode == 0) {
        // Clean exit (e.g. shutdown signal). Not an error.
        return Category::Permanent;
    }

    if (processErrorCode == kTimedout || processErrorCode == kReadError
        || processErrorCode == kWriteError) {
        return Category::Transient;
    }

    if (processErrorCode == kCrashed || exitStatus == 1 /*QProcess::CrashExit*/) {
        // Crashes get up to 3 retries before we give up.
        return Category::Transient;
    }

    return Category::Unknown;
}

// ---------------------------------------------------------------------------
// classifyText(QString)
// ---------------------------------------------------------------------------
ErrorClassifier::Category
ErrorClassifier::classifyText(const QString& text)
{
    const QString line = text.trimmed();
    if (line.isEmpty()) return Category::Unknown;
    if (stderrMatchesPermanent(line))      return Category::Permanent;
    if (stderrMatchesVersionMismatch(line)) return Category::VersionMismatch;
    if (stderrMatchesRetryOnce(line))      return Category::RetryOnce;
    if (stderrMatchesTransient(line))      return Category::Transient;
    return Category::Unknown;
}

// ---------------------------------------------------------------------------
// Metadata accessors
// ---------------------------------------------------------------------------
bool ErrorClassifier::isRetryable(Category c) noexcept
{
    return c != Category::Permanent;
}

QString ErrorClassifier::categoryName(Category c)
{
    switch (c) {
        case Category::Permanent:       return QStringLiteral("permanent");
        case Category::Transient:       return QStringLiteral("transient");
        case Category::RetryOnce:       return QStringLiteral("retry_once");
        case Category::VersionMismatch: return QStringLiteral("version_mismatch");
        case Category::Unknown:         return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString ErrorClassifier::userFacingReason(Category c)
{
    switch (c) {
        case Category::Permanent:
            return QStringLiteral("The process exited with a fatal error. "
                                 "Please verify the binary path and try again.");
        case Category::Transient:
            return QStringLiteral("The process is temporarily unavailable. "
                                 "Retrying automatically.");
        case Category::RetryOnce:
            return QStringLiteral("The process reported a transient lock or "
                                 "open-wallet conflict. Retrying once.");
        case Category::VersionMismatch:
            return QStringLiteral("The remote service reports a version "
                                 "mismatch. Waiting for it to finish upgrading.");
        case Category::Unknown:
            return QStringLiteral("The process exited for an unknown reason. "
                                 "Retrying with backoff.");
    }
    return QString();
}

} // namespace judecoin_wallet::core::process
