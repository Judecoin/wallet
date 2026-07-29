// =============================================================================
// Judecoin Wallet - PendingTxState (st-013)
//
// 3-state tracker for an in-flight transfer_split RPC, per docs/UI_DETAILS_TODO.md
// "crash recovery sub-decision #2" (V5.4.4 #2 B + C).
//
// Background
// ----------
// wallet-rpc::transfer_split is a long-running call. If wallet-rpc is killed
// (or daemon dies) WHILE the call is in flight, the UI must figure out whether
// the user's funds are safe. There are exactly three possibilities:
//
//   1. tx_hash not yet generated
//      wallet-rpc was still assembling the tx (mixing inputs, picking decoys,
//      signing). Nothing has been broadcast; nothing has been spent from the
//      user's wallet. User is SAFE: just retry the call.
//
//   2. tx_hash generated but NOT broadcast
//      wallet-rpc finished assembling the tx and the in-process wallet has
//      ALREADY subtracted the spent outputs from its local balance view.
//      But the tx is sitting in a local "tx pool" that never made it to
//      the network. After wallet-rpc is restarted the local pool is gone
//      (per wallet-rpc design - no on-disk tx staging for V1.0). User is in
//      a "funds look spent but the network never saw the tx" limbo. We must
//      surface a warning ("transfer may have been lost; check history") and
//      let the user manually re-send once the chain resyncs.
//
//   3. tx_hash generated AND broadcast
//      wallet-rpc finished the call AND it was accepted into the daemon
//      mempool (or already in a block). Even if wallet-rpc dies now, the
//      tx is on the network. After wallet-rpc restart the tx shows up in
//      get_transfers (per docs/CLI_COMMANDS.md 8.4). User is SAFE: the
//      transfer completed; just let the history view catch up.
//
// State machine
// -------------
//
//   NotGenerated --(updateGenerated(hash))--> GeneratedNotBroadcast
//   GeneratedNotBroadcast --(updateBroadcast())--> GeneratedAndBroadcast
//   GeneratedNotBroadcast --(markOrphaned())--> GeneratedNotBroadcast
//                                                 (stays, but emits
//                                                  transferOrphaned)
//
//   No state ever regresses (monotonic). Once a tx reaches
//   GeneratedAndBroadcast, no further transitions are accepted; the entry
//   can be cleared via clear() once the user acknowledges.
//
// V5.4.7 compliance: PendingTxState is purely a UI-side tracker. It never
// reads .keys files, never re-derives keys, never accesses wallet state
// directly. It is fed by the UI (hash returned from wallet-rpc, broadcast
// detection from daemon RPC) and queried by the UI ("should I warn the
// user about this pending transfer?").
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RECOVERY_PENDING_TX_STATE_H
#define JUDECOIN_WALLET_CORE_RECOVERY_PENDING_TX_STATE_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace judecoin_wallet::core::recovery {

// -----------------------------------------------------------------------------
// PendingTxState - one row per in-flight transfer.
//
// Constructed via the static factories (init / fromJson). Direct construction
// is allowed but discouraged; prefer init() so the timestamp is set.
// -----------------------------------------------------------------------------
class PendingTxState
{
public:
    // V5.4.4 #2 B + C: exactly 3 sub-states for a pending transfer.
    enum class State {
        NotGenerated,            // wallet-rpc is still building the tx
        GeneratedNotBroadcast,   // tx_hash known but not seen on chain
        GeneratedAndBroadcast,   // tx_hash known AND seen on chain
    };

    // -- factories -----------------------------------------------------------

    // Initialize a new pending transfer. The caller (CrashRecoveryManager)
    // passes an opaque UI-issued transferId (e.g. "tx-2026-07-03-001"); we
    // also stash amount/destinations for diagnostics only.
    static PendingTxState init(const QString& transferId,
                               quint64 amountAtomic,
                               const QString& primaryAddress,
                               qint64 startedAtMs);

    // Parse back from a serialized JSON blob (used when reloading from disk
    // across a UI restart). Returns a default-constructed instance on parse
    // failure; callers must check isValid().
    static PendingTxState fromJson(const QJsonObject& obj);

    // -- accessors -----------------------------------------------------------

    QString transferId() const { return transferId_; }
    quint64 amountAtomic() const { return amountAtomic_; }
    QString primaryAddress() const { return primaryAddress_; }
    QString txHash() const { return txHash_; }
    State   state() const { return state_; }
    qint64  startedAtMs() const { return startedAtMs_; }
    qint64  generatedAtMs() const { return generatedAtMs_; }
    qint64  broadcastAtMs() const { return broadcastAtMs_; }

    // True iff this entry is non-empty (used by fromJson callers to detect
    // a parse failure: an invalid entry has empty transferId_).
    bool isValid() const { return !transferId_.isEmpty(); }

    // Stable name for the State enum (lowercase, suitable for logs / JSON).
    static QString stateName(State s);
    static State   stateFromName(const QString& name);

    // -- mutators (state transitions) --------------------------------------

    // Move NotGenerated -> GeneratedNotBroadcast. Sets tx_hash_ and the
    // generatedAtMs_ timestamp. Returns true iff the transition was legal
    // (i.e. we were in NotGenerated). Calling twice is a no-op the second
    // time; we treat it as a re-confirmation rather than an error.
    bool updateGenerated(const QString& txHash, qint64 atMs);

    // Move GeneratedNotBroadcast -> GeneratedAndBroadcast. Stamps
    // broadcastAtMs_. Returns true iff the transition was legal.
    bool updateBroadcast(qint64 atMs);

    // Mark the transfer as orphaned (tx_hash generated but not seen in the
    // network after a daemon/wallet-rpc crash). Stays in
    // GeneratedNotBroadcast; emits no signal itself - the caller observes
    // the state and emits the UI-facing signal.
    bool markOrphaned();

    // Move to a terminal "completed" sentinel. Used once the user has
    // confirmed the transfer is in history; the entry should be removed.
    // Returns true iff state was already GeneratedAndBroadcast.
    bool markAcknowledged();

    // -- serialisation ------------------------------------------------------

    QJsonObject toJson() const;

    // Convenience: render the JSON wire form for the in-memory map dump
    // (debug-only; not part of any external API).
    static QVector<PendingTxState> allFromJsonArray(const QJsonArray& arr);
    static QJsonArray allToJsonArray(const QVector<PendingTxState>& list);

private:
    // Private ctor; use the factories.
    PendingTxState() = default;

    QString transferId_;
    quint64 amountAtomic_   = 0;
    QString primaryAddress_;
    QString txHash_;
    State   state_          = State::NotGenerated;
    qint64  startedAtMs_    = 0;
    qint64  generatedAtMs_  = 0;  // 0 if not yet generated
    qint64  broadcastAtMs_  = 0;  // 0 if not yet broadcast
    bool    orphaned_       = false;
    bool    acknowledged_   = false;
};

} // namespace judecoin_wallet::core::recovery

#endif // JUDECOIN_WALLET_CORE_RECOVERY_PENDING_TX_STATE_H
