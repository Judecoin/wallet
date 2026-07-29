// =============================================================================
// Judecoin Wallet - ErrorClassifier (st-006)
//
// Maps a process-exit condition (QProcess::ExitStatus, QProcess::errorOccurred,
// stderr text, RPC error code) into one of five categories (V5.4.4 #2 decision D).
//
// The five categories dictate restart policy in RestartStrategy:
//
//   Category::Permanent      - do NOT retry. Fatal error: missing binary,
//                              bad arguments, permission denied. UI must
//                              surface a modal dialog explaining the cause
//                              and stop.
//   Category::Transient      - retry up to 3 times with 1s/2s/4s exponential
//                              backoff. RPC timeouts, EOF mid-response,
//                              network unreachable, daemon resyncing.
//   Category::RetryOnce      - retry exactly 1 time after 1s. Lock conflicts
//                              (another instance holds the wallet), "wallet
//                              already open" race after a recent restart.
//   Category::VersionMismatch - retry up to 2 times with 2s/4s backoff.
//                              "Daemon version too old" / "fork is upgrading".
//                              The remote side is racing its own upgrade, so
//                              a short wait usually resolves it.
//   Category::Unknown        - retry up to 3 times with 1s/2s/4s backoff.
//                              Catch-all so we never silently give up.
//
// Classification strategy
// -----------------------
// Three inputs are inspected in order, first match wins:
//
//   1. RpcException code (if the failure surfaced through HttpRpcClient):
//        -1 wrong args, -2 I/O, -3 daemon not running, -4 invalid payment id,
//        -5 no wallet, -6 wallet already open, -7 invalid address, -8 invalid
//        amount, -9 not enough money, -10 too many outputs, -11 wallet closing,
//        -12 wrong password, -13 no daemon connection, -14 wallet not found,
//        -15 wallet loading, plus transport codes (-32099 .. -32000).
//   2. Stderr / process-error message text (substring / regex matches).
//   3. QProcess::ProcessError enum + exit code.
//
// JSON-RPC server-error range -32000..-32099 is reserved for transport-level
// errors we synthesise in HttpRpcClient (timeouts, EOF, refused). Those all
// classify as Transient.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_PROCESS_ERROR_CLASSIFIER_H
#define JUDECOIN_WALLET_CORE_PROCESS_ERROR_CLASSIFIER_H

#include <QString>
#include <QStringList>

#include <QMetaType>

namespace judecoin_wallet::core::rpc {
    class RpcException;
} // namespace judecoin_wallet::core::rpc

namespace judecoin_wallet::core::process {

class ErrorClassifier
{
public:
    // Five categories. Names are stable across the codebase - serialize
    // them through categoryName() if you need to persist or display them.
    enum class Category {
        Permanent,
        Transient,
        RetryOnce,
        VersionMismatch,
        Unknown,
    };

    // Classify an RPC exception (preferred input when the failure came
    // from HttpRpcClient). The Category is purely a function of the
    // exception - no process state is consulted.
    static Category classify(const judecoin_wallet::core::rpc::RpcException& error);

    // Classify a QProcess-style failure. processErrorCode mirrors
    // QProcess::ProcessError (cast to int at the call site):
    //   0 FailedToStart, 1 Crashed, 2 Timedout, 3 WriteError,
    //   4 ReadError, 5 UnknownError
    // exitStatus mirrors QProcess::ExitStatus (0 NormalExit, 1 CrashExit).
    // exitCode is the process's own exit code (-1 if not available).
    // stderrLines is a snapshot of the last few lines from stderr; we
    // scan them for known substrings (case-insensitive).
    static Category classify(int processErrorCode,
                             int exitStatus,
                             int exitCode,
                             const QStringList& stderrLines);

    // Classify raw text (e.g. a one-line stderr message). Used when the
    // caller has not packaged a full QProcess state.
    static Category classifyText(const QString& text);

    // True iff category indicates the process may be retried.
    static bool isRetryable(Category c) noexcept;

    // Human-readable name (stable, lowercase, useful for logs / metrics).
    static QString categoryName(Category c);

    // Convenience: short user-facing reason string. Used by UI banners
    // when restart has been exhausted. Returns a translated-friendly
    // English string - caller wraps in tr() if i18n is desired.
    static QString userFacingReason(Category c);
};

} // namespace judecoin_wallet::core::process

Q_DECLARE_METATYPE(judecoin_wallet::core::process::ErrorClassifier::Category)

#endif // JUDECOIN_WALLET_CORE_PROCESS_ERROR_CLASSIFIER_H
