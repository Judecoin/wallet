// =============================================================================
// Judecoin Wallet - WalletRpcClient (st-005)
//
// Thin wrapper around HttpRpcClient that talks JSON-RPC to the wallet-rpc
// subprocess (V5.4 mainnet-only, default port 19071). V1.0 exposes the 30
// wallet-rpc methods documented in devplan/st-005, grouped into 10 modules
// to keep each header scannable:
//
//   BalanceApi      - 6 methods  (get_balance / get_unlocked_balance /
//                                 get_address / get_accounts / get_version /
//                                 get_height)
//   TransferApi     - 3 methods  (transfer_split / sweep_all / sweep_single)
//   HistoryApi      - 3 methods  (get_transfer / get_transfers / get_payments)
//   AddressBookApi  - 3 methods  (get_address_book / add_address_book /
//                                 delete_address_book)
//   UriApi          - 2 methods  (make_uri / parse_uri)
//   TxProofApi      - 4 methods  (get_tx_key / check_tx_key /
//                                 get_tx_proof / check_tx_proof)
//   BalanceProofApi - 2 methods  (get_reserve_proof / check_reserve_proof)
//   MultisigApi     - 2 methods  (make_multisig / is_multisig)
//   FreezeApi       - 1 method   (freeze)
//   OtherApi        - 4 methods  (create_wallet / open_wallet / close_wallet /
//                                 change_wallet_password)
//
// Total: 6 + 3 + 3 + 3 + 2 + 4 + 2 + 2 + 1 + 4 = 30 (matches devplan/st-005).
//
// Method reduction rationale vs. CLI_COMMANDS.md 8.2 (51 methods):
//   - balance_proofs cut from 4 -> 2 (drop get/check_spend_proof; rarely used
//     and shares envelope with reserve proofs)
//   - multisig cut from 6 -> 2 (drop prepare / finalize / sign / submit; the
//     two endpoints exposed cover the setup-and-status question fully)
//   - freeze cut from 3 -> 1 (drop thaw, frozen; thaw reuses the freeze
//     envelope and frozen is read-only state already inferable from history)
//   - tx_proofs cut from 6 -> 4 (drop get_tx_note / set_tx_note per V5.4.5
//     storage_policy - notes are kept in UI-local .json by st-017)
//   - other cut from 14 -> 4 (drop generate_from_keys per V5.4.5 storage
//     policy and the admin/debug endpoints: description / attribute / log /
//     daemon / mining / rescan)
//
// Per devplan/st-005 spec_notes:
//   - All methods throw RpcException on error.
//   - Async API uses Qt signals/slots; correlation id ties completion back
//     to the caller.
//   - WalletRpcClient never reads .keys files (V5.4.7) - wallet-rpc is the
//     sole encryptor / password verifier.
//   - transfer_split ring_size is hardcoded to 16 (current hard-fork; no
//     future-proof knob in V1.0).
//
// Sync methods block; async methods return a correlation id and emit
// walletResponse / walletFailed. The pattern mirrors HttpRpcClient::call
// vs callAsync - we wrap that into a strongly-typed envelope so callers
// never have to parse QJsonObject themselves.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_WALLET_RPC_CLIENT_H
#define JUDECOIN_WALLET_CORE_RPC_WALLET_RPC_CLIENT_H

#include "core/rpc/AddressBookApi.h"
#include "core/rpc/BalanceApi.h"
#include "core/rpc/BalanceProofApi.h"
#include "core/rpc/FreezeApi.h"
#include "core/rpc/HistoryApi.h"
#include "core/rpc/HttpRpcClient.h"
#include "core/rpc/MultisigApi.h"
#include "core/rpc/OtherApi.h"
#include "core/rpc/TransferApi.h"
#include "core/rpc/TxProofApi.h"
#include "core/rpc/UriApi.h"

#include "core/util/SecureBuffer.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

#include <cstdint>
#include <functional>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// WalletRpcClient
//
// All 30 wallet-rpc methods are exposed as synchronous methods.
// They are intentionally not Q_INVOKABLE; QML should access wallet
// functionality through WalletManager's higher-level, reviewed API.
// Each one has an async overload that returns a correlation id and emits
// the walletResponse / walletFailed signal pair. The sync method simply
// wraps HttpRpcClient::call and parses the JSON-RPC result envelope via
// the corresponding <X>Api parser.
//
// Naming convention:
//   - snake_case wallet-rpc method name (getBalance -> "get_balance")
//   - sync version: <methodName>(args) -> parsed struct
//   - async version: <methodName>Async(args, correlationId) -> QString
// -----------------------------------------------------------------------------
class WalletRpcClient : public HttpRpcClient
{
    Q_OBJECT

public:
    /// Convenience constants for the V1.0 wallet-rpc default endpoint.
    static constexpr quint16 kDefaultPort = 19071;
    static constexpr const char* kDefaultPath = "/json_rpc";

    explicit WalletRpcClient(QObject* parent = nullptr);
    WalletRpcClient(const QUrl& endpoint, QObject* parent = nullptr);
    ~WalletRpcClient() override;

    // =========================================================================
    // BalanceApi (6 methods)
    // =========================================================================

    /// get_balance. Atomic units. account_index 0 = primary.
    BalanceInfo getBalance(quint64 account_index = 0);
    QString getBalanceAsync(quint64 account_index,
                            const QString& correlationId = {});

    /// get_unlocked_balance. Sub-account aware.
    BalanceInfo getUnlockedBalance(quint64 account_index = 0);
    QString getUnlockedBalanceAsync(quint64 account_index,
                                    const QString& correlationId = {});

    /// get_address. Returns the (account_index, address_index) sub-address.
    AccountAddress getAddress(quint64 account_index = 0,
                              quint64 address_index = 0);
    QString getAddressAsync(quint64 account_index,
                            quint64 address_index,
                            const QString& correlationId = {});

    /// FIX-G20 (2026-07-18): create_address RPC.
    /// Creates a NEW subaddress at the next available index and returns it.
    /// This is the correct RPC for creating subaddresses — get_address only
    /// retrieves existing ones and may fall back to the primary if the
    /// requested index doesn't exist yet.
    AccountAddress createAddress(quint64 account_index = 0,
                                  const QString& label = {});

    /// get_accounts. Returns all sub-addresses for the current wallet.
    AccountList getAccounts();
    QString getAccountsAsync(const QString& correlationId = {});

    /// get_version. Returns numeric version + human-readable release string.
    WalletVersion getVersion();
    QString getVersionAsync(const QString& correlationId = {});

    /// get_height. Returns the chain height as seen by the wallet.
    BlockHeight getHeight();
    QString getHeightAsync(const QString& correlationId = {});

    // =========================================================================
    // TransferApi (3 methods)
    // =========================================================================

    /// transfer_split. Single transaction to N destinations.
    TransferResult transferSplit(const TransferRequest& request);
    QString transferSplitAsync(const TransferRequest& request,
                               const QString& correlationId = {});

    /// sweep_all. Send all unlocked balance to one destination.
    /// destinations in `request` MUST contain exactly one element.
    TransferResult sweepAll(const TransferRequest& request);
    QString sweepAllAsync(const TransferRequest& request,
                          const QString& correlationId = {});

    /// sweep_single. Send a specific unlocked output to one destination.
    /// request.amount is the source output amount; destinations[0].amount
    /// is ignored (sweep sends the whole output).
    TransferResult sweepSingle(const TransferRequest& request);
    QString sweepSingleAsync(const TransferRequest& request,
                             const QString& correlationId = {});

    // =========================================================================
    // HistoryApi (3 methods)
    // =========================================================================

    /// get_transfer. Single TX by tx_hash. Returns default-constructed entry
    /// (tx_hash == "") if not found.
    TransferEntry getTransfer(const QString& txHash);
    QString getTransferAsync(const QString& txHash,
                             const QString& correlationId = {});

    /// get_transfers. Filtered list (in/out/pending/failed/pool) by account.
    TransferList getTransfers(const QString& filter = "all",
                              quint64 account_index = 0,
                              quint64 max_count = 0);
    QString getTransfersAsync(const QString& filter,
                              quint64 account_index,
                              quint64 max_count,
                              const QString& correlationId = {});

    /// get_payments. Incoming TXs filtered by payment_id (legacy).
    PaymentList getPayments(const QString& payment_id);
    QString getPaymentsAsync(const QString& payment_id,
                             const QString& correlationId = {});

    // =========================================================================
    // AddressBookApi (3 methods)
    // =========================================================================

    /// get_address_book. Returns all entries.
    AddressBookEntryList getAddressBook();
    QString getAddressBookAsync(const QString& correlationId = {});

    /// add_address_book. Returns the new entry's index.
    quint64 addAddressBook(const AddressBookEntry& entry);
    QString addAddressBookAsync(const AddressBookEntry& entry,
                                const QString& correlationId = {});

    /// delete_address_book. Remove by index.
    void deleteAddressBook(quint64 index);
    QString deleteAddressBookAsync(quint64 index,
                                   const QString& correlationId = {});

    // =========================================================================
    // UriApi (2 methods)
    // =========================================================================

    /// make_uri. Encode address+amount+pid into a pay-URI string.
    QString makeUri(const MakeUriRequest& request);
    QString makeUriAsync(const MakeUriRequest& request,
                         const QString& correlationId = {});

    /// parse_uri. Decode a pay-URI to its components.
    ParsedUri parseUri(const QString& uri);
    QString parseUriAsync(const QString& uri,
                          const QString& correlationId = {});

    // =========================================================================
    // TxProofApi (4 methods)
    // =========================================================================

    /// get_tx_key. Returns the tx private key (hex) for an outgoing tx we sent.
    QString getTxKey(const QString& txHash);
    QString getTxKeyAsync(const QString& txHash,
                          const QString& correlationId = {});

    /// check_tx_key. Verify that `tx_key` matches `txHash` and confirm the
    /// recipient received `expectedAmount` atomic units.
    CheckTxKeyResult checkTxKey(const QString& txHash,
                                const QString& txKey,
                                const QString& address,
                                quint64 expectedAmount = 0);
    QString checkTxKeyAsync(const QString& txHash,
                             const QString& txKey,
                             const QString& address,
                             quint64 expectedAmount,
                             const QString& correlationId = {});

    /// get_tx_proof. Returns MCTX proof (hex signature) for a tx to address.
    QString getTxProof(const QString& txHash,
                       const QString& address,
                       const QString& message = QString());
    QString getTxProofAsync(const QString& txHash,
                            const QString& address,
                            const QString& message,
                            const QString& correlationId = {});

    /// check_tx_proof. Verify the signature for tx+address+message.
    CheckTxProofResult checkTxProof(const QString& txHash,
                                    const QString& address,
                                    const QString& message,
                                    const QString& signature);
    QString checkTxProofAsync(const QString& txHash,
                              const QString& address,
                              const QString& message,
                              const QString& signature,
                              const QString& correlationId = {});

    // =========================================================================
    // BalanceProofApi (2 methods)
    // =========================================================================

    /// get_reserve_proof. Returns the audit-style signature proving that
    /// total >= amount at the time the proof was generated.
    QString getReserveProof(bool all,
                            quint64 account_index,
                            quint64 amount,
                            const QString& message = QString());
    QString getReserveProofAsync(bool all,
                                 quint64 account_index,
                                 quint64 amount,
                                 const QString& message,
                                 const QString& correlationId = {});

    /// check_reserve_proof. Verify the signature against current state.
    CheckReserveProofResult checkReserveProof(const QString& message,
                                              const QString& signature);
    QString checkReserveProofAsync(const QString& message,
                                   const QString& signature,
                                   const QString& correlationId = {});

    // =========================================================================
    // MultisigApi (2 methods)
    // =========================================================================

    /// make_multisig. Convert a regular wallet into an N-of-M multisig wallet.
    /// `multisigInfo` is the list of hex multisig strings from each cosigner.
    MultisigInfo makeMultisig(quint64 threshold,
                              const QVector<QString>& multisigInfo);
    QString makeMultisigAsync(quint64 threshold,
                              const QVector<QString>& multisigInfo,
                              const QString& correlationId = {});

    /// is_multisig. Returns whether the current wallet is multisig, ready,
    /// and the threshold/total parameters if so.
    MultisigStatus isMultisig();
    QString isMultisigAsync(const QString& correlationId = {});

    // =========================================================================
    // FreezeApi (1 method)
    // =========================================================================

    /// freeze. Lock the given atomic-unit amount until a specified block
    /// height. Returns the spending tx hash + key.
    FreezeResult freeze(quint64 amount, quint64 unlock_block_height);
    QString freezeAsync(quint64 amount,
                        quint64 unlock_block_height,
                        const QString& correlationId = {});

    // =========================================================================
    // OtherApi (4 methods - wallet lifecycle)
    // =========================================================================

    /// create_wallet. V5.4.7: wallet-rpc is the sole encryptor.
    void createWallet(const CreateWalletRequest& request);
    QString createWalletAsync(const CreateWalletRequest& request,
                              const QString& correlationId = {});

    /// open_wallet. V5.4.7: single password verifier.
    void openWallet(const OpenWalletRequest& request);
    QString openWalletAsync(const OpenWalletRequest& request,
                            const QString& correlationId = {});

    /// query_key (st-151). Queries keys/mnemonic from an open wallet.
    /// key_type: "mnemonic" | "view_key" | "spend_key" | ...
    /// Returns the sensitive key material as a SecureBuffer so the C++
    /// caller can scrub it after use. This method is NOT Q_INVOKABLE;
    /// QML must never receive raw secret material from this layer.
    judecoin_wallet::core::util::SecureBuffer queryKey(const QueryKeyRequest& request);

    /// close_wallet. Idempotent: closing a non-open wallet is a no-op.
    void closeWallet(const CloseWalletRequest& request);
    QString closeWalletAsync(const CloseWalletRequest& request,
                             const QString& correlationId = {});

    /// change_wallet_password. Re-encrypts the .keys file in place.
    void changeWalletPassword(const ChangeWalletPasswordRequest& request);
    QString changeWalletPasswordAsync(const ChangeWalletPasswordRequest& request,
                                      const QString& correlationId = {});

    /// generate_from_keys. V5.4.7: wallet-rpc is the sole encryptor;
    /// UI never touches the .keys file. View-only / restore variant.
    void generateFromKeys(const GenerateFromKeysRequest& request);
    QString generateFromKeysAsync(const GenerateFromKeysRequest& request,
                                  const QString& correlationId = {});

    /// generate_from_json. V5.4.7: advanced restore from a JSON snapshot;
    /// UI forwards user-provided JSON text verbatim, wallet-rpc parses +
    /// re-encrypts.
    void generateFromJson(const GenerateFromJsonRequest& request);
    QString generateFromJsonAsync(const GenerateFromJsonRequest& request,
                                  const QString& correlationId = {});

    /// restore_deterministic_wallet (st-150). Seed-based wallet recovery.
    /// wallet-rpc derives address + viewkey + spendkey from the 25-word
    /// mnemonic and creates a new wallet file. Returns {} on success.
    void restoreDeterministicWallet(
        const RestoreDeterministicRequest& request);
    QString restoreDeterministicWalletAsync(
        const RestoreDeterministicRequest& request,
        const QString& correlationId = {});


signals:
    /// Emitted when an async call succeeds. `correlationId` matches the
    /// value returned by the corresponding Async method.
    /// `method` is the wallet-rpc method name (e.g. "get_balance").
    /// `result` is the raw JSON-RPC result object - the QML layer can
    /// either parse it directly or use one of the <X>Api parser helpers.
    void walletResponse(const QString& correlationId,
                        const QString& method,
                        const QJsonObject& result);

    /// Emitted when an async call fails. `error` carries the categorised
    /// exception. Callers should disconnect on this signal and let the
    /// UI surface a user-friendly message via RpcException::toString().
    void walletFailed(const QString& correlationId,
                      const QString& method,
                      const judecoin_wallet::core::rpc::RpcException error);

private slots:
    /// Bridge HttpRpcClient::rpcCompleted to walletResponse. Only forwards
    /// calls tagged as wallet-rpc calls (currently all calls from this
    /// object satisfy that, but the indirection leaves room for future
    /// daemon RPCs to be re-routed).
    void onRpcCompleted(const QString& correlationId,
                        const QString& method,
                        const QJsonObject& result);
    void onRpcFailed(const QString& correlationId,
                     const QString& method,
                     const judecoin_wallet::core::rpc::RpcException error);

private:
    /// Common path: asynchronously call a method. Returns the correlation
    /// id assigned (auto-generated if empty). Result is delivered through
    /// the walletResponse / walletFailed signals. The two callbacks are
    /// not used; we re-route through our own slots.
    QString callAsync(const QString& method,
                      const QJsonObject& params,
                      const QString& correlationId);

    // Construct a full http://127.0.0.1:19071/json_rpc URL.
    static QUrl defaultEndpoint();

    Q_DISABLE_COPY(WalletRpcClient)
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_WALLET_RPC_CLIENT_H
