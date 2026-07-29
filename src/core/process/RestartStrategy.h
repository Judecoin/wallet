// =============================================================================
// Judecoin Wallet - RestartStrategy (st-006)
//
// Given an ErrorClassifier::Category, decides whether to retry and how long
// to wait before the next attempt. The strategy is pure: it does not touch
// any QProcess. ProcessManager calls decideNextAttempt() after each failure
// and uses the returned RestartDecision to drive QTimer::singleShot delays.
//
// Policy (V5.4.4 #2 D decision)
// -------------------------
//   Permanent       -> do not retry (attempts remaining = 0)
//   RetryOnce       -> 1 retry after 1s
//   VersionMismatch -> 2 retries, 2s then 4s (server is upgrading; wait)
//   Transient       -> 3 retries, 1s then 2s then 4s (exponential)
//   Unknown         -> 3 retries, 1s then 2s then 4s (safe default)
//
// After attemptsRemaining hits zero, the process is declared "abandoned".
// ProcessManager emits restartExhausted(category, lastReason) and the UI
// must surface a fatal dialog.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_PROCESS_RESTART_STRATEGY_H
#define JUDECOIN_WALLET_CORE_PROCESS_RESTART_STRATEGY_H

#include "core/process/ErrorClassifier.h"

#include <QString>

#include <chrono>

namespace judecoin_wallet::core::process {

// One decision returned by RestartStrategy::decideNextAttempt().
//
// `shouldRetry`        - false means we are done; ProcessManager should
//                         emit restartExhausted() instead of scheduling
//                         another start.
// `delay`              - ms to wait before the next attempt. Only
//                         meaningful when shouldRetry is true.
// `attemptIndex`       - 1-based index of the NEXT attempt. Useful for
//                         log lines ("attempt 2 of 3").
// `maxAttempts`        - total budget for this failure category.
struct RestartDecision {
    bool                         shouldRetry = false;
    std::chrono::milliseconds    delay{0};
    int                          attemptIndex = 0;
    int                          maxAttempts = 0;
};

class RestartStrategy
{
public:
    // Spec defaults - exposed as constants so ProcessManager can log them
    // and tests can assert against the same numbers.
    static constexpr int kMaxAttemptsTransient       = 3;
    static constexpr int kMaxAttemptsRetryOnce       = 1;
    static constexpr int kMaxAttemptsVersionMismatch = 2;
    static constexpr int kMaxAttemptsUnknown         = 3;
    static constexpr int kMaxAttemptsPermanent       = 0;

    // Standard backoff ladder (1s, 2s, 4s). The first retry uses 1s.
    // For VersionMismatch the first retry is 2s (server is upgrading;
    // polling faster is wasteful).
    static constexpr std::chrono::milliseconds kBackoffTransient       = std::chrono::milliseconds(1000);
    static constexpr std::chrono::milliseconds kBackoffRetryOnce       = std::chrono::milliseconds(1000);
    static constexpr std::chrono::milliseconds kBackoffVersionMismatch = std::chrono::milliseconds(2000);
    static constexpr std::chrono::milliseconds kBackoffUnknown         = std::chrono::milliseconds(1000);

    // Given the category and the just-completed attempt number (1-based:
    // 1 = the failure that just happened, i.e. one attempt was used),
    // return the decision for what to do next.
    //
    //   category      = the just-classified error category
    //   attemptsDone  = number of attempts that have already been made
    //                   (including the failing one). Must be >= 1.
    //   lastReason    = optional human-readable last-reason; stashed on
    //                   the strategy for diagnostics, not used here.
    static RestartDecision decideNextAttempt(ErrorClassifier::Category category,
                                            int attemptsDone,
                                            const QString& lastReason = {});

    // Convenience: maximum attempts budget for a category. Used by
    // ProcessManager to size its counter and by UI to render progress.
    static int maxAttemptsFor(ErrorClassifier::Category c);

    // Convenience: compute the next backoff for a category and attempt
    // index (1-based). Returns std::chrono::milliseconds::max() if no
    // further retry is allowed.
    static std::chrono::milliseconds backoffFor(ErrorClassifier::Category c,
                                                int attemptIndex);
};

} // namespace judecoin_wallet::core::process

#endif // JUDECOIN_WALLET_CORE_PROCESS_RESTART_STRATEGY_H
