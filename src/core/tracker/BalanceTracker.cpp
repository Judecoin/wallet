// =============================================================================
// Judecoin Wallet - BalanceTracker implementation (st-162)
//
// Polls wallet-rpc's get_accounts every 30s, sums balance + unlocked_balance
// across all accounts, emits balanceChanged() so QML bindings refresh.
// =============================================================================

#include "core/tracker/BalanceTracker.h"
#include "core/rpc/BalanceApi.h"
#include "core/rpc/WalletRpcClient.h"

namespace judecoin_wallet::core::tracker {

BalanceTracker::BalanceTracker(QObject* parent)
    : QObject(parent),
      pollTimer_(this)
{
    pollTimer_.setSingleShot(false);
    pollTimer_.setInterval(
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(kPollInterval).count()));
    connect(&pollTimer_, &QTimer::timeout,
            this, &BalanceTracker::onPollTick);
}

BalanceTracker::~BalanceTracker()
{
    stopTracking();
}

void BalanceTracker::setWalletRpcClient(
    QSharedPointer<judecoin_wallet::core::rpc::WalletRpcClient> client)
{
    wallet_ = std::move(client);
}

void BalanceTracker::startTracking()
{
    if (pollTimer_.isActive()) return;
    pollTimer_.start();
    // st-175 (2026-07-19): delay first poll 8s so daemon + wallet-rpc
    // have time to finish handshake. Before this, the immediate poll fired
    // get_accounts against an unready daemon, producing transient -32002 errors.
    QTimer::singleShot(8000, this, &BalanceTracker::onPollTick);
}

void BalanceTracker::stopTracking()
{
    pollTimer_.stop();
}

void BalanceTracker::onPollTick()
{
    refresh();
}

void BalanceTracker::refresh()
{
    if (!wallet_) return;

    try {
        auto accounts = wallet_->getAccounts();
        quint64 total   = 0;
        quint64 unlocked = 0;
        for (const auto& a : accounts.addresses) {
            total    += static_cast<quint64>(a.balance);
            unlocked += static_cast<quint64>(a.unlocked_balance);
        }
        if (total != totalBalance_ || unlocked != unlockedBalance_) {
            totalBalance_    = total;
            unlockedBalance_ = unlocked;
            emit balanceChanged();
        }
    } catch (...) {
        // Silently retry on next tick — wallet-rpc may not be ready yet.
    }
}

} // namespace judecoin_wallet::core::tracker
