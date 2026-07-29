// =============================================================================
// Judecoin Wallet - TransactionTracker (st-018)
//
// V5.4.3 plan B: UI dumb display layer for the transaction list.
//
// What this is
// ------------
//   A single-account transaction-history reader that polls
//   wallet-rpc.get_transfers every kPollInterval (30s) and re-classifies each
//   row into one of three UI states:
//
//     Pending     - block_height == 0  (tx in mempool, pool, failed, or
//                                    pending — no block confirmation yet)
//     Confirming  - block_height > 0 && confirmations < kConfirmationTarget
//     Confirmed   - confirmations >= kConfirmationTarget OR unlocked == true
//
//   The tracker is the consumer of truth for the UI's tx list. QML reads
//   `trackedTransfers()` and binds to the `transactionsUpdated` signal — the
//   tracker itself does no business logic. Per V5.4.3 plan B, the UI is
//   forbidden from re-deriving keys, computing confirmation counts, or
//   making its own decisions about "is this transfer safe to spend"; all of
//   that flows from the wallet-rpc JSON we feed into classifyState().
//
//   Each emitted row is paired with the local `tx_notes.json` store from
//   LocalDataManager (st-017) keyed on tx_hash. This is the "V5.4.5 storage
//   policy" — we keep the note metadata in UI-local JSON, not via wallet-rpc.
//
// What this is NOT
// ----------------
//   - Not a crash-recovery module (that's st-013 / PendingTxState). This
//     tracker only sees txs that already exist in wallet-rpc's history.
//   - Not a multi-account aggregator (V1.0 tracks one account at a time;
//     the QML side switches accounts by calling start(newAccountIndex)).
//   - Not a balance tracker (use BalanceApi + BalanceInfo for that).
//
// Threading
// ---------
//   All signals are emitted on the thread that owns this QObject (the UI
//   main thread). The poll timer fires in the same thread, so slot handlers
//   and signal receivers share the GUI event loop — no thread affinity
//   surprises. Matches st-013 CrashRecoveryManager's threading rule.
//
// V5.4.7 compliance: the tracker never reads .keys files, never derives
// keys, never re-signs anything. The tx_hash string is treated as opaque
// metadata.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_TRACKER_TRANSACTION_TRACKER_H
#define JUDECOIN_WALLET_CORE_TRACKER_TRANSACTION_TRACKER_H

#include "core/rpc/HistoryApi.h"

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <chrono>

class QTimer;

namespace judecoin_wallet::core::rpc {
class WalletRpcClient;
class RpcException;
} // namespace judecoin_wallet::core::rpc

namespace judecoin_wallet::core::localdata {
class LocalDataManager;
} // namespace judecoin_wallet::core::localdata

namespace judecoin_wallet::core::tracker {

// -----------------------------------------------------------------------------
// TrackedTransfer
//
// One row in the transaction list. Mirrors the parsed fields of
// rpc::TransferEntry (tx_hash, amount, block_height, confirmations, ...)
// plus the local note from tx_notes.json and our derived UI state.
//
// Stored by-value in a QVector (small, copy-cheap). hash is the natural key
// for deduplication; we keep an O(1) QSet<QString> of seen hashes alongside
// the vector so that add/update can be detected per refresh.
// -----------------------------------------------------------------------------
struct TrackedTransfer
{
    QString  tx_hash;          // wallet-rpc get_transfers "tx_hash" (opaque)
    qint64   amount     = 0;   // atomic units (signed; neg = outgoing)
    quint64  block_height = 0; // 0 = not in a block (pending / pool / failed)
    quint64  confirmations = 0;
    bool     unlocked   = false;
    quint64  fee        = 0;
    quint64  timestamp  = 0;   // unix seconds (uint64 per wallet-rpc)
    QString  payment_id;
    QString  type;             // "in" / "out" / "pending" / "failed" / "pool"
    QStringList destinations;
    QString  note;             // from tx_notes.json; "" if user has no note

    // UI-state machine, derived from the fields above. See classifyState().
    enum class State {
        Pending,     // block_height == 0
        Confirming,  // block_height > 0 && confirmations < 10 && !unlocked
        Confirmed,   // confirmations >= 10 || unlocked
    };
    State state = State::Pending;

    bool isValid() const { return !tx_hash.isEmpty(); }
    bool isConfirmed() const { return state == State::Confirmed; }
    bool isInChain() const { return block_height > 0; }

    // Stable lowercase name for State (logs / JSON / QSettings keys).
    static QString stateName(State s);
    static State   stateFromName(const QString& name);

    // Equality by tx_hash only (refresh = replace by hash). Order-insensitive.
    bool sameAs(const TrackedTransfer& other) const;
};

// -----------------------------------------------------------------------------
// TransactionTracker
//
// Owns: the 30s poll QTimer + the optional 2s one-shot "early refresh" timer
// triggered by notifyTransferSent() right after a transfer submission.
// Holds non-owning pointers to WalletRpcClient and LocalDataManager.
// -----------------------------------------------------------------------------
class TransactionTracker : public QObject
{
    Q_OBJECT

public:
    // Top-level state for the tracker (separate from per-tx state).
    enum class State {
        Idle,      // start() not called yet (or stop() was called)
        Polling,   // poll timer is active, last refresh was OK
        Error,     // last refresh failed (RPC error); next tick will retry
    };
    Q_ENUM(State)

    // Spec defaults (V5.4.3 + V5.4.5). Tests can override via setters.
    static constexpr std::chrono::seconds kPollInterval{30};   // 30s
    static constexpr std::chrono::seconds kFirstRefreshDelay{2}; // 2s
    static constexpr int kConfirmationTarget = 10;             // 10 confs

    explicit TransactionTracker(QObject* parent = nullptr);
    TransactionTracker(judecoin_wallet::core::rpc::WalletRpcClient* wallet,
                       judecoin_wallet::core::localdata::LocalDataManager* localData,
                       QObject* parent = nullptr);
    ~TransactionTracker() override;

    // -- wiring --------------------------------------------------------------
    // Setter form: lets the UI wire the tracker after the wallet-rpc client
    // and LocalDataManager have been constructed (matches the order in
    // st-013 / st-017 wiring). Either pointer may be null at any time; we
    // only dereference them inside refresh() and inside noteFor().
    void setWalletRpcClient(judecoin_wallet::core::rpc::WalletRpcClient*);
    void setLocalDataManager(judecoin_wallet::core::localdata::LocalDataManager*);

    // -- lifecycle -----------------------------------------------------------
    // start() switches to Polling, fires one synchronous refresh, and
    // arms the 30s timer. Calling start() while Polling restarts the
    // timer (i.e. it acts as a soft "restart with new account index").
    void start(quint32 account_index = 0);
    void stop();

    // Force a refresh right now. Safe to call from any slot; the call is
    // synchronous (blocks for the RPC roundtrip) so it should be called
    // from the UI thread, never from a worker thread.
    void refresh();

    // Notify the tracker that a new transfer was just submitted. This
    // arms a 2s one-shot timer so the UI sees the tx move from
    // "submitting" to "pending in pool" without waiting for the next
    // 30s tick. Idempotent: a second call before the 2s fires is a
    // no-op (we don't stack early-refresh timers).
    void notifyTransferSent();

    // -- accessors -----------------------------------------------------------
    State  state() const          { return state_; }
    quint32 accountIndex() const { return accountIndex_; }
    QString lastError() const     { return lastError_; }
    qint64  lastRefreshMs() const { return lastRefreshMs_; }

    // Current snapshot of tracked transfers. The QVector is returned
    // by value (defensive copy) to keep the QML model from mutating
    // our internal state.
    QVector<TrackedTransfer> trackedTransfers() const { return transfers_; }

    // QML-facing slice used by RecentTransactions.qml (st-053). Returns
    // the first kRecentTransfersLimit rows as a QVariantList of
    // QVariantMap entries, ready for a QML ListView `model:` binding.
    // The shape of each entry mirrors the TxRow.qml (st-052) public API:
    //   txHash       (string)
    //   direction    (string: "in" | "out" | "pending" | "pool" |
    //                          "failed" — TxRow collapses pending/pool/
    //                          failed into "in" so the avatar stays neutral)
    //   counterparty (string, first destination address or "" if missing)
    //   amount       (double, atomic units; +in / -out signed)
    //   timestamp    (quint64, unix seconds per wallet-rpc get_transfers)
    //   note         (string, user note from tx_notes.json or "")
    //
    // The Q_PROPERTY NOTIFY signal fires whenever the underlying
    // `transfers_` snapshot changes, so QML re-reads the slice and
    // ListView redraws. Limit is 5 by default (Dashboard "recent"
    // panel); HistoryPage can read the full list via trackedTransfers().
    // st-130: QML-facing tracker state so the HistoryPage can surface
    // a warning banner when wallet-rpc is unreachable.
    // trackerState: 0=Idle, 1=Polling, 2=Error.
    // trackerError: last error message (empty when healthy).
    Q_PROPERTY(int trackerState READ trackerStateInt NOTIFY stateChanged)
    Q_PROPERTY(QString trackerError READ lastError NOTIFY errorOccurred)
    int trackerStateInt() const { return static_cast<int>(state_); }

    Q_PROPERTY(QVariantList recentTransfers
               READ recentTransfers
               NOTIFY recentTransfersChanged)
    QVariantList recentTransfers() const;

    /// Hard cap for the recentTransfers slice. Matches design_specs.md
    /// §5.5 ("5 row limit on the Dashboard panel"). Kept as a public
    /// constant so tests / future code can refer to it symbolically.
    static constexpr int kRecentTransfersLimit = 5;

    // st-062: full-history Q_PROPERTY used by HistoryPage.qml. Unlike
    // `recentTransfers` (capped at kRecentTransfersLimit == 5 for the
    // Dashboard panel), `transfers` exposes the entire tracked snapshot
    // so the HistoryPage ListView can show every row. The shape of each
    // entry matches `recentTransfers()` (QVariantMap of txHash /
    // direction / counterparty / amount / timestamp / note + the
    // HistoryPage-only `state` field). NOTIFY signal `transfersChanged`
    // is fired on every successful refresh alongside
    // recentTransfersChanged() so both ListView bindings refresh in
    // lockstep.
    Q_PROPERTY(QVariantList transfers
               READ transfers
               NOTIFY transfersChanged)
    QVariantList transfers() const;

    // O(N) lookup by tx_hash; returns a default-constructed TrackedTransfer
    // on miss. N is small (a few hundred at most for V1.0 users) so we
    // don't bother with a parallel hash index.
    TrackedTransfer findByHash(const QString& tx_hash) const;

    // Convenience: returns the local note string for a tx_hash. Empty
    // string if the user has no note OR if LocalDataManager is not wired.
    QString noteFor(const QString& tx_hash) const;

    // Per-tx state classifier — static + pure, so tests can call it
    // without instantiating a tracker. The rule:
    //   block_height == 0  -> Pending
    //   block_height  > 0 && conf < kConfirmationTarget && !unlocked
    //                       -> Confirming
    //   conf >= kConfirmationTarget || unlocked  -> Confirmed
    static TrackedTransfer::State classifyState(quint64 block_height,
                                                 quint64 confirmations,
                                                 bool    unlocked);

    // Test helpers (avoid the 30s default in unit tests). Tests pass small
    // intervals (e.g. 50ms) so the suite finishes in < 1 second.
    void setPollInterval(std::chrono::milliseconds ms);
    void setFirstRefreshDelay(std::chrono::milliseconds ms);

    // Number of poll ticks that have fired (test/observability counter).
    int pollTickCount() const { return pollTickCount_; }
    int refreshCount() const  { return refreshCount_; }

Q_SIGNALS:
    // Emitted on every state transition (Idle <-> Polling / Error).
    void stateChanged(judecoin_wallet::core::tracker::TransactionTracker::State state);

    // Fired after a successful refresh. Carries the full snapshot
    // (cheaper for QML than a per-row add/update, matches st-013's
    // pattern of emitting the full map on each transition).
    void transactionsUpdated(const QVector<judecoin_wallet::core::tracker::TrackedTransfer>& transfers);

    // st-053: NOTIFY signal for the recentTransfers Q_PROPERTY. Fires on
    // every successful refresh (mirrors transactionsUpdated) so QML's
    // ListView bound to `transactionTrackerObj.recentTransfers` re-reads
    // the slice and re-paints. We don't carry the data because the
    // getter is cheap (kRecentTransfersLimit items, by value).
    void recentTransfersChanged();

    // st-062: NOTIFY signal for the `transfers` Q_PROPERTY used by
    // HistoryPage.qml. Same fire timing as recentTransfersChanged() -
    // emitted after every successful refresh, alongside
    // recentTransfersChanged(), so both ListView bindings refresh in
    // lockstep. The HistoryPage ListView reads `transactionTrackerObj.
    // transfers`; emitting this signal re-fires that getter.
    void transfersChanged();

    // Per-row signals so the UI can show a toast / play a sound for
    // newly-arrived rows. transferAdded fires for tx_hashes we have
    // never seen before; transferUpdated fires when an existing row
    // changes (e.g. confirmations 3 -> 5, Pending -> Confirming).
    void transferAdded(const judecoin_wallet::core::tracker::TrackedTransfer& t);
    void transferUpdated(const judecoin_wallet::core::tracker::TrackedTransfer& t);

    // Emitted on refresh failure (RPC error, timeout, parse failure).
    void errorOccurred(const QString& message);

private Q_SLOTS:
    void onPollTick();
    void onEarlyRefreshTick();

    // st-130: async get_transfers completion handlers.
    void onGetTransfersCompleted(const QString& correlationId,
                                 const QString& method,
                                 const QJsonObject& result);
    void onGetTransfersFailed(const QString& correlationId,
                              const QString& method,
                              const judecoin_wallet::core::rpc::RpcException& error);

private:
    void setState(State newState);
    void doRefresh();   // shared by refresh() and onPollTick() / onEarlyRefreshTick()
    void applyTransferList(const judecoin_wallet::core::rpc::TransferList& list);
    TrackedTransfer buildFromEntry(const judecoin_wallet::core::rpc::TransferEntry& e,
                                   quint32 account_index) const;

    // Dependencies (weak pointers — we never own them).
    judecoin_wallet::core::rpc::WalletRpcClient* wallet_   = nullptr;
    judecoin_wallet::core::localdata::LocalDataManager* localData_ = nullptr;

    quint32  accountIndex_   = 0;
    State    state_          = State::Idle;
    QString  lastError_;
    qint64   lastRefreshMs_  = 0;
    int      pollTickCount_  = 0;
    int      refreshCount_   = 0;

    // Current snapshot + dedup helper.
    QVector<TrackedTransfer> transfers_;
    QSet<QString> knownHashes_;

    // Async correlation tracking (st-130). Non-empty while an async
    // get_transfers call is in-flight; subsequent doRefresh() calls are
    // skipped until the current one completes.
    QString activeCorrelationId_;

    // Timers.
    QTimer* pollTimer_         = nullptr; // 30s, repeating
    QTimer* earlyRefreshTimer_ = nullptr; // 2s, one-shot

    // Effective intervals (mutable so tests can shrink them).
    std::chrono::milliseconds pollIntervalMs_         = std::chrono::duration_cast<std::chrono::milliseconds>(kPollInterval);
    std::chrono::milliseconds firstRefreshDelayMs_   = std::chrono::duration_cast<std::chrono::milliseconds>(kFirstRefreshDelay);

    Q_DISABLE_COPY(TransactionTracker)
};

} // namespace judecoin_wallet::core::tracker

Q_DECLARE_METATYPE(judecoin_wallet::core::tracker::TrackedTransfer)
Q_DECLARE_METATYPE(QVector<judecoin_wallet::core::tracker::TrackedTransfer>)

#endif // JUDECOIN_WALLET_CORE_TRACKER_TRANSACTION_TRACKER_H
