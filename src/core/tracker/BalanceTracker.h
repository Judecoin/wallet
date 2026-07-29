// =============================================================================
// Judecoin Wallet - BalanceTracker (st-162)
//
// Polls wallet-rpc's get_accounts every 30s, sums balance + unlocked_balance
// across all accounts, and exposes totalBalance / unlockedBalance as QML
// Q_PROPERTY entries so the Dashboard hero card renders live wallet balances.
//
// V5.4.7: never reads .keys files; only calls wallet-rpc.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_TRACKER_BALANCE_TRACKER_H
#define JUDECOIN_WALLET_CORE_TRACKER_BALANCE_TRACKER_H

#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QTimer>

#include <chrono>

namespace judecoin_wallet::core::rpc {
class WalletRpcClient;
} // namespace judecoin_wallet::core::rpc

namespace judecoin_wallet::core::tracker {

class BalanceTracker : public QObject
{
    Q_OBJECT

public:
    static constexpr std::chrono::seconds kPollInterval{30};

    // Q_PROPERTY entries for QML bindings.
    // totalBalance   — sum of all accounts' balance (atomic units, quint64 internally).
    // unlockedBalance — sum of all accounts' unlocked_balance (atomic units, quint64 internally).
    // QML boundary keeps double for compatibility (JDE supply << 2^53, see P0-07-fix doc).
    Q_PROPERTY(double totalBalance READ totalBalance NOTIFY balanceChanged)
    Q_PROPERTY(double unlockedBalance READ unlockedBalance NOTIFY balanceChanged)

    explicit BalanceTracker(QObject* parent = nullptr);
    ~BalanceTracker() override;

    double totalBalance() const    { return static_cast<double>(totalBalance_); }
    double unlockedBalance() const { return static_cast<double>(unlockedBalance_); }

    void setWalletRpcClient(QSharedPointer<judecoin_wallet::core::rpc::WalletRpcClient> client);
    void startTracking();
    void stopTracking();

signals:
    void balanceChanged();

private slots:
    void onPollTick();

private:
    void refresh();

    QTimer pollTimer_;
    QSharedPointer<judecoin_wallet::core::rpc::WalletRpcClient> wallet_;
    quint64 totalBalance_    = 0;
    quint64 unlockedBalance_ = 0;
    std::chrono::milliseconds pollIntervalMs_{kPollInterval};

    Q_DISABLE_COPY(BalanceTracker)
};

} // namespace judecoin_wallet::core::tracker

#endif // JUDECOIN_WALLET_CORE_TRACKER_BALANCE_TRACKER_H
