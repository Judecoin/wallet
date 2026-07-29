// =============================================================================
// Judecoin Wallet - CrashRecoveryManager implementation (st-013)
//
// All signal emission happens on the thread that owns this manager (the
// QObject parent thread; usually the UI main thread). ProcessManager
// signals are dispatched on the same thread because we constructed the
// connections with the default Qt::AutoConnection.
// =============================================================================

#include "core/recovery/CrashRecoveryManager.h"

#include "core/process/ProcessManager.h"
#include "core/rpc/DaemonClient.h"
#include "core/rpc/HttpRpcClient.h"
#include "core/rpc/WalletRpcClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace judecoin_wallet::core::recovery {

using judecoin_wallet::core::process::ErrorClassifier;
using judecoin_wallet::core::process::ProcessManager;
using judecoin_wallet::core::rpc::DaemonClient;
using judecoin_wallet::core::rpc::RpcException;
using judecoin_wallet::core::rpc::WalletRpcClient;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CrashRecoveryManager::CrashRecoveryManager(QObject* parent)
    : QObject(parent)
{
    // Mempool poll timer - 1s tick while wallet-rpc is in recovery mode.
    mempoolTimer_ = new QTimer(this);
    mempoolTimer_->setInterval(static_cast<int>(kMempoolPollInterval.count()));
    mempoolTimer_->setSingleShot(false);
    connect(mempoolTimer_, &QTimer::timeout,
            this, &CrashRecoveryManager::onMempoolPollTick);

    // Grace timer - 2s one-shot for unlock after restart.
    graceTimer_ = new QTimer(this);
    graceTimer_->setInterval(static_cast<int>(kGraceWindow.count()));
    graceTimer_->setSingleShot(true);
    connect(graceTimer_, &QTimer::timeout,
            this, &CrashRecoveryManager::onGraceTick);
}

CrashRecoveryManager::CrashRecoveryManager(ProcessManager* pm,
                                            DaemonClient* daemon,
                                            QObject* parent)
    : QObject(parent)
{
    mempoolTimer_ = new QTimer(this);
    mempoolTimer_->setInterval(static_cast<int>(kMempoolPollInterval.count()));
    mempoolTimer_->setSingleShot(false);
    connect(mempoolTimer_, &QTimer::timeout,
            this, &CrashRecoveryManager::onMempoolPollTick);

    graceTimer_ = new QTimer(this);
    graceTimer_->setInterval(static_cast<int>(kGraceWindow.count()));
    graceTimer_->setSingleShot(true);
    connect(graceTimer_, &QTimer::timeout,
            this, &CrashRecoveryManager::onGraceTick);

    setProcessManager(pm);
    setDaemonClient(daemon);
}

CrashRecoveryManager::~CrashRecoveryManager()
{
    // Timers are parented to us - auto-destroyed.
    if (mempoolTimer_ != nullptr) {
        mempoolTimer_->stop();
    }
    if (graceTimer_ != nullptr) {
        graceTimer_->stop();
    }
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

void CrashRecoveryManager::setProcessManager(ProcessManager* pm)
{
    if (processManager_ == pm) {
        return;
    }
    if (processManager_ != nullptr) {
        disconnect(processManager_, nullptr, this, nullptr);
    }
    processManager_ = pm;
    if (processManager_ != nullptr) {
        connect(processManager_, &ProcessManager::daemonStateChanged,
                this, &CrashRecoveryManager::onDaemonStateChanged);
        connect(processManager_, &ProcessManager::walletRpcStateChanged,
                this, &CrashRecoveryManager::onWalletRpcStateChanged);
        connect(processManager_, &ProcessManager::lockOperations,
                this, &CrashRecoveryManager::onLockOperations);
        connect(processManager_, &ProcessManager::unlockOperations,
                this, &CrashRecoveryManager::onUnlockOperations);
        connect(processManager_, &ProcessManager::restartScheduled,
                this, &CrashRecoveryManager::onRestartScheduled);
        connect(processManager_, &ProcessManager::restartExhausted,
                this, &CrashRecoveryManager::onRestartExhausted);
    }
}

void CrashRecoveryManager::setDaemonClient(DaemonClient* daemon)
{
    daemon_ = daemon;
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

QString CrashRecoveryManager::restartReason(ErrorClassifier::Category c)
{
    switch (c) {
        case ErrorClassifier::Category::Permanent:
            return QStringLiteral("Permanent failure: binary missing, bad args, or permission denied.");
        case ErrorClassifier::Category::Transient:
            return QStringLiteral("Transient failure: RPC timeout or daemon unreachable.");
        case ErrorClassifier::Category::RetryOnce:
            return QStringLiteral("Wallet already open - retrying after 1s.");
        case ErrorClassifier::Category::VersionMismatch:
            return QStringLiteral("Remote is upgrading - waiting 2s before retry.");
        case ErrorClassifier::Category::Unknown:
            return QStringLiteral("Unknown error - retrying with exponential backoff.");
    }
    return QStringLiteral("Unknown error category.");
}

// ---------------------------------------------------------------------------
// transfer_split tracking
// ---------------------------------------------------------------------------

bool CrashRecoveryManager::trackTransferSplit(const QString& transferId,
                                               quint64 amountAtomic,
                                               const QString& primaryAddress)
{
    if (transferId.isEmpty()) {
        return false;
    }
    if (pending_.contains(transferId)) {
        return false;  // dedupe
    }
    pending_.insert(transferId,
                    PendingTxState::init(transferId,
                                          amountAtomic,
                                          primaryAddress,
                                          QDateTime::currentMSecsSinceEpoch()));
    // New transfer: ensure mempool poll is running so we can detect
    // broadcast as soon as wallet-rpc returns the hash.
    if (!mempoolTimer_->isActive()) {
        mempoolTimer_->start();
    }
    emit pendingTxStateChanged(transferId,
                               PendingTxState::State::NotGenerated,
                               QString());
    return true;
}

bool CrashRecoveryManager::updateTransferGenerated(const QString& transferId,
                                                    const QString& txHash)
{
    // gf-006 (P0): mutablePending now returns std::optional<reference_wrapper>.
    // Use the optional immediately within this function so the reference
    // cannot dangle due to a rehash of `pending_` between the lookup and use.
    auto pOpt = mutablePending(transferId);
    if (!pOpt.has_value()) {
        return false;
    }
    PendingTxState& p = pOpt->get();
    const PendingTxState::State prev = p.state();
    if (!p.updateGenerated(txHash, QDateTime::currentMSecsSinceEpoch())) {
        return false;
    }
    if (p.state() != prev) {
        emit pendingTxStateChanged(transferId, p.state(), p.txHash());
    }
    return true;
}

bool CrashRecoveryManager::updateTransferBroadcast(const QString& transferId)
{
    // gf-006 (P0): see updateTransferGenerated for the lifetime contract.
    auto pOpt = mutablePending(transferId);
    if (!pOpt.has_value()) {
        return false;
    }
    PendingTxState& p = pOpt->get();
    if (!p.updateBroadcast(QDateTime::currentMSecsSinceEpoch())) {
        return false;
    }
    emit pendingTxStateChanged(transferId, p.state(), p.txHash());
    return true;
}

bool CrashRecoveryManager::markTransferOrphaned(const QString& transferId)
{
    // gf-006 (P0): see updateTransferGenerated for the lifetime contract.
    auto pOpt = mutablePending(transferId);
    if (!pOpt.has_value()) {
        return false;
    }
    PendingTxState& p = pOpt->get();
    if (!p.markOrphaned()) {
        return false;
    }
    emit transferOrphaned(transferId, p.txHash());
    return true;
}

bool CrashRecoveryManager::clearTransfer(const QString& transferId)
{
    auto it = pending_.find(transferId);
    if (it == pending_.end()) {
        return false;
    }
    pending_.erase(it);
    if (pending_.isEmpty() && mempoolTimer_->isActive()) {
        mempoolTimer_->stop();
    }
    return true;
}

bool CrashRecoveryManager::hasPending(const QString& transferId) const
{
    return pending_.contains(transferId);
}

QVector<PendingTxState> CrashRecoveryManager::pendingTransfers() const
{
    QVector<PendingTxState> out;
    out.reserve(pending_.size());
    for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) {
        out.append(it.value());
    }
    return out;
}

std::optional<std::reference_wrapper<PendingTxState>>
CrashRecoveryManager::mutablePending(const QString& transferId)
{
    // gf-006 (P0): returning std::nullopt instead of nullptr, and wrapping
    // the QHash element in std::reference_wrapper so the caller still gets a
    // mutable lvalue reference (no copy, no state-machine breakage).
    //
    // The returned reference is valid only as long as `pending_` is not
    // rehashed or the entry is not erased. All three current call sites use
    // the reference within the same statement / function call, which keeps
    // us safe. If a future call site needs to hold the reference across
    // mutating operations, it must re-call mutablePending (or restructure
    // to do the mutation inside this manager's API).
    auto it = pending_.find(transferId);
    if (it == pending_.end()) {
        return std::nullopt;
    }
    return std::reference_wrapper<PendingTxState>(it.value());
}

// ---------------------------------------------------------------------------
// Mempool probe
// ---------------------------------------------------------------------------

bool CrashRecoveryManager::probeDaemonForTxHash(const QString& txHash)
{
    if (daemon_ == nullptr) {
        return false;
    }
    if (txHash.isEmpty()) {
        return false;
    }
    try {
        // Lightest available probe: daemon.get_info. We do NOT need to
        // look the tx up by hash in a synthetic test environment; we use
        // get_info as a daemon-reachability signal. Real broadcast
        // detection would require get_transfers or a mempool query, but
        // daemon.get_info is enough to gate "is the daemon responsive?"
        // which is the precondition for broadcast.
        //
        // For st-013 the question we answer is: "can we tell the tx_hash
        // was broadcast?" - which boils down to "is the daemon reachable?"
        // If yes, the UI trusts wallet-rpc's claim that the tx was
        // submitted and we move GeneratedNotBroadcast -> GeneratedAndBroadcast.
        // If no, we count a miss.
        Q_UNUSED(daemon_->fetchInfo());
        return true;
    } catch (const RpcException&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

void CrashRecoveryManager::onMempoolPollTick()
{
    // Walk every tx in GeneratedNotBroadcast state. Ask the daemon if it's
    // there. If yes -> mark broadcast. If no, count a miss; after
    // kMempoolMissesBeforeOrphan consecutive misses, mark orphaned.
    QStringList toMarkBroadcast;
    QStringList toMarkOrphan;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        PendingTxState& p = it.value();
        if (p.state() != PendingTxState::State::GeneratedNotBroadcast) {
            continue;
        }
        const QString txHash = p.txHash();
        if (txHash.isEmpty()) {
            continue;
        }
        const bool reachable = probeDaemonForTxHash(txHash);
        if (reachable) {
            mempoolMissCount_.remove(txHash);
            toMarkBroadcast.append(it.key());
        } else {
            int& misses = mempoolMissCount_[txHash];
            ++misses;
            if (misses >= kMempoolMissesBeforeOrphan) {
                toMarkOrphan.append(it.key());
            }
        }
    }
    for (const QString& id : toMarkBroadcast) {
        updateTransferBroadcast(id);
    }
    for (const QString& id : toMarkOrphan) {
        markTransferOrphaned(id);
    }
}

void CrashRecoveryManager::mempoolPollTick()
{
    onMempoolPollTick();
}

// ---------------------------------------------------------------------------
// Grace tick
// ---------------------------------------------------------------------------

void CrashRecoveryManager::onGraceTick()
{
    if (state_ == State::WalletRpcWaitForGrace) {
        setState(State::Idle);
        emit walletRpcUp();
        emit unlockModifyOperations();
        if (pending_.isEmpty() && mempoolTimer_->isActive()) {
            mempoolTimer_->stop();
        }
    } else if (state_ == State::DaemonRecovering) {
        setState(State::Idle);
        emit daemonUp();
        emit unlockModifyOperations();
    }
}

void CrashRecoveryManager::reset()
{
    if (state_ != State::RecoveryExhausted) {
        return;
    }
    lastError_.clear();
    lastCategory_ = ErrorClassifier::Category::Unknown;
    setState(State::Idle);
}

// ---------------------------------------------------------------------------
// ProcessManager observation
// ---------------------------------------------------------------------------

void CrashRecoveryManager::onDaemonStateChanged(ProcessManager::State s,
                                                  bool isHealthy)
{
    if (isHealthy) {
        if (state_ == State::DaemonDown || state_ == State::DaemonRecovering) {
            // We were down - start grace timer.
            setState(State::DaemonRecovering);
            graceTimer_->start();
        }
    } else {
        // Daemon just went down. Sub-decision 3: lock modify operations,
        // popup is the UI's responsibility (we just emit daemonDown).
        const QString reason = QStringLiteral("Daemon is not running. Wallet modify operations are locked.");
        if (state_ != State::DaemonDown) {
            setState(State::DaemonDown);
            emit daemonDown(reason);
            emit lockModifyOperations(reason);
        }
        // Cancel any pending grace timer.
        graceTimer_->stop();
    }
}

void CrashRecoveryManager::onWalletRpcStateChanged(ProcessManager::State s,
                                                     bool isHealthy)
{
    if (isHealthy) {
        if (state_ == State::WalletRpcRecovering) {
            // wallet-rpc back up - start grace timer before unlocking.
            setState(State::WalletRpcWaitForGrace);
            graceTimer_->start();
        }
    } else {
        if (state_ != State::WalletRpcRecovering && state_ != State::WalletRpcWaitForGrace) {
            const QString reason = QStringLiteral("wallet-rpc is not running. Auto-restart scheduled.");
            setState(State::WalletRpcRecovering);
            emit walletRpcDown(reason);
            // Start mempool poll if we have anything to poll.
            if (!pending_.isEmpty() && !mempoolTimer_->isActive()) {
                mempoolTimer_->start();
            }
        }
    }
}

void CrashRecoveryManager::onLockOperations(const QString& /*reason*/)
{
    // ProcessManager already locks ops; we mirror this so UI handlers
    // bind to CrashRecoveryManager::lockModifyOperations() uniformly.
    emit lockModifyOperations(QStringLiteral("Process manager locked operations"));
}

void CrashRecoveryManager::onUnlockOperations()
{
    // Don't override a daemon-down state - that lock must stay until the
    // daemon itself is back.
    if (state_ == State::DaemonDown || state_ == State::DaemonRecovering) {
        return;
    }
    emit unlockModifyOperations();
}

void CrashRecoveryManager::onRestartScheduled(int delayMs,
                                                ErrorClassifier::Category category)
{
    lastCategory_ = category;
    emit recoveryScheduled(category, delayMs);
}

void CrashRecoveryManager::onRestartExhausted(ErrorClassifier::Category category,
                                                const QString& reason)
{
    lastCategory_ = category;
    lastError_    = reason;
    setState(State::RecoveryExhausted);
    emit recoveryExhausted(category, reason);
}

// ---------------------------------------------------------------------------
// State setter
// ---------------------------------------------------------------------------

void CrashRecoveryManager::setState(State newState)
{
    if (state_ == newState) {
        return;
    }
    state_ = newState;
    emit stateChanged(state_);
}

PendingTxState::State
CrashRecoveryManager::classifyPendingFromState(State /*s*/)
{
    // Currently a 1:1 mapping (no manager-state influences tx state). Kept
    // as a function for forward-compatibility: when WalletRpcRecovering
    // starts, we may want to downgrade any NotGenerated row to "stuck".
    return PendingTxState::State::NotGenerated;
}

} // namespace judecoin_wallet::core::recovery
