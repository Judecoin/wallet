// =============================================================================
// Judecoin Wallet - TransactionTracker implementation (st-018)
//
// Threading: every signal is emitted on the thread that owns this manager
// (the UI main thread). The poll timer and the early-refresh timer both
// fire in the same thread, so refresh() and its slot handlers execute
// serially on the event loop — no mutex needed.
// =============================================================================

#include "core/tracker/TransactionTracker.h"

#include "core/localdata/LocalDataManager.h"
#include "core/rpc/HttpRpcClient.h"
#include "core/rpc/WalletRpcClient.h"

#include <QDateTime>
#include <QTimer>

#include <utility>

namespace judecoin_wallet::core::tracker {

using judecoin_wallet::core::localdata::LocalDataManager;
using judecoin_wallet::core::rpc::HttpRpcClient;
using judecoin_wallet::core::rpc::RpcException;
using judecoin_wallet::core::rpc::TransferEntry;
using judecoin_wallet::core::rpc::TransferList;
using judecoin_wallet::core::rpc::WalletRpcClient;

// =============================================================================
// TrackedTransfer
// =============================================================================

QString TrackedTransfer::stateName(TrackedTransfer::State s)
{
    switch (s) {
        case TrackedTransfer::State::Pending:    return QStringLiteral("pending");
        case TrackedTransfer::State::Confirming: return QStringLiteral("confirming");
        case TrackedTransfer::State::Confirmed:  return QStringLiteral("confirmed");
    }
    return QStringLiteral("pending");
}

TrackedTransfer::State TrackedTransfer::stateFromName(const QString& name)
{
    if (name == QStringLiteral("confirming")) return TrackedTransfer::State::Confirming;
    if (name == QStringLiteral("confirmed"))  return TrackedTransfer::State::Confirmed;
    return TrackedTransfer::State::Pending;
}

bool TrackedTransfer::sameAs(const TrackedTransfer& other) const
{
    return tx_hash == other.tx_hash;
}

// =============================================================================
// TransactionTracker
// =============================================================================

TransactionTracker::TransactionTracker(QObject* parent)
    : QObject(parent)
{
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(static_cast<int>(pollIntervalMs_.count()));
    pollTimer_->setSingleShot(false);
    connect(pollTimer_, &QTimer::timeout,
            this, &TransactionTracker::onPollTick);

    earlyRefreshTimer_ = new QTimer(this);
    earlyRefreshTimer_->setInterval(static_cast<int>(firstRefreshDelayMs_.count()));
    earlyRefreshTimer_->setSingleShot(true);
    connect(earlyRefreshTimer_, &QTimer::timeout,
            this, &TransactionTracker::onEarlyRefreshTick);
}

TransactionTracker::TransactionTracker(WalletRpcClient* wallet,
                                         LocalDataManager* localData,
                                         QObject* parent)
    : QObject(parent)
{
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(static_cast<int>(pollIntervalMs_.count()));
    pollTimer_->setSingleShot(false);
    connect(pollTimer_, &QTimer::timeout,
            this, &TransactionTracker::onPollTick);

    earlyRefreshTimer_ = new QTimer(this);
    earlyRefreshTimer_->setInterval(static_cast<int>(firstRefreshDelayMs_.count()));
    earlyRefreshTimer_->setSingleShot(true);
    connect(earlyRefreshTimer_, &QTimer::timeout,
            this, &TransactionTracker::onEarlyRefreshTick);

    setWalletRpcClient(wallet);
    setLocalDataManager(localData);
}

TransactionTracker::~TransactionTracker()
{
    if (pollTimer_ != nullptr)         pollTimer_->stop();
    if (earlyRefreshTimer_ != nullptr) earlyRefreshTimer_->stop();
}

// -----------------------------------------------------------------------------
// Wiring
// -----------------------------------------------------------------------------

void TransactionTracker::setWalletRpcClient(WalletRpcClient* w)
{
    if (wallet_ == w) return;

    // st-130: disconnect old wallet's async signals before swapping.
    if (wallet_ != nullptr) {
        disconnect(wallet_, &HttpRpcClient::rpcCompleted,
                   this, &TransactionTracker::onGetTransfersCompleted);
        disconnect(wallet_, &HttpRpcClient::rpcFailed,
                   this, &TransactionTracker::onGetTransfersFailed);
    }

    wallet_ = w;
    activeCorrelationId_.clear();

    // st-130: connect new wallet's async completion signals so the tracker
    // receives get_transfers results via the Qt event loop (non-blocking).
    if (wallet_ != nullptr) {
        connect(wallet_, &HttpRpcClient::rpcCompleted,
                this, &TransactionTracker::onGetTransfersCompleted);
        connect(wallet_, &HttpRpcClient::rpcFailed,
                this, &TransactionTracker::onGetTransfersFailed);
    }
}

void TransactionTracker::setLocalDataManager(LocalDataManager* m)
{
    localData_ = m;
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void TransactionTracker::start(quint32 account_index)
{
    accountIndex_ = account_index;
    // Wipe the cached snapshot so a new account doesn't briefly show
    // the previous account's rows. The first refresh below will refill.
    transfers_.clear();
    knownHashes_.clear();
    setState(State::Polling);
    lastError_.clear();
    doRefresh();
    pollTimer_->start();
}

void TransactionTracker::stop()
{
    pollTimer_->stop();
    earlyRefreshTimer_->stop();
    setState(State::Idle);
}

void TransactionTracker::refresh()
{
    doRefresh();
}

void TransactionTracker::notifyTransferSent()
{
    // Idempotent: if the timer is already running, don't restart it.
    if (earlyRefreshTimer_->isActive()) {
        return;
    }
    earlyRefreshTimer_->start();
}

// -----------------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------------

TrackedTransfer TransactionTracker::findByHash(const QString& tx_hash) const
{
    for (const auto& t : transfers_) {
        if (t.tx_hash == tx_hash) {
            return t;
        }
    }
    return TrackedTransfer{};
}

QString TransactionTracker::noteFor(const QString& tx_hash) const
{
    if (localData_ == nullptr || tx_hash.isEmpty()) {
        return QString();
    }
    return localData_->txNotes().noteFor(tx_hash);
}

// st-053: QML-facing slice used by RecentTransactions.qml. Returns the
// first kRecentTransfersLimit rows as a QVariantList of QVariantMap
// entries ready for a QML ListView `model:` binding. The shape mirrors
// the TxRow.qml (st-052) public API so the QML delegate can bind each
// row straight from the model without an intermediate translation.
//
// We do NOT call this from C++ tests; it's purely a QML bridge. The
// test_transaction_tracker suite (st-018) covers the C++ tracker
// surface end-to-end via trackedTransfers() / transactionsUpdated.
QVariantList TransactionTracker::recentTransfers() const
{
    QVariantList result;
    if (transfers_.isEmpty()) {
        return result;
    }
    const int n = qMin(kRecentTransfersLimit,
                       static_cast<int>(transfers_.size()));
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        const TrackedTransfer& t = transfers_.at(i);
        QVariantMap m;
        m.insert(QStringLiteral("txHash"),       t.tx_hash);
        // wallet-rpc type is one of {"in","out","pending","pool","failed"}.
        // TxRow.qml (st-052) only branches on "in"/"out" but accepts any
        // string (the direction glyph falls back to ↓ / --tx-out for
        // anything that isn't "in"). We pass the raw type through so
        // QML / future code can render pool/failed with a custom hint
        // without a second bridge method.
        m.insert(QStringLiteral("direction"),    t.type);
        // counterparty is the FIRST destination address (wallet-rpc
        // get_transfers "destinations" array). If the array is empty
        // (e.g. mining tx or pool entry with no recipient) we emit "" so
        // the TxRow shows its "(no counterparty)" fallback (TxRow.qml
        // line ~282).
        m.insert(QStringLiteral("counterparty"),
                 t.destinations.isEmpty() ? QString() : t.destinations.first());
        // amount is signed atomic units (negative = outgoing). TxRow
        // formats it as "+N.NNN JDE" / "-N.NNN JDE" with the direction
        // color; we pass the raw qint64 through. NOTE: QVariant::fromValue
        // would also work but QML auto-converts qint64 -> double, which
        // loses precision above 2^53 atomic units (~9 PEB). For V1.0 user
        // wallets this is well outside the realistic range so we just
        // hand the qint64 through QVariantList's auto-promotion.
        m.insert(QStringLiteral("amount"),       QVariant::fromValue<qint64>(t.amount));
        // timestamp: wallet-rpc gives unix-seconds quint64; we forward
        // as double (QML Date arithmetic is happy up to 2^53) so the
        // TxRow._toDate() helper can pick the right magnitude branch.
        m.insert(QStringLiteral("timestamp"),    static_cast<double>(t.timestamp));
        m.insert(QStringLiteral("note"),         t.note);
        result.append(m);
    }
    return result;
}

// st-062: full-history bridge used by HistoryPage.qml. Mirrors the field
// shape of recentTransfers() (txHash / direction / counterparty / amount /
// timestamp / note) but adds a `state` field ("pending" | "confirming" |
// "confirmed") so the HistoryPage TabBar / filter logic can use the same
// UI-state machine the Dashboard panel does. No upper bound - the History
// page renders every tracked row.
//
// We deliberately do NOT cap this list: the C++ side owns the
// snapshot-size policy via wallet-rpc pagination (and the V1.0 user is
// expected to have < 1k rows). If a future sub_task needs paging, this
// is the place to add an offset/limit.
QVariantList TransactionTracker::transfers() const
{
    QVariantList result;
    if (transfers_.isEmpty()) {
        return result;
    }
    result.reserve(static_cast<int>(transfers_.size()));
    for (int i = 0; i < transfers_.size(); ++i) {
        const TrackedTransfer& t = transfers_.at(i);
        QVariantMap m;
        m.insert(QStringLiteral("txHash"),       t.tx_hash);
        m.insert(QStringLiteral("direction"),    t.type);
        m.insert(QStringLiteral("counterparty"),
                 t.destinations.isEmpty() ? QString() : t.destinations.first());
        m.insert(QStringLiteral("amount"),       QVariant::fromValue<qint64>(t.amount));
        m.insert(QStringLiteral("timestamp"),    static_cast<double>(t.timestamp));
        m.insert(QStringLiteral("note"),         t.note);
        // st-062: per-row UI state (pending / confirming / confirmed).
        // Drives the HistoryPage TabBar's "Pending" filter and the
        // TxDetailDialog's state badge. Stable lowercase name so
        // QML can switch on the string.
        switch (t.state) {
            case TrackedTransfer::State::Pending:    m.insert(QStringLiteral("state"), QStringLiteral("pending"));    break;
            case TrackedTransfer::State::Confirming: m.insert(QStringLiteral("state"), QStringLiteral("confirming")); break;
            case TrackedTransfer::State::Confirmed:  m.insert(QStringLiteral("state"), QStringLiteral("confirmed"));  break;
        }
        // st-062: confirmations + blockHeight let the TxDetailDialog show
        // a "5/10 confirmations" hint without re-fetching.
        m.insert(QStringLiteral("confirmations"), static_cast<double>(t.confirmations));
        m.insert(QStringLiteral("blockHeight"),   static_cast<double>(t.block_height));
        m.insert(QStringLiteral("fee"),           QVariant::fromValue<quint64>(t.fee));
        result.append(m);
    }
    return result;
}

TrackedTransfer::State TransactionTracker::classifyState(quint64 block_height,
                                                          quint64 confirmations,
                                                          bool    unlocked)
{
    if (block_height == 0) {
        return TrackedTransfer::State::Pending;
    }
    if (unlocked || confirmations >= static_cast<quint64>(kConfirmationTarget)) {
        return TrackedTransfer::State::Confirmed;
    }
    return TrackedTransfer::State::Confirming;
}

// -----------------------------------------------------------------------------
// Test helpers
// -----------------------------------------------------------------------------

void TransactionTracker::setPollInterval(std::chrono::milliseconds ms)
{
    pollIntervalMs_ = ms;
    if (pollTimer_ != nullptr) {
        const bool wasActive = pollTimer_->isActive();
        pollTimer_->stop();
        pollTimer_->setInterval(static_cast<int>(ms.count()));
        if (wasActive) {
            pollTimer_->start();
        }
    }
}

void TransactionTracker::setFirstRefreshDelay(std::chrono::milliseconds ms)
{
    firstRefreshDelayMs_ = ms;
    if (earlyRefreshTimer_ != nullptr) {
        const bool wasActive = earlyRefreshTimer_->isActive();
        earlyRefreshTimer_->stop();
        earlyRefreshTimer_->setInterval(static_cast<int>(ms.count()));
        if (wasActive) {
            earlyRefreshTimer_->start();
        }
    }
}

// -----------------------------------------------------------------------------
// Slot handlers
// -----------------------------------------------------------------------------

void TransactionTracker::onPollTick()
{
    ++pollTickCount_;
    doRefresh();
}

void TransactionTracker::onEarlyRefreshTick()
{
    doRefresh();
}

// -----------------------------------------------------------------------------
// st-130: async get_transfers completion handlers
// -----------------------------------------------------------------------------

void TransactionTracker::onGetTransfersCompleted(const QString& correlationId,
                                                   const QString& method,
                                                   const QJsonObject& result)
{
    Q_UNUSED(method);

    // Ignore callbacks that don't match our in-flight request (e.g. a
    // stale completion from a previous account or a different caller).
    if (correlationId != activeCorrelationId_) return;
    activeCorrelationId_.clear();

    TransferList list;
    if (!TransferList::parse(result, list)) {
        lastError_ = QStringLiteral("Failed to parse get_transfers response");
        if (state_ != State::Error) {
            setState(State::Error);
        }
        Q_EMIT errorOccurred(lastError_);
        return;
    }

    lastError_.clear();
    if (state_ != State::Polling) {
        setState(State::Polling);
    }
    lastRefreshMs_ = QDateTime::currentMSecsSinceEpoch();
    ++refreshCount_;
    applyTransferList(list);
}

void TransactionTracker::onGetTransfersFailed(const QString& correlationId,
                                                const QString& method,
                                                const RpcException& error)
{
    Q_UNUSED(method);

    if (correlationId != activeCorrelationId_) return;
    activeCorrelationId_.clear();

    lastError_ = error.toString();
    if (state_ != State::Error) {
        setState(State::Error);
    }
    Q_EMIT errorOccurred(lastError_);
}

// -----------------------------------------------------------------------------
// Internal: doRefresh
// -----------------------------------------------------------------------------

void TransactionTracker::doRefresh()
{
    if (wallet_ == nullptr) {
        lastError_ = QStringLiteral("WalletRpcClient not set");
        if (state_ != State::Error) {
            setState(State::Error);
        }
        Q_EMIT errorOccurred(lastError_);
        return;
    }

    // st-130: async refresh. If a get_transfers call is already in-flight,
    // skip this one (prevents stacking from poll timer + early-refresh
    // timer overlapping). The next timer tick will retry.
    if (!activeCorrelationId_.isEmpty()) {
        return;
    }

    try {
        activeCorrelationId_ = wallet_->getTransfersAsync(
            QStringLiteral("all"),
            static_cast<quint64>(accountIndex_),
            /*max_count=*/0);
    } catch (...) {
        // Async dispatch should not throw, but defensively surface the
        // failure so the UI doesn't hang in Polling indefinitely.
        lastError_ = QStringLiteral("Failed to start async transfer fetch");
        if (state_ != State::Error) {
            setState(State::Error);
        }
        Q_EMIT errorOccurred(lastError_);
    }
}

void TransactionTracker::applyTransferList(const TransferList& list)
{
    // Build the new snapshot. We classify and pair with notes here so
    // the emitted `transactionsUpdated` payload is self-contained.
    QVector<TrackedTransfer> next;
    next.reserve(static_cast<int>(list.entries.size()));
    QSet<QString> nextHashes;
    for (const auto& e : list.entries) {
        TrackedTransfer t = buildFromEntry(e, accountIndex_);
        next.push_back(t);
        nextHashes.insert(t.tx_hash);
    }

    // Diff against knownHashes_ to emit add/update per row.
    for (const auto& t : next) {
        if (!knownHashes_.contains(t.tx_hash)) {
            Q_EMIT transferAdded(t);
        } else {
            // Update only if the row materially changed. We compare the
            // full row by value (small struct, copy-cheap).
            const TrackedTransfer old = findByHash(t.tx_hash);
            // Material difference: state changed OR amount changed OR
            // confirmations changed OR note changed.
            if (old.state != t.state ||
                old.amount != t.amount ||
                old.confirmations != t.confirmations ||
                old.unlocked != t.unlocked ||
                old.block_height != t.block_height ||
                old.note != t.note)
            {
                Q_EMIT transferUpdated(t);
            }
        }
    }

    transfers_ = std::move(next);
    knownHashes_ = std::move(nextHashes);
    Q_EMIT transactionsUpdated(transfers_);
    // st-053: also fire the NOTIFY for the recentTransfers Q_PROPERTY
    // so QML ListView bound to `transactionTrackerObj.recentTransfers`
    // re-reads the slice. We emit AFTER transactionsUpdated so any
    // QML slot wired to transactionsUpdated runs first (matches the
    // "raw snapshot first, derived slice second" mental model).
    Q_EMIT recentTransfersChanged();
    // st-062: also fire the NOTIFY for the `transfers` Q_PROPERTY so the
    // HistoryPage ListView bound to `transactionTrackerObj.transfers`
    // re-reads the full snapshot. Emitted AFTER recentTransfersChanged so
    // the dashboard (5-row slice) updates first and the full history
    // follows - matches the "narrow first, broad second" mental model.
    Q_EMIT transfersChanged();
}

TrackedTransfer TransactionTracker::buildFromEntry(const TransferEntry& e,
                                                     quint32 /*account_index*/) const
{
    TrackedTransfer t;
    t.tx_hash       = e.tx_hash;
    t.amount        = e.amount;
    t.block_height  = e.block_height;
    t.confirmations = e.confirmations;
    t.unlocked      = e.unlocked;
    t.fee           = e.fee;
    t.timestamp     = e.timestamp;
    t.payment_id    = e.payment_id;
    t.type          = e.type;
    t.destinations  = e.destinations;
    // Pair with the local note. We resolve at row-build time so the
    // emitted `transactionsUpdated` payload is self-contained (QML does
    // not have to query the LocalDataManager separately).
    if (localData_ != nullptr) {
        t.note = localData_->txNotes().noteFor(t.tx_hash);
    }
    t.state = classifyState(t.block_height, t.confirmations, t.unlocked);
    return t;
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void TransactionTracker::setState(State newState)
{
    if (state_ == newState) {
        return;
    }
    state_ = newState;
    Q_EMIT stateChanged(newState);
}

} // namespace judecoin_wallet::core::tracker
