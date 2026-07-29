// =============================================================================
// Judecoin Wallet - WalletManager (st-011)
//
// V5.4.7 design:
//   The UI never reads .keys files. Wallet-rpc is the sole encryptor /
//   decryptor. WalletManager forwards user-provided passwords verbatim to
//   wallet-rpc through the wallet-rpc "OtherApi" envelope (create_wallet /
//   open_wallet / close_wallet / change_wallet_password / generate_from_keys
//   / generate_from_json). WalletManager itself never touches .keys on disk.
//
// In-memory state (cached for the currently-open wallet only):
//   - filename       : the basename wallet-rpc currently has open.
//   - state          : State::Closed | State::Open
//   - opened_at_ms   : clock() at the time of the most recent successful open,
//                      used by stale-close heuristics and audit.
//   - last_error_msg : human-readable last failure (cleared on each call).
//
// V1.0 does NOT cache the password (V5.4.7 forbids it). The UI is expected
// to re-prompt on each open/change operation.
//
// WalletManager wraps WalletRpcClient for the 6 wallet-lifecycle RPCs. The
// pattern follows WalletRpcClient itself: sync methods throw RpcException;
// async methods return a correlation id and emit walletResponse /
// walletFailed. In addition, WalletManager surfaces higher-level signals
// (walletOpened, walletClosed, stateChanged, currentWalletChanged) so QML
// can drive the UI from a single observable object.
//
// V1.0 RPC surface wrapped:
//   create_wallet              (filename, password, language)
//   open_wallet                (filename, password)
//   close_wallet               (filename, optional - closes current)
//   change_wallet_password     (old, new, filename)
//   generate_from_keys         (filename, password, address, viewkey)
//   generate_from_json         (filename, password, json)
//
// Storage policy (V5.4.7): no write to disk beyond what wallet-rpc itself
// does under ~/.judecoin/wallets/<name>.keys. WalletManager only keeps
// the filename + state in process memory.
//
// V5.4.7 + gemma4-p0-fixes / gf-007 ownership semantics:
//
//   The previous design stored the wrapped client as a raw pointer
//   (`WalletRpcClient* rpc_`) plus a boolean ownership flag
//   (`bool ownsRpc_`). Two constructors (default + injection) took
//   different code paths through the destructor:
//
//     - default ctor:        `rpc_(new WalletRpcClient(this))` + ownsRpc_=true
//     - injection ctor:      `rpc_(client)` + ownsRpc_=false
//     - dtor:                `if (ownsRpc_) delete rpc_;`
//
//   This was the classic "dual ownership" anti-pattern flagged by the
//   gemma4:12b audit (P0): the ownership flag is state that has to be
//   kept in sync with the pointer across every code path, and any
//   refactor that drops a call site's `new`/`delete` can silently leak
//   or double-delete. It also collided with Qt's own QObject
//   parent-child mechanism: the default ctor passed `this` as the
//   WalletRpcClient's Qt parent (so Qt would auto-delete), AND
//   tracked ownsRpc_=true (so the dtor would also delete). Both
//   mechanisms agreed on deletion in the happy path but disagreed
//   about who-did-it.
//
//   Replaced with QSharedPointer<WalletRpcClient>:
//
//     - `rpc_` is a single QSharedPointer; ownership is *type-enforced*
//       rather than tracked via a separate bool.
//     - The default ctor creates an owned client via
//       `QSharedPointer<WalletRpcClient>::create()`; no Qt parent is
//       set on the inner WalletRpcClient so there is no longer any
//       double-management between Qt parent-child and the shared
//       pointer.
//     - The injection ctor accepts a `QSharedPointer<WalletRpcClient>`
//       from the caller; the caller and WalletManager now co-own the
//       client via Qt's standard shared-ownership semantics. This
//       also fixes the lifetime coupling to RecoveryManager (which
//       previously stored a borrowed raw `WalletRpcClient*` extracted
//       via `walletManager->rpc()` - safe only as long as the
//       WalletManager outlived the RecoveryManager).
//     - The destructor is implicit: QSharedPointer's last reference
//       dropping runs the deleter. ownsRpc_ is gone.
//
//   Public-API impact (intentional, required by spec):
//     - WalletManager(QSharedPointer<WalletRpcClient> client, ...)
//     - rpc() returns QSharedPointer<WalletRpcClient>
//
//   Callers updated:
//     - tests/test_wallet_manager/test_wallet_manager.cpp  (fixture now
//       owns `client_` as a QSharedPointer)
//     - src/core/wallet/RecoveryManager.cpp                (the
//       constructor that takes a WalletManager* extracts the raw
//       pointer via `.data()` for its still-raw `walletRpc_` field)
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_WALLET_WALLET_MANAGER_H
#define JUDECOIN_WALLET_CORE_WALLET_WALLET_MANAGER_H

#include "core/rpc/OtherApi.h"
#include "core/rpc/WalletRpcClient.h"

#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace judecoin_wallet::core::wallet {

// -----------------------------------------------------------------------------
// WalletManager
//
// Owns a WalletRpcClient and exposes the 6 wallet-lifecycle RPCs as both
// synchronous (throw RpcException) and asynchronous (signal-emitting)
// methods. Tracks the currently-open wallet name in process memory; the
// wallet-rpc is the source of truth for "is anything open?" (we cannot
// inspect .keys files per V5.4.7).
//
// V5.4.7 + gf-007 ownership: `rpc_` is a QSharedPointer<WalletRpcClient>.
// The two constructors (default / injection) are now distinguished only
// by whether the caller passes a shared client or accepts an internally
// created one; the `ownsRpc_` boolean is gone.
// -----------------------------------------------------------------------------
class WalletManager : public QObject
{
    Q_OBJECT

public:
    /// Coarse state machine for the wallet-rpc subprocess. Mirrors what
    /// wallet-rpc itself tracks internally; we mirror it here so QML can
    /// bind to state without hitting the RPC.
    enum class State {
        Closed,   // no wallet open (initial; also after close_wallet)
        Opening,  // open_wallet RPC in flight
        Open,     // open_wallet RPC succeeded; currentFilename() valid
        Closing,  // close_wallet RPC in flight
        Error,    // last RPC failed; currentFilename() unchanged but UI
                  // should surface lastError() and prevent user actions
    };
    Q_ENUM(State)

    /// Default ctor: WalletManager creates and owns its own
    /// WalletRpcClient. Lifetime is fully managed by the
    /// QSharedPointer inside `rpc_`; the inner WalletRpcClient has no
    /// Qt parent (QSharedPointer owns it).
    explicit WalletManager(QObject* parent = nullptr);

    /// Injection ctor: the caller passes a QSharedPointer
    /// WalletRpcClient. WalletManager and the caller co-own the client.
    /// The caller can safely hold its own QSharedPointer alongside
    /// WalletManager's; the underlying WalletRpcClient is destroyed
    /// exactly once, when the last reference drops.
    ///
    /// Rationale (gf-007): the previous design (`WalletRpcClient*`,
    /// caller owns) coupled RecoveryManager's lifetime to the
    /// WalletManager's. With QSharedPointer, callers can hold a strong
    /// reference independent of WalletManager's lifetime, eliminating
    /// the use-after-free window if RecoveryManager outlived
    /// WalletManager.
    explicit WalletManager(QSharedPointer<judecoin_wallet::core::rpc::WalletRpcClient> client,
                           QObject* parent = nullptr);
    ~WalletManager() override;

    // -- accessors ------------------------------------------------------------

    /// The wrapped wallet-rpc client, as a QSharedPointer. Callers that
    /// need to extend the client's lifetime (e.g. RecoveryManager)
    /// should hold this shared pointer rather than extracting a raw
    /// pointer via `.data()` - the latter is safe only while
    /// WalletManager itself is alive AND the returned shared pointer
    /// is still in scope.
    QSharedPointer<judecoin_wallet::core::rpc::WalletRpcClient> rpc() const { return rpc_; }

    // FIX-G3b (2026-07-18): Q_PROPERTY so QML can read wallet state.
    // Without Q_PROPERTY, QML evaluates `walletManager.state` as undefined
    // (state() is a plain C++ method, not exposed to the QML engine).
    // The State enum is already Q_ENUM-registered and accessible as
    // WalletManager.Closed / WalletManager.Opening / WalletManager.Open /
    // WalletManager.Closing / WalletManager.Error in QML.
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentFilename READ currentFilename NOTIFY currentWalletChanged)
    Q_PROPERTY(qint64 openedAtMs READ openedAtMs CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY walletFailed)

    /// Current state (Closed / Opening / Open / Closing / Error).
    State state() const { return state_; }

    /// Filename of the currently-open wallet (empty if Closed/Error).
    QString currentFilename() const { return currentFilename_; }

    /// Wall-clock milliseconds since epoch when open_wallet last succeeded.
    qint64 openedAtMs() const { return openedAtMs_; }

    /// Last error message (empty on success). Cleared at the start of each
    /// wallet-lifecycle RPC.
    QString lastError() const { return lastError_; }

    // -- synchronous API ------------------------------------------------------

    /// create_wallet. Creates a new wallet file via wallet-rpc and leaves
    /// it OPEN. On success returns the new wallet's filename; on failure
    /// throws RpcException and state transitions to Error.
    /// Q_INVOKABLE: exposed to QML via meta-object system. Case1Create.qml:772
    /// calls page.walletManager.createWallet(...). See audit/2026-07-17/
    /// 2026-07-17-fix-f-qml-binding-missing.md.
    Q_INVOKABLE QString createWallet(const QString& filename,
                                     const QString& password,
                                     const QString& language = QStringLiteral("English"));

    // FIX-G10 (2026-07-17 21:24): seed-aware variant. When `seed`
    // is non-empty the call routes to wallet-rpc `generate_from_keys`
    // so the wallet is created FROM a client-generated 24-word mnemonic
    // (which the UI then displays + copies). Empty seed -> legacy path
    // (see the 3-arg overload below) which calls `create_wallet`
    // server-side seed, no UI exposure. See audit/2026-07-17/
    // 2026-07-17-mnemonic-24-words-and-copy.md.
    Q_INVOKABLE QString createWallet(const QString& filename,
                                     const QString& password,
                                     const QString& seed,
                                     const QString& language);

    /// query_key "mnemonic" (st-151). Returns the wallet's 25-word
    /// mnemonic seed from wallet-rpc. Must be called AFTER the wallet
    /// is open (i.e. after createWallet or openWallet succeeds).
    Q_INVOKABLE QString getMnemonicSeed();

    /// query_key "spend_key". Returns the wallet's secret spend key (64-char
    /// hex). Must be called AFTER the wallet is open.
    Q_INVOKABLE QString getSpendKey();

    /// query_key "view_key". Returns the wallet's secret view key (64-char
    /// hex). Must be called AFTER the wallet is open.
    Q_INVOKABLE QString getViewKey();

    /// get_address. Returns the wallet's primary address.
    Q_INVOKABLE QString getWalletAddress();

    /// get_height. Returns the current block height. Used as restore_height
    /// when exporting keys for Case4 import.
    Q_INVOKABLE qint64 getWalletHeight();

    /// FIX-G11 (2026-07-18): lists wallet files in the wallet
    /// directory by scanning for *.keys files and stripping the suffix.
    /// Returns an empty list when walletDir is not set or has no .keys.
    Q_INVOKABLE QStringList listWalletFiles() const;

    /// FIX-G11: set / get the directory where wallet-rpc stores .keys files.
    /// Set from main.cpp after the data directory is resolved.
    Q_INVOKABLE void setWalletDir(const QString& dir);
    Q_INVOKABLE QString walletDir() const;

    /// open_wallet. V5.4.7: single password verifier; UI never reads .keys.
    /// Throws RpcException with code -12 on wrong password.
    /// Q_INVOKABLE: exposed to QML via meta-object system. Case2Open.qml:496
    /// calls page.walletManager.openWallet(...). See audit/2026-07-17/
    /// 2026-07-17-fix-f-qml-binding-missing.md.
    Q_INVOKABLE void openWallet(const QString& filename, const QString& password);

    /// V5.4.7 + st-061: QML-friendly password verifier for the ConfirmModal.
    /// Returns true if `password` decrypts the currently-open wallet's
    /// .keys file successfully, false otherwise. Does NOT throw on wrong
    /// password (returns false + sets lastError() to the underlying
    /// RpcException's message so the QML side can show
    /// qsTr("Incorrect password")). Wall clock is ~0.1s -- the wallet-rpc
    /// open_wallet roundtrip + the local KDF cost.
    ///
    /// Implementation note: wallet-rpc does not expose a non-destructive
    /// "verify password" RPC, so verifyPassword re-issues open_wallet
    /// against the currently-open wallet. On success the wallet is closed
    /// and immediately re-opened (state goes Opening -> Open). On a wrong
    /// password the wallet ends up closed (state Error) -- the caller
    /// should treat the return value as authoritative and NOT trust
    /// state() in the same tick. For V1.0 the user-facing flow is
    /// (SendPage Submit) -> ConfirmModal -> (correct password) -> real
    /// transfer, so the close+reopen window is acceptable; a dedicated
    /// verify-only RPC is a future-st-060 follow-up if profiling shows
    /// the roundtrip is too slow for the post-confirm transfer flow.
    ///
    /// Returns false (without an RPC roundtrip) when no wallet is open,
    /// so the modal can be mounted on the Onboarding/closed-wallet path
    /// without crashing.
    Q_INVOKABLE bool verifyPassword(const QString& password);

    /// FIX-G29 (2026-07-18): Q_INVOKABLE transfer_split so QML can
    /// call walletManagerObj.transferSplit(...) from main.qml's
    /// onSubmitClicked handler. Wraps the underlying
    /// WalletRpcClient::transferSplit. Returns a JSON object with
    ///   { ok: bool, tx_hash: string, error: string }
    /// so QML can switch on the ok flag and surface the tx_hash or
    /// error message via Toast.
    ///
    /// Parameters (all strings for QML compat):
    ///   fromAddress   - sender's base58 subaddress
    ///   toAddress     - recipient's base58 address
    ///   amountAtomic  - amount in atomic units (as string, parsed to quint64)
    ///   priority      - 0=Low / 1=Normal / 2=Elevated / 3=High (as int)
    Q_INVOKABLE QJsonObject transferSplit(const QString& fromAddress,
                                           const QString& toAddress,
                                           const QString& amountAtomic,
                                           int priority);

    /// close_wallet. Releases the wallet-rpc-side handle and clears our
    /// in-memory state. Idempotent: closing when nothing is open is a no-op
    /// (state stays Closed).
    void closeWallet();

    /// change_wallet_password. Re-encrypts the .keys file in place via
    /// wallet-rpc. Throws RpcException with code -12 on wrong old password.
    void changeWalletPassword(const QString& old_password,
                              const QString& new_password);

    /// generate_from_keys. Restore / view-only variant. Creates + opens a
    /// new wallet derived from address + viewkey. Throws RpcException on
    /// any failure (invalid address, view key mismatch, etc.).
    QString generateFromKeys(const QString& filename,
                             const QString& password,
                             const QString& address,
                             const QString& viewkey);

    /// generate_from_keys - st-022 Case 4 (B-template backup restore).
    /// Same RPC but with the optional spendkey + restore_height +
    /// autosave_current fields populated. The UI typically gets here
    /// after a BackupParser parse yielded ok=true; the parsed fields
    /// are forwarded verbatim to wallet-rpc (V5.4.7: UI never derives
    /// keys). When spendkey is empty this behaves exactly like the
    /// view-only overload above.
    /// Q_INVOKABLE: exposed to QML via meta-object system. Case4BackupRestore.qml:394
    /// calls page.walletManager.generateFromKeysBackup(...). See audit/2026-07-17/
    /// 2026-07-17-fix-f-qml-binding-missing.md.
    Q_INVOKABLE QString generateFromKeysBackup(const QString& filename,
                                                const QString& password,
                                                const QString& address,
                                                const QString& viewkey,
                                                const QString& spendkey,
                                                quint64 restore_height = 0,
                                                bool autosave_current = false);

    /// P0 #12: QML-callable restore from a key-backup file using a C++-side
    /// token instead of passing spendkey/viewkey through JS strings.
    ///
    /// The token was created by BackupParserFacade::parseBackupFile(); it
    /// holds the parsed spendkey/viewkey in BackupParserTokenStore. This method
    /// consumes the token, builds a GenerateFromKeysRequest, calls
    /// wallet-rpc generate_from_keys, and scrubs every local copy of the
    /// sensitive fields before returning.
    ///
    /// Returns a QVariantMap { ok: bool, filename: string, error: string }.
    /// On failure `ok` is false and `error` contains a human-readable message;
    /// the caller should NOT parse exceptions from this QML-facing method.
    Q_INVOKABLE QVariantMap restoreFromBackupByToken(int tokenId,
                                                    const QString& password,
                                                    const QString& filename);

    /// generate_from_json. Advanced restore from a JSON snapshot. Forwarded
    /// verbatim to wallet-rpc (UI does not parse or store the JSON).
    /// Throws RpcException on any failure.
    QString generateFromJson(const QString& filename,
                             const QString& password,
                             const QString& jsonString);

    // -- asynchronous API -----------------------------------------------------

    /// Same as the sync createWallet but returns the correlation id and
    /// emits walletOpened() + walletResponse() on completion.
    QString createWalletAsync(const QString& filename,
                              const QString& password,
                              const QString& language = QStringLiteral("English"),
                              const QString& correlationId = {});

    /// Async open. Emits walletOpened() on success; walletFailed() on error.
    QString openWalletAsync(const QString& filename,
                            const QString& password,
                            const QString& correlationId = {});

    /// Async close. Emits walletClosed() on success.
    QString closeWalletAsync(const QString& correlationId = {});

    /// Async change. Emits walletResponse() with method "change_wallet_password".
    QString changeWalletPasswordAsync(const QString& old_password,
                                      const QString& new_password,
                                      const QString& correlationId = {});

    /// Async generate_from_keys. Emits walletOpened() on success.
    QString generateFromKeysAsync(const QString& filename,
                                  const QString& password,
                                  const QString& address,
                                  const QString& viewkey,
                                  const QString& correlationId = {});

    /// Async Case 4 (st-022) backup-restore variant. Same semantics as
    /// generateFromKeysBackup() but routed through the async pipeline.
    QString generateFromKeysBackupAsync(const QString& filename,
                                          const QString& password,
                                          const QString& address,
                                          const QString& viewkey,
                                          const QString& spendkey,
                                          quint64 restore_height = 0,
                                          bool autosave_current = false,
                                          const QString& correlationId = {});

    /// Async generate_from_json. Emits walletOpened() on success.
    QString generateFromJsonAsync(const QString& filename,
                                  const QString& password,
                                  const QString& jsonString,
                                  const QString& correlationId = {});

    // -- recovery integration (st-150, 2026-07-24) --------------------

    /// Called by RecoveryManager when restore_deterministic_wallet succeeds
    /// externally (i.e. not through WalletManager's own open methods).
    /// Sets state to Open, updates currentFilename/openedAtMs, and emits
    /// walletOpened so all the downstream wiring (BalanceTracker,
    /// SubaddressManager, TransactionTracker) refreshes automatically.
    /// This is the bridge between RecoveryManager's async recovery flow
    /// and WalletManager's state machine.
    Q_INVOKABLE void notifyExternalOpen(const QString& filename,
                                        qint64 openedAtMs);

signals:
    /// Emitted whenever state transitions. Carries the new state.
    void stateChanged(judecoin_wallet::core::wallet::WalletManager::State state);

    /// Emitted whenever the currently-open wallet name changes (open /
    /// close / generate-from-...). Carries the new filename, or empty
    /// string if no wallet is currently open.
    void currentWalletChanged(const QString& filename);

    /// Convenience signal: a wallet has just been opened (sync or async
    /// path). UI hooks this to refresh balance / history views.
    void walletOpened(const QString& filename, qint64 openedAtMs);

    /// Convenience signal: a wallet has just been closed. UI hooks this to
    /// show the "open a wallet" landing screen.
    void walletClosed();

    /// Low-level async success. Mirrors WalletRpcClient::walletResponse
    /// but is re-emitted so consumers can listen on WalletManager only.
    void walletResponse(const QString& correlationId,
                        const QString& method,
                        const QJsonObject& result);

    /// Low-level async failure. Carries the categorised exception.
    void walletFailed(const QString& correlationId,
                      const QString& method,
                      const judecoin_wallet::core::rpc::RpcException error);

private slots:
    void onRpcResponse(const QString& correlationId,
                       const QString& method,
                       const QJsonObject& result);
    void onRpcFailed(const QString& correlationId,
                     const QString& method,
                     const judecoin_wallet::core::rpc::RpcException error);

private:
    /// Set state and emit stateChanged. Centralised so the transition
    /// matrix (Opening->Open, Closed->Opening, Open->Closing, etc.) lives
    /// in one place.
    void setState(State newState);

    /// Set currentFilename_ and emit currentWalletChanged if it changed.
    void setCurrentFilename(const QString& filename);

    /// For use by notifyExternalOpen: set openedAtMs_ without going through
    /// the normal RPC flow. Used when RecoveryManager completes recovery
    /// externally.
    void setOpenedAtMs(qint64 ms) { openedAtMs_ = ms; }

    /// Translate the wallet-rpc method name to the high-level event we
    /// should fire on success (open_wallet -> walletOpened, etc.).
    /// Returns true if the method is one we treat as opening the wallet.
    static bool isOpenMethod(const QString& method);

    /// Translate method -> close event.
    static bool isCloseMethod(const QString& method);

    /// Translate method -> failure event (sets lastError_ from RpcException).
    void recordError(const QString& method,
                     const judecoin_wallet::core::rpc::RpcException& error);

    /// Clear lastError_ on each new outgoing call (state goes Opening /
    /// Closing; previous error is no longer relevant).
    void clearError();

    /// Extract Unix-epoch-ms from QDateTime::currentMSecsSinceEpoch().
    static qint64 nowMs();

    // The wrapped client. Ownership is enforced by the type: the
    // QSharedPointer either uniquely owns (default ctor) or shares
    // ownership with whoever passed in the shared pointer (injection
    // ctor). The previous `bool ownsRpc_` is gone (gf-007).
    QSharedPointer<judecoin_wallet::core::rpc::WalletRpcClient> rpc_;

    State state_ = State::Closed;
    QString currentFilename_;
    qint64 openedAtMs_ = 0;
    QString lastError_;

    /// Filename the caller is about to open via a sync method. Set
    /// immediately before the sync RPC call so that onRpcResponse (which
    /// fires inside the call) can emit walletOpened with the right name.
    /// Cleared after the RPC returns (success or failure). For async
    /// methods, this stays empty; the caller tracks filename via
    /// correlationId mapping.
    QString pendingFilename_;

    /// FIX-G11 (2026-07-18): wallet directory where wallet-rpc stores
    /// .keys files. Set via setWalletDir() from main.cpp.
    QString walletDir_;

    Q_DISABLE_COPY(WalletManager)
};

} // namespace judecoin_wallet::core::wallet

#endif // JUDECOIN_WALLET_CORE_WALLET_WALLET_MANAGER_H