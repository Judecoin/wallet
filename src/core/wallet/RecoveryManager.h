// =============================================================================
// Judecoin Wallet - RecoveryManager (st-012)
//
// Case 3 (docs/UI_DETAILS_TODO.md §Case 3): recover a wallet from a 25-word
// monero mnemonic seed. The UI's responsibility is:
//
//   1. Collect a new password from the user (V5.4.7 - never read .keys).
//   2. Validate the 25-word mnemonic against the monero English wordlist
//      (1626 words, embedded in MnemonicWordList).
//   3. Run a "daemon check #1": daemon is running + has at least one
//      block (synchronised is not strictly required at this point - the
//      scan is going to happen anyway).
//   4. Forward the seed to wallet-rpc via `generate_from_keys` (with the
//      seed field populated). wallet-rpc re-derives keys from the seed
//      ("daemon check #2" - chain scan for matching UTXOs - happens
//      internally inside the RPC; success = seed is valid + wallet has
//      on-chain activity).
//   5. Poll daemon.get_info during the scan and emit progressUpdated
//      (current height / target height) so the UI can show
//      "Scanning block N / total" per V5.4.4 decision A.
//
// V5.4.4 sub-decisions implemented:
//   - 1 text box (paste) - the UI binds to validateMnemonic(QString)
//   - B: validate only after all 25 entered (no per-word UI chatter)
//   - A: show concrete progress during chain scan
//
// V5.4.7: wallet-rpc is the sole encryptor / key-deriver. The UI never
// derives keys from the seed and never touches .keys files. We forward
// the seed string verbatim to wallet-rpc.
//
// WalletManager is used to obtain the underlying WalletRpcClient. This
// keeps a single source of truth for the "currently-open wallet" and
// the state machine that WalletManager already maintains. When recovery
// completes, the newly-created wallet is also marked Open in
// WalletManager - the UI does not need to call open_wallet afterwards.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_WALLET_RECOVERY_MANAGER_H
#define JUDECOIN_WALLET_CORE_WALLET_RECOVERY_MANAGER_H

#include "core/rpc/OtherApi.h"
#include "core/rpc/WalletRpcClient.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <memory>

#include "../util/SecureBuffer.h"

class QTimer;

namespace judecoin_wallet::core::rpc {
class DaemonClient;
}

namespace judecoin_wallet::core::wallet {
class WalletManager;
class MnemonicWordList;
}

namespace judecoin_wallet::core::wallet {

// -----------------------------------------------------------------------------
// MnemonicValidationResult
//
// Outcome of MnemonicWordList::splitWords + isValidEnglishWord for a
// free-form mnemonic string. ok=true iff exactly 25 non-empty words
// AND every word is in the embedded English wordlist.
// -----------------------------------------------------------------------------
struct MnemonicValidationResult
{
    /// True if exactly 25 words and all are valid monero English words.
    bool ok = false;

    /// Human-readable failure reason (empty when ok=true).
    /// Examples: "Expected 25 words, got 24",
    ///           "Word 7 is not in the monero English wordlist".
    QString reason;

    /// Parsed words (lowercased, trimmed). Length == wordCount when
    /// ok=true; may be < 25 if input was malformed.
    QStringList words;

    /// Indices (0-based) of words that failed the wordlist check.
    /// Only populated when ok=false AND wordCount == 25. Empty when
    /// the failure is "wrong word count" or "empty input".
    QVector<int> invalidIndices;

    /// Number of parsed words (0 if input was empty).
    int wordCount = 0;
};

// -----------------------------------------------------------------------------
// RecoveryManager
//
// High-level orchestrator for Case 3 mnemonic recovery. Owns / borrows a
// WalletRpcClient + DaemonClient and drives the multi-step state machine.
// Emits signals for QML to bind to (state, progress, completion / failure).
// -----------------------------------------------------------------------------
class RecoveryManager : public QObject
{
    Q_OBJECT

public:
    /// State machine for the recovery flow.
    enum class State {
        Idle,                 // not in any recovery flow
        CheckingDaemon,       // daemon check #1 in progress
        Generating,           // calling wallet-rpc.generate_from_keys
        ScanningBlockchain,   // wallet-rpc is scanning chain (progress poll)
        Complete,             // success; currentFilename is the new wallet
        Failed                // last step failed; see lastError()
    };
    Q_ENUM(State)

    /// Inputs for startRecovery[Async].
    ///
    /// Two shapes are supported; they are mutually exclusive (the parser
    /// or wizard enforces this):
    ///
    ///   Case 3 (mnemonic):
    ///     mnemonic must be non-empty. address/spendkey/viewkey MUST be
    ///     empty (wallet-rpc derives them from the seed).
    ///
    ///   Case 4 (backup restore, st-022 B-template):
    ///     mnemonic MUST be empty. address + spendkey + viewkey come from
    ///     BackupParser output and are forwarded verbatim. restore_height
    ///     is optional (0 = full scan from genesis).
    struct Config {
        /// New wallet filename.
        QString filename;
        /// New wallet password (V5.4.7 - never persisted, never logged).
        QString password;       // SENSITIVE (P0 #10)

        // -- Case 3 (seed recovery) ------------------------------------
        /// 25-word seed (any whitespace-separated form). Non-empty means
        /// Case 3; must be empty in Case 4.
        QString mnemonic;       // SENSITIVE (P0 #10)
        // -- Case 4 (backup restore) -----------------------------------
        /// Public address from the backup. Case 4 only.
        QString address;
        /// Private view key (hex). Case 4 only.
        QString viewkey;        // SENSITIVE (P0 #10)
        /// Private spend key (hex). Case 4 only.
        QString spendkey;       // SENSITIVE (P0 #10)
        /// Optional restore-height hint from the backup.
        quint64 restore_height = 0;

        /// Common
        bool autosave_current = false;

        /// Heuristic: returns true if this Config looks like a Case 4
        /// (backup) restore (spendkey + viewkey + address all set,
        /// mnemonic empty). The wizard / QML layer should set exactly
        /// one shape; this helper lets callers sanity-check before
        /// calling startRecovery.
        bool isCase4Backup() const {
            return mnemonic.isEmpty() &&
                   !spendkey.isEmpty() &&
                   !viewkey.isEmpty() &&
                   !address.isEmpty();
        }
        bool isCase3Mnemonic() const {
            return !mnemonic.isEmpty() &&
                   spendkey.isEmpty() &&
                   viewkey.isEmpty() &&
                   address.isEmpty();
        }

        /// Zero sensitive QString fields when this Config leaves scope.
        /// filename + address + restore_height + autosave_current are
        /// intentionally NOT scrubbed: filename is only the wallet name,
        /// address is a public on-chain address, and the remaining fields
        /// are numeric/bool (no heap string storage).
        ~Config()
        {
            judecoin_wallet::core::util::secureClear(password);
            judecoin_wallet::core::util::secureClear(mnemonic);
            judecoin_wallet::core::util::secureClear(viewkey);
            judecoin_wallet::core::util::secureClear(spendkey);
        }
    };

    explicit RecoveryManager(QObject* parent = nullptr);
    RecoveryManager(judecoin_wallet::core::rpc::WalletRpcClient* walletRpc,
                    judecoin_wallet::core::rpc::DaemonClient* daemon,
                    QObject* parent = nullptr);
    RecoveryManager(judecoin_wallet::core::wallet::WalletManager* walletManager,
                    judecoin_wallet::core::rpc::DaemonClient* daemon,
                    QObject* parent = nullptr);
    ~RecoveryManager() override;

    // -- pure helpers (no RPC, no state change) ------------------------------

    /// Parse + validate a free-form mnemonic string. Does not call any
    /// RPC; safe to invoke from QML's onTextChanged. V5.4.4 B says
    /// the UI binds this only after the user clicks "Validate" / "Restore"
    /// (i.e. after the 25th word is entered) - not on every keystroke.
    Q_INVOKABLE static MnemonicValidationResult validateMnemonic(const QString& mnemonic);

    // -- accessors -----------------------------------------------------------

    State state() const { return state_; }
    QString lastError() const { return lastError_; }
    QString lastRpcMethod() const { return lastRpcMethod_; }
    int progressCurrent() const { return progressCurrent_; }
    int progressTotal() const { return progressTotal_; }
    QString currentFilename() const { return currentFilename_; }
    judecoin_wallet::core::rpc::WalletRpcClient* walletRpc() const { return walletRpc_; }
    judecoin_wallet::core::rpc::DaemonClient* daemon() const { return daemon_; }

    // -- synchronous API -----------------------------------------------------

    /// Run the entire Case 3 recovery flow synchronously. Throws
    /// RpcException if any RPC step fails. Throws std::runtime_error
    /// for local validation failures (bad wordlist, wrong word count).
    /// On success: state == Complete, currentFilename == config.filename.
    void startRecovery(const Config& config);

    /// Move to Idle. Safe to call from any state except Complete (where
    /// it is a no-op so the UI can present the new wallet first). If
    /// called mid-flow (CheckingDaemon / Generating / ScanningBlockchain)
    /// any in-flight RPC will still complete; we just stop emitting
    /// progress updates for it.
    Q_INVOKABLE void cancel();

    // -- asynchronous API ----------------------------------------------------

    /// Async version of startRecovery. Returns a correlation id used
    /// to match the recoveryResponse / recoveryFailed signals.
    /// Transitions through CheckingDaemon -> Generating -> ScanningBlockchain
    /// -> Complete / Failed, emitting stateChanged at each transition.
    Q_INVOKABLE QString startRecoveryAsync(const QVariantMap& qmlConfig,
                                            const QString& correlationId = {});

    /// Internal async path (C++ only). Called by the Q_INVOKABLE
    /// QVariantMap overload after converting the QML config.
    QString startRecoveryAsync(const Config& config,
                                const QString& correlationId = {});

signals:
    /// Emitted whenever state transitions. Carries the new state.
    void stateChanged(judecoin_wallet::core::wallet::RecoveryManager::State state);

    /// Emitted during ScanningBlockchain at each poll tick (every 1s by
    /// default). current is the wallet-rpc-reported wallet_height (or
    /// daemon height if wallet is unavailable); total is daemon.height.
    void progressUpdated(int current, int total);

    /// Emitted once on successful recovery. Carries the new wallet's
    /// filename + openedAt ms (epoch milliseconds, same convention as
    /// WalletManager::walletOpened).
    void recoveryComplete(const QString& filename,
                          qint64 openedAtMs,
                          const QString& correlationId);

    /// Emitted once on any failure path. `reason` is human-readable;
    /// `rpcMethod` is the wallet-rpc method name if the failure
    /// originated in an RPC ("generate_from_keys"), or empty for
    /// local validation / daemon failures.
    void recoveryFailed(const QString& reason,
                        const QString& rpcMethod,
                        const QString& correlationId);

private slots:
    void onGenerateKeysResponse(const QString& correlationId,
                                 const QString& method,
                                 const QJsonObject& result);
    void onGenerateKeysFailed(const QString& correlationId,
                               const QString& method,
                               const judecoin_wallet::core::rpc::RpcException error);
    void onProgressTick();

private:
    void setState(State newState);
    void setError(const QString& reason, const QString& rpcMethod = {});

    /// Step 1 (local, never fails via RPC): validate mnemonic.
    /// Returns true if valid; populates outWords with the 25 words.
    bool doValidate(const QString& mnemonic,
                    MnemonicValidationResult* outResult);

    /// Step 1b (st-022, local): validate that a Case 4 backup Config has
    /// a consistent shape (spendkey + viewkey + address all non-empty,
    /// mnemonic empty). Returns true if valid; populates outReason with
    /// a human-readable explanation when false. We do not re-check
    /// field syntax here - BackupParser already enforced that.
    static bool validateCase4Config(const Config& config,
                                    QString* outReason);

    /// Step 2 (daemon RPC): check daemon is reachable + height > 0.
    /// Throws RpcException on failure.
    void doCheckDaemon();

    /// Step 3 (wallet-rpc RPC): call generate_from_keys with seed.
    /// Throws RpcException on failure.
    void doGenerate(const Config& config);

    /// Step 4: start polling daemon.get_info for chain progress.
    void doStartProgressPoll();
    void doStopProgressPoll();

    /// Build the GenerateFromKeysRequest from a Config. Branches on
    /// Config shape (Case 3 seed vs Case 4 backup). Forwards exactly the
    /// fields wallet-rpc expects, with the documented empty/zero
    /// defaults when not used.
    static judecoin_wallet::core::rpc::GenerateFromKeysRequest
    buildRequest(const Config& config);

    /// qint64 epoch milliseconds, same helper as WalletManager uses.
    static qint64 nowMs();

    /// Convert a QVariantMap (from QML) into a Config struct.
    static Config configFromMap(const QVariantMap& map);

    /// Resolve the wallet filename associated with a correlation id that
    /// we sent out via startRecoveryAsync(). Used by onGenerateKeysResponse
    /// to populate currentFilename_ when the async RPC completes. For
    /// V1.0 there is at most one in-flight async recovery at a time, so
    /// we track the pending filename directly (no map needed).
    QString resolvePendingFilename(const QString& correlationId);

    // ---- dependencies (owned iff constructed without external ptr) -------
    judecoin_wallet::core::rpc::WalletRpcClient* walletRpc_ = nullptr;
    bool ownsWalletRpc_ = true;
    judecoin_wallet::core::rpc::DaemonClient* daemon_ = nullptr;
    bool ownsDaemon_ = true;

    // st-150 (2026-07-24): stored so onGenerateKeysResponse can
    // call walletManager_->notifyExternalOpen() after a successful
    // restore_deterministic_wallet RPC. The pointer is non-owning;
    // WalletManager outlives RecoveryManager (both are stack vars in
    // main.cpp, WalletManager declared first).
    judecoin_wallet::core::wallet::WalletManager* walletManager_ = nullptr;

    State state_ = State::Idle;
    QString lastError_;
    QString lastRpcMethod_;
    int progressCurrent_ = 0;
    int progressTotal_ = 0;
    QString currentFilename_;

    QTimer* progressTimer_ = nullptr;
    QString pendingCorrelationId_;
    QString pendingFilename_;

    Q_DISABLE_COPY(RecoveryManager)
};

} // namespace judecoin_wallet::core::wallet

#endif // JUDECOIN_WALLET_CORE_WALLET_RECOVERY_MANAGER_H
