// =============================================================================
// Judecoin Wallet - CrashRecoveryManager (st-013)
//
// Implements the 7 sub-decisions of V5.4.4 #2 (docs/UI_DETAILS_TODO.md). The
// manager sits between ProcessManager (process lifecycle) and the UI layer:
// it listens to process state transitions, classifies failures, drives
// recovery state machines, and emits high-level signals the QML UI binds to.
//
// Seven sub-decisions
// -------------------
//   #1 wallet-rpc crash (A + daemon check)
//       Auto-restart wallet-rpc; before unlocking operations, ping the
//       daemon to confirm it still has the tx in its mempool (if any
//       transfer_split was in flight). State machine below.
//
//   #2 transfer_split hangs (B + C)
//       Track every in-flight transfer_split through 3 sub-states
//       (PendingTxState): NotGenerated -> GeneratedNotBroadcast ->
//       GeneratedAndBroadcast. See PendingTxState.h for details.
//
//   #3 daemon crash (C + lock modify operations)
//       Pop up a non-dismissable daemon-down modal; lock ALL wallet-modify
//       operations (transfer / subaddress create / address-book write);
//       5s retry loop until daemon comes back, then unlock.
//
//   #4 daemon upgrade (A)
//       Treat as daemon crash. ProcessManager already detects the daemon
//       restart cycle (stateChanged). CrashRecoveryManager mirrors the
//       crash path - we don't distinguish because the user-visible UX is
//       identical (modal popup + locked ops).
//
//   #5 UI crash (A)
//       V5.4 startup flow (st-015 StartupManager) covers UI-side crash
//       recovery; CrashRecoveryManager does NOT do anything extra. The
//       sub-decision is "no manager action; UI restart handles it".
//
//   #6 QProcess monitoring (C)
//       We piggy-back on ProcessManager's 5s heartbeat (kHeartbeatInterval
//       in ProcessManager). CrashRecoveryManager does NOT own its own
//       QProcess monitor; it observes ProcessManager::stateChanged
//       signals.
//
//   #7 restart strategy (D)
//       We delegate to RestartStrategy (st-006) which already encodes
//       the 5-error-category classification + max-attempts budget + 1s/2s/4s
//       exponential backoff. CrashRecoveryManager surfaces
//       restartScheduled() / restartExhausted() signals so the UI can
//       show progress.
//
// Three state machines implemented here
// -------------------------------------
//   1. CrashRecoveryManager state machine (this file)
//        Idle -> WalletRpcRecovering -> WalletRpcWaitForGrace
//             -> Idle
//        Idle -> DaemonDown -> DaemonRecovering -> Idle
//        any -> RecoveryExhausted (terminal)
//
//   2. PendingTxState (PendingTxState.h)
//        3-state sub-machine per transfer. Owned as values inside this
//        manager, not QObject children (no parent-tree complexity).
//
//   3. RestartStrategy (core/process/RestartStrategy.h, st-006)
//        Per-category retry counter + backoff timer. We delegate; this
//        manager observes the resulting restartScheduled() /
//        restartExhausted() signals and re-emits them with state context.
//
// V5.4.7 compliance: no key files are read, no key derivation happens
// anywhere in this module. PendingTxState entries are tagged with the
// tx_hash returned by wallet-rpc, which we treat as opaque metadata.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RECOVERY_CRASH_RECOVERY_MANAGER_H
#define JUDECOIN_WALLET_CORE_RECOVERY_CRASH_RECOVERY_MANAGER_H

#include "core/process/ErrorClassifier.h"
#include "core/process/ProcessManager.h"
#include "core/recovery/PendingTxState.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

class QTimer;

namespace judecoin_wallet::core::process {
class ProcessManager;
class RestartStrategy;
} // namespace judecoin_wallet::core::process

namespace judecoin_wallet::core::rpc {
class DaemonClient;
class WalletRpcClient;
} // namespace judecoin_wallet::core::rpc

namespace judecoin_wallet::core::recovery {

// -----------------------------------------------------------------------------
// CrashRecoveryManager
//
// Owns: map of in-flight PendingTxState entries; QTimer for the daemon
// mempool poll (1 s tick during WalletRpcRecovering); reference to the
// ProcessManager (for crash signal subscription) and RPC clients (for
// mempool polling). Re-emits classified sub-decision signals to the UI.
// -----------------------------------------------------------------------------
class CrashRecoveryManager : public QObject
{
    Q_OBJECT

public:
    // Top-level coordinator state machine (sub-decision 3 + 4 + 7 driver).
    enum class State {
        Idle,                  // no active recovery
        WalletRpcRecovering,   // wallet-rpc just crashed; restart scheduled
        WalletRpcWaitForGrace, // wallet-rpc back up; grace timer counting
        DaemonDown,            // daemon is down; lock modify ops
        DaemonRecovering,      // daemon back up; grace timer counting
        RecoveryExhausted,     // restart budget spent; terminal
    };
    Q_ENUM(State)

    // Spec defaults. Exposed as constants so tests can override and assert.
    static constexpr std::chrono::milliseconds kDaemonRetryInterval{5000};  // 5s
    static constexpr std::chrono::milliseconds kMempoolPollInterval{1000};  // 1s
    static constexpr std::chrono::milliseconds kGraceWindow{2000};          // 2s after up

    explicit CrashRecoveryManager(QObject* parent = nullptr);
    CrashRecoveryManager(judecoin_wallet::core::process::ProcessManager* pm,
                         judecoin_wallet::core::rpc::DaemonClient* daemon,
                         QObject* parent = nullptr);
    ~CrashRecoveryManager() override;

    // -- wiring --------------------------------------------------------------

    void setProcessManager(judecoin_wallet::core::process::ProcessManager* pm);
    void setDaemonClient(judecoin_wallet::core::rpc::DaemonClient* daemon);

    // -- pure helpers (no Qt side effects) ----------------------------------

    // Map an ErrorClassifier::Category to the human-readable reason used in
    // restartExhausted() payloads. Mirrors ErrorClassifier::userFacingReason
    // but is kept here so the recovery layer is self-contained.
    static QString restartReason(judecoin_wallet::core::process::ErrorClassifier::Category c);

    // -- accessors ----------------------------------------------------------

    State state() const { return state_; }
    QString lastError() const { return lastError_; }
    judecoin_wallet::core::process::ErrorClassifier::Category lastCategory() const
    { return lastCategory_; }
    int activeTransferCount() const { return pending_.size(); }
    QVector<PendingTxState> pendingTransfers() const;

    // For tests: peek the in-flight map.
    bool hasPending(const QString& transferId) const;

    // -- transfer_split tracking (sub-decision 2 / B+C) ---------------------

    // UI calls this when it submits a transfer_split. Returns false if the
    // transferId is already tracked (caller must dedup).
    bool trackTransferSplit(const QString& transferId,
                            quint64 amountAtomic,
                            const QString& primaryAddress);

    // UI calls this once wallet-rpc returns the tx_hash. Returns true if
    // the entry was found and updated.
    bool updateTransferGenerated(const QString& transferId,
                                 const QString& txHash);

    // CrashRecoveryManager itself decides when a tx_hash appears in the
    // daemon mempool (via mempoolPoll). Tests can call this directly to
    // force the transition. Returns true if a transition happened.
    bool updateTransferBroadcast(const QString& transferId);

    // Mark a tx as orphaned (used when the daemon dies and we cannot
    // determine broadcast status). Stays in GeneratedNotBroadcast; the
    // caller emits transferOrphaned.
    bool markTransferOrphaned(const QString& transferId);

    // Acknowledge + remove a pending transfer (caller = UI: "user has
    // confirmed the transfer is in history; please clear the warning").
    bool clearTransfer(const QString& transferId);

    // -- manual control -----------------------------------------------------

    // Force a mempool-poll tick right now (used by tests).
    void mempoolPollTick();

    // Reset to Idle (only valid from RecoveryExhausted; no-op otherwise).
    void reset();

signals:
    // Top-level coordinator state transitions.
    void stateChanged(judecoin_wallet::core::recovery::CrashRecoveryManager::State state);

    // Sub-decision 3 / 4: daemon is up / down.
    // daemonDown carries the reason for the UI to display in the modal.
    void daemonDown(const QString& reason);
    void daemonUp();

    // Sub-decision 1: wallet-rpc is up / down.
    void walletRpcDown(const QString& reason);
    void walletRpcUp();

    // Sub-decision 2: PendingTxState transitions.
    // emitted whenever any tracked transfer moves through its 3-state
    // machine. `transferId` matches the value passed to trackTransferSplit.
    void pendingTxStateChanged(const QString& transferId,
                               judecoin_wallet::core::recovery::PendingTxState::State state,
                               const QString& txHash);

    // Sub-decision 2: tx_hash was generated but we cannot confirm broadcast
    // (daemon died or wallet-rpc died before mempool poll succeeded).
    // The UI should surface a warning specific to this transfer.
    void transferOrphaned(const QString& transferId,
                          const QString& txHash);

    // Sub-decision 7: restart was scheduled by ProcessManager.
    void recoveryScheduled(judecoin_wallet::core::process::ErrorClassifier::Category category,
                           int delayMs);

    // Sub-decision 7: restart budget exhausted.
    void recoveryExhausted(judecoin_wallet::core::process::ErrorClassifier::Category category,
                           const QString& reason);

    // True when the caller should lock wallet-modify operations (transfer,
    // subaddress create, address-book write). Distinct from the
    // ProcessManager::lockOperations() signal: this one fires also when the
    // daemon is down (sub-decision 3).
    void lockModifyOperations(const QString& reason);
    void unlockModifyOperations();

private slots:
    // ProcessManager observation slots.
    void onDaemonStateChanged(judecoin_wallet::core::process::ProcessManager::State s,
                              bool isHealthy);
    void onWalletRpcStateChanged(judecoin_wallet::core::process::ProcessManager::State s,
                                 bool isHealthy);
    void onLockOperations(const QString& reason);
    void onUnlockOperations();
    void onRestartScheduled(int delayMs,
                            judecoin_wallet::core::process::ErrorClassifier::Category category);
    void onRestartExhausted(judecoin_wallet::core::process::ErrorClassifier::Category category,
                            const QString& reason);

    // Daemon-mempool poll tick. Looks up every tracked tx_hash; if any is
    // missing for kMempoolPollInterval * kMempoolMissesBeforeOrphan ticks,
    // emits transferOrphaned. If present, advances the row to
    // GeneratedAndBroadcast.
    void onMempoolPollTick();

    // Grace timer ticks for walletRpcWaitForGrace / daemonRecovering.
    void onGraceTick();

private:
    void setState(State newState);

    // Daemon RPC helper - polls for tx_hash presence. Returns true if the
    // daemon reports the tx in mempool or blocks. The implementation issues
    // daemon.get_info (cheap) + a get_transfers probe when needed. If the
    // daemon is unreachable we report false (caller should NOT mark
    // broadcast; instead wait for daemon to come back).
    bool probeDaemonForTxHash(const QString& txHash);

    // Helper: derive a stable PendingTxState::State for the given
    // wallet-rpc call stage. Used by callers that do not want to peek the
    // PendingTxState directly.
    static PendingTxState::State classifyPendingFromState(State s);

    // O(1) lookup helper.
    //
    // gf-006 (P0): previously this returned `PendingTxState*` into the
    // `pending_` QHash. The QHash may rehash on insert/erase (or move entries
    // internally), so any raw pointer handed back here could dangle the
    // moment another code path mutates `pending_` - classic use-after-free.
    //
    // We now return `std::optional<std::reference_wrapper<PendingTxState>>`.
    // The `optional` makes the "may be absent" semantics explicit in the
    // type system (replacing `nullptr` checks); the `reference_wrapper` keeps
    // it a true reference so callers can mutate the entry without copying
    // it out (which would break state-machine invariants, since callers
    // mutate in place).
    //
    // Note: std::optional<std::reference_wrapper<T>> is the standard C++17
    // way to express "reference or nothing" - std::optional<T&> itself is
    // not part of C++17. The project pins CMAKE_CXX_STANDARD 17.
    //
    // Lifetime contract: callers MUST use the returned reference within the
    // same expression / function call (no storage across mutating operations
    // on `pending_`). All three current call sites
    // (updateTransferGenerated, updateTransferBroadcast, markTransferOrphaned)
    // satisfy this contract.
    std::optional<std::reference_wrapper<PendingTxState>>
    mutablePending(const QString& transferId);

    // Dependency objects (weak pointers - we never own them).
    judecoin_wallet::core::process::ProcessManager* processManager_ = nullptr;
    judecoin_wallet::core::rpc::DaemonClient*      daemon_          = nullptr;

    // In-flight transfer map.
    QHash<QString, PendingTxState> pending_;

    // Mempool poll bookkeeping: tx_hash -> consecutive miss count.
    QHash<QString, int> mempoolMissCount_;
    static constexpr int kMempoolMissesBeforeOrphan = 5;

    State state_ = State::Idle;
    QString lastError_;
    judecoin_wallet::core::process::ErrorClassifier::Category lastCategory_ =
        judecoin_wallet::core::process::ErrorClassifier::Category::Unknown;

    // Active timers (owned).
    QTimer* mempoolTimer_ = nullptr;
    QTimer* graceTimer_   = nullptr;

    Q_DISABLE_COPY(CrashRecoveryManager)
};

} // namespace judecoin_wallet::core::recovery

#endif // JUDECOIN_WALLET_CORE_RECOVERY_CRASH_RECOVERY_MANAGER_H
