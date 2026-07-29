// =============================================================================
// Judecoin Wallet - LockManager (st-180)
//
// Monitors user inactivity. When the idle period exceeds the configured
// autoLockSeconds, triggers a wallet-lock sequence:
//   1. close wallet via WalletManager::closeWallet() (close_wallet RPC
//      — unloads the wallet from wallet-rpc; wallet-rpc keeps running
//      so the user can re-open the wallet quickly after unlock).
//   2. emit lockTriggered() → main.qml sets walletReady=false →
//      OnboardingWizard (startup screen) is shown
//
// Inactivity detection: event filter on QGuiApplication catches
// mouse/keyboard/touch events; each resets the idle timer.
//
// Dependencies (weak pointers, set by caller before start()):
//   SettingsManager  → reads autoLockSeconds, listens for changes
//   WalletManager    → closeWallet() on lock (close_wallet RPC)
//
// NOTE: close_wallet RPC unloads the wallet but does NOT stop
// wallet-rpc. The process keeps running so unlock is fast.
//
// Thread safety: the event filter + QTimer both run on the thread
// that owns this QObject (must be the GUI thread).
// =============================================================================

#ifndef JUDECOIN_WALLET_LOCK_MANAGER_H
#define JUDECOIN_WALLET_LOCK_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QEvent>
#include <QGuiApplication>

#include "core/settings/SettingsManager.h"
#include "core/wallet/WalletManager.h"

namespace judecoin_wallet {

class LockManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool locked READ isLocked NOTIFY lockedChanged)
    Q_PROPERTY(int autoLockSeconds READ autoLockSeconds NOTIFY autoLockSecondsChanged)

public:
    explicit LockManager(QObject *parent = nullptr);
    ~LockManager() override;

    void setSettingsManager(judecoin_wallet::core::settings::SettingsManager *sm);
    void setWalletManager(judecoin_wallet::core::wallet::WalletManager *wm);

    bool isLocked()         const { return locked_; }
    int  autoLockSeconds()  const { return autoLockSeconds_; }

public Q_SLOTS:
    void start();
    void stop();
    Q_INVOKABLE void resetActivity();
    Q_INVOKABLE void lockNow();

Q_SIGNALS:
    void lockedChanged(bool locked);
    void autoLockSecondsChanged(int seconds);
    void lockTriggered();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onSettingsChanged();
    void onIdleTick();
    void executeLock();

    judecoin_wallet::core::settings::SettingsManager  *settings_  = nullptr;
    judecoin_wallet::core::wallet::WalletManager      *wallet_    = nullptr;

    QTimer   *idleTimer_       = nullptr;
    int       autoLockSeconds_ = 300;
    int       idleSeconds_     = 0;
    bool      locked_          = false;
    bool      monitoring_      = false;

    // Async close_wallet bookkeeping (P0 fix): one in-flight lock attempt
    // at a time; connections are one-shot and disconnected on completion.
    bool                 lockPending_  = false;
    quint64              lockSeq_      = 0;
    QMetaObject::Connection lockRespConn_;
    QMetaObject::Connection lockFailConn_;

    Q_DISABLE_COPY(LockManager)
};

// =============================================================================
// Inline implementation
// =============================================================================

inline LockManager::LockManager(QObject *parent)
    : QObject(parent)
{
    idleTimer_ = new QTimer(this);
    idleTimer_->setInterval(1000);
    idleTimer_->setTimerType(Qt::VeryCoarseTimer);
    connect(idleTimer_, &QTimer::timeout, this, &LockManager::onIdleTick);
}

inline LockManager::~LockManager()
{
    stop();
}

inline void LockManager::setSettingsManager(
    judecoin_wallet::core::settings::SettingsManager *sm)
{
    if (settings_) {
        disconnect(settings_, nullptr, this, nullptr);
    }
    settings_ = sm;
    if (settings_) {
        autoLockSeconds_ = settings_->autoLockSeconds();
        connect(settings_,
                &judecoin_wallet::core::settings::SettingsManager::autoLockSecondsChanged,
                this, &LockManager::onSettingsChanged);
    }
}

inline void LockManager::setWalletManager(
    judecoin_wallet::core::wallet::WalletManager *wm)
{
    wallet_ = wm;
}

inline void LockManager::start()
{
    if (monitoring_) return;
    if (locked_) return;

    monitoring_ = true;
    idleSeconds_ = 0;

    if (settings_) {
        autoLockSeconds_ = settings_->autoLockSeconds();
    }

    QGuiApplication::instance()->installEventFilter(this);

    if (autoLockSeconds_ > 0) {
        idleTimer_->start();
    }
}

inline void LockManager::stop()
{
    monitoring_ = false;
    idleTimer_->stop();
    idleSeconds_ = 0;
    QGuiApplication::instance()->removeEventFilter(this);
}

inline void LockManager::resetActivity()
{
    if (!monitoring_ || locked_) return;
    idleSeconds_ = 0;
}

inline void LockManager::lockNow()
{
    if (locked_) return;
    stop();
    executeLock();
}

inline void LockManager::onSettingsChanged()
{
    if (!settings_) return;
    const int newVal = settings_->autoLockSeconds();
    if (newVal == autoLockSeconds_) return;
    autoLockSeconds_ = newVal;
    emit autoLockSecondsChanged(newVal);

    if (monitoring_) {
        if (newVal <= 0) {
            idleTimer_->stop();
        } else {
            idleSeconds_ = 0;
            if (!idleTimer_->isActive()) {
                idleTimer_->start();
            }
        }
    }
}

inline void LockManager::onIdleTick()
{
    if (!monitoring_ || locked_) return;

    ++idleSeconds_;
    if (autoLockSeconds_ > 0 && idleSeconds_ >= autoLockSeconds_) {
        stop();
        executeLock();
    }
}

inline void LockManager::executeLock()
{
    if (locked_ || lockPending_) return;

    // No wallet manager attached: nothing to close over RPC, lock now.
    if (!wallet_) {
        locked_ = true;
        emit lockedChanged(true);
        emit lockTriggered();
        return;
    }

    // P0 fix: run close_wallet via the async API so the GUI thread is
    // not blocked by a slow wallet-rpc response. locked_ is only set
    // after the RPC actually succeeds (walletClosed signal).
    lockPending_ = true;
    const QString cid = QStringLiteral("lockmanager-%1").arg(++lockSeq_);

    // One-shot success path. walletClosed() fires when the close_wallet
    // RPC has completed (or immediately, if no wallet was open).
    lockRespConn_ = connect(
        wallet_, &judecoin_wallet::core::wallet::WalletManager::walletClosed,
        this, [this]() {
            if (!lockPending_) return;
            lockPending_ = false;
            disconnect(lockRespConn_);
            disconnect(lockFailConn_);
            if (locked_) return;
            locked_ = true;
            emit lockedChanged(true);
            emit lockTriggered();
        });

    // One-shot failure path: RPC did NOT succeed, so locked_ stays
    // false and inactivity monitoring resumes (wallet is still open).
    lockFailConn_ = connect(
        wallet_, &judecoin_wallet::core::wallet::WalletManager::walletFailed,
        this, [this, cid](const QString &rcid, const QString & /*method*/,
                          const judecoin_wallet::core::rpc::RpcException & /*error*/) {
            if (!lockPending_ || rcid != cid) return;
            lockPending_ = false;
            disconnect(lockRespConn_);
            disconnect(lockFailConn_);
            qWarning("LockManager: async close_wallet failed; wallet still open, resuming monitoring");
            if (!locked_) start();
        });

    try {
        wallet_->closeWalletAsync(cid);
    } catch (...) {
        // Request could not even be sent: wallet still open. Do NOT set
        // locked_; resume monitoring so the lock retries later.
        lockPending_ = false;
        disconnect(lockRespConn_);
        disconnect(lockFailConn_);
        qWarning("LockManager: failed to send async close_wallet; wallet still open, resuming monitoring");
        if (!locked_) start();
    }
}

inline bool LockManager::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        resetActivity();
        break;
    default:
        break;
    }
    return QObject::eventFilter(obj, event);
}

} // namespace judecoin_wallet

#endif // JUDECOIN_WALLET_LOCK_MANAGER_H
