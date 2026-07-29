// =============================================================================
// Judecoin Wallet - RestartStrategy (st-006) implementation
//
// Pure functions - no Qt state, no logging, no I/O. Each function returns
// deterministic values based on the category and attempt index.
// =============================================================================

#include "core/process/RestartStrategy.h"

#include <algorithm>

namespace judecoin_wallet::core::process {

namespace {

// Return the maximum attempt count for a category.
int maxForCategory(ErrorClassifier::Category c)
{
    using Cat = ErrorClassifier::Category;
    switch (c) {
        case Cat::Permanent:       return RestartStrategy::kMaxAttemptsPermanent;
        case Cat::RetryOnce:       return RestartStrategy::kMaxAttemptsRetryOnce;
        case Cat::VersionMismatch: return RestartStrategy::kMaxAttemptsVersionMismatch;
        case Cat::Transient:       return RestartStrategy::kMaxAttemptsTransient;
        case Cat::Unknown:         return RestartStrategy::kMaxAttemptsUnknown;
    }
    return 0;
}

} // namespace

int RestartStrategy::maxAttemptsFor(ErrorClassifier::Category c)
{
    return maxForCategory(c);
}

std::chrono::milliseconds
RestartStrategy::backoffFor(ErrorClassifier::Category c, int attemptIndex)
{
    using Cat = ErrorClassifier::Category;
    const int maxAttempts = maxForCategory(c);
    if (maxAttempts == 0 || attemptIndex > maxAttempts) {
        // Out of budget - caller will get shouldRetry == false.
        return std::chrono::milliseconds::max();
    }

    // attemptIndex is the next attempt number (1-based). The backoff
    // schedule is exponential: 1s, 2s, 4s for the standard ladder,
    // or 2s, 4s for VersionMismatch.
    switch (c) {
        case Cat::RetryOnce:
            // Always 1s.
            return kBackoffRetryOnce;

        case Cat::VersionMismatch:
            // attempt 1 -> 2s, attempt 2 -> 4s
            if (attemptIndex == 1) return kBackoffVersionMismatch;
            return kBackoffVersionMismatch * 2;

        case Cat::Transient:
        case Cat::Unknown:
            // attempt 1 -> 1s, attempt 2 -> 2s, attempt 3 -> 4s
            switch (attemptIndex) {
                case 1:  return kBackoffTransient;
                case 2:  return kBackoffTransient * 2;
                case 3:  return kBackoffTransient * 4;
                default: return kBackoffTransient * 8;  // unused (we cap at 3)
            }

        case Cat::Permanent:
            return std::chrono::milliseconds::max();
    }
    return std::chrono::milliseconds::max();
}

RestartDecision
RestartStrategy::decideNextAttempt(ErrorClassifier::Category category,
                                   int attemptsDone,
                                   const QString& /*lastReason*/)
{
    RestartDecision d;
    const int maxAttempts = maxForCategory(category);

    if (maxAttempts == 0 || attemptsDone >= maxAttempts) {
        // Either Permanent, or budget exhausted.
        d.shouldRetry  = false;
        d.delay        = std::chrono::milliseconds::zero();
        d.attemptIndex = attemptsDone;
        d.maxAttempts  = maxAttempts;
        return d;
    }

    const int nextAttempt = attemptsDone + 1;
    d.shouldRetry  = true;
    // attemptsDone is the count of completed attempts (>= 1); it doubles
    // as the 1-based retry index for the backoff ladder: 1st failure
    // -> 1s, 2nd -> 2s, 3rd -> 4s.
    d.delay        = backoffFor(category, attemptsDone);
    d.attemptIndex = nextAttempt;
    d.maxAttempts  = maxAttempts;
    return d;
}

} // namespace judecoin_wallet::core::process
