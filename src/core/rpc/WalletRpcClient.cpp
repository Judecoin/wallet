// =============================================================================
// Judecoin Wallet - WalletRpcClient (st-005) - implementation
//
// All 30 wallet-rpc methods share the same skeleton:
//
//   <sync>   <methodName>(args) -> Struct
//     1. build QJsonObject params
//     2. call HttpRpcClient::call("<wallet_rpc_method>", params)
//     3. parse the result envelope via the matching <X>Api::parse helper
//     4. return the populated struct (default-constructed on parse failure)
//
//   <async>  <methodName>Async(args, correlationId) -> QString
//     1. build QJsonObject params
//     2. call HttpRpcClient::callAsync("<wallet_rpc_method>", params, ...,
//                                      correlationId)
//     3. return the correlation id (or the auto-generated one)
//     4. on completion, HttpRpcClient::rpcCompleted fires -> onRpcCompleted
//        -> walletResponse / walletFailed re-emitted with the same id
//
// Errors thrown from sync methods are RpcException instances; errors from
// async methods are delivered via the walletFailed signal.
// =============================================================================

#include "core/rpc/WalletRpcClient.h"

#include "core/util/SecureBuffer.h"

#include <QJsonDocument>
#include <QUrl>

using judecoin_wallet::core::util::SecureBuffer;
using judecoin_wallet::core::util::secureClear;

namespace judecoin_wallet::core::rpc {

// =============================================================================
// Construction
// =============================================================================

WalletRpcClient::WalletRpcClient(QObject* parent)
    : HttpRpcClient(parent)
{
    setEndpoint(defaultEndpoint());
    connect(this, &HttpRpcClient::rpcCompleted,
            this, &WalletRpcClient::onRpcCompleted);
    connect(this, &HttpRpcClient::rpcFailed,
            this, &WalletRpcClient::onRpcFailed);
}

WalletRpcClient::WalletRpcClient(const QUrl& endpoint, QObject* parent)
    : HttpRpcClient(parent)
{
    setEndpoint(endpoint);
    connect(this, &HttpRpcClient::rpcCompleted,
            this, &WalletRpcClient::onRpcCompleted);
    connect(this, &HttpRpcClient::rpcFailed,
            this, &WalletRpcClient::onRpcFailed);
}

WalletRpcClient::~WalletRpcClient() = default;

QUrl WalletRpcClient::defaultEndpoint()
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(kDefaultPort);
    url.setPath(QString::fromLatin1(kDefaultPath));
    return url;
}

// =============================================================================
// Private helpers
// =============================================================================

QString WalletRpcClient::callAsync(const QString& method,
                                   const QJsonObject& params,
                                   const QString& correlationId)
{
    // Empty completion/error callbacks; we re-route through our own slots.
    return HttpRpcClient::callAsync(method, params, {}, {}, correlationId);
}

void WalletRpcClient::onRpcCompleted(const QString& correlationId,
                                     const QString& method,
                                     const QJsonObject& result)
{
    emit walletResponse(correlationId, method, result);
}

void WalletRpcClient::onRpcFailed(const QString& correlationId,
                                  const QString& method,
                                  const judecoin_wallet::core::rpc::RpcException error)
{
    emit walletFailed(correlationId, method, error);
}

// =============================================================================
// BalanceApi (6 methods)
// =============================================================================

BalanceInfo WalletRpcClient::getBalance(quint64 account_index)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_balance"), params);
    BalanceInfo out;
    BalanceInfo::parse(result, out);
    return out;
}

QString WalletRpcClient::getBalanceAsync(quint64 account_index,
                                         const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    return callAsync(QStringLiteral("get_balance"), params, correlationId);
}

BalanceInfo WalletRpcClient::getUnlockedBalance(quint64 account_index)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_unlocked_balance"), params);
    BalanceInfo out;
    BalanceInfo::parse(result, out);
    return out;
}

QString WalletRpcClient::getUnlockedBalanceAsync(quint64 account_index,
                                                 const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    return callAsync(QStringLiteral("get_unlocked_balance"),
                     params, correlationId);
}

AccountAddress WalletRpcClient::getAddress(quint64 account_index,
                                           quint64 address_index)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));

    // FIX-G22 (2026-07-18): Some Oxen/Loki wallet-rpc forks require
    // address_index as an ARRAY (e.g. [1]) rather than a scalar.
    // Sending a scalar for non-zero indices may be silently ignored
    // and the primary returned instead.
    QJsonArray addrIdxArr;
    addrIdxArr.append(QJsonValue(static_cast<qint64>(address_index)));
    params.insert(QStringLiteral("address_index"), addrIdxArr);

    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_address"), params);

    // G23 diagnostic: print raw get_address response
    qInfo().noquote()
        << "st-G23: get_address raw response for index=" << address_index
        << "\n" << QJsonDocument(result).toJson(QJsonDocument::Compact);

    // The response format from this wallet-rpc:
    // {
    //   "address": "<PRIMARY>",        ← always the primary
    //   "addresses": [{                  ← actual subaddress result
    //     "address": "<SUBADDR>",
    //     "address_index": N
    //   }]
    // }
    // We must read from "addresses" first, NOT the top-level "address".
    AccountAddress out;
    const QJsonArray arr = result.value(
        QStringLiteral("addresses")).toArray();
    if (!arr.isEmpty()) {
        AccountAddress::parse(arr.first().toObject(), out);
    } else {
        AccountAddress::parse(result, out);  // fallback: single format
    }
    return out;
}

QString WalletRpcClient::getAddressAsync(quint64 account_index,
                                         quint64 address_index,
                                         const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    params.insert(QStringLiteral("address_index"),
                  QJsonValue(static_cast<qint64>(address_index)));
    return callAsync(QStringLiteral("get_address"), params, correlationId);
}

// FIX-G20 (2026-07-18): create_address RPC.
// Creates a new subaddress at the next available index.
AccountAddress WalletRpcClient::createAddress(quint64 account_index,
                                              const QString& label)
{
    QJsonObject params;
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    if (!label.isEmpty()) {
        params.insert(QStringLiteral("label"), label);
    }
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("create_address"), params);

    // G23 diagnostic: print raw create_address response
    qInfo().noquote()
        << "st-G23: create_address raw response"
        << "\n" << QJsonDocument(result).toJson(QJsonDocument::Compact);

    AccountAddress out;
    AccountAddress::parse(result, out);
    return out;
}

AccountList WalletRpcClient::getAccounts()
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_accounts"), {});

    // G23 diagnostic: print raw get_accounts response
    qInfo().noquote()
        << "st-G23: get_accounts raw response"
        << "\n" << QJsonDocument(result).toJson(QJsonDocument::Compact);

    AccountList out;
    AccountList::parse(result, out);
    return out;
}

QString WalletRpcClient::getAccountsAsync(const QString& correlationId)
{
    return callAsync(QStringLiteral("get_accounts"), {}, correlationId);
}

WalletVersion WalletRpcClient::getVersion()
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_version"), {});
    WalletVersion out;
    WalletVersion::parse(result, out);
    return out;
}

QString WalletRpcClient::getVersionAsync(const QString& correlationId)
{
    return callAsync(QStringLiteral("get_version"), {}, correlationId);
}

BlockHeight WalletRpcClient::getHeight()
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_height"), {});
    BlockHeight out;
    BlockHeight::parse(result, out);
    return out;
}

QString WalletRpcClient::getHeightAsync(const QString& correlationId)
{
    return callAsync(QStringLiteral("get_height"), {}, correlationId);
}

// =============================================================================
// TransferApi (3 methods)
// =============================================================================

TransferResult WalletRpcClient::transferSplit(const TransferRequest& request)
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("transfer_split"), request.toJson());
    TransferResult out;
    TransferResult::parse(result, out);
    return out;
}

QString WalletRpcClient::transferSplitAsync(const TransferRequest& request,
                                            const QString& correlationId)
{
    return callAsync(QStringLiteral("transfer_split"),
                     request.toJson(), correlationId);
}

TransferResult WalletRpcClient::sweepAll(const TransferRequest& request)
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("sweep_all"), request.toJson());
    TransferResult out;
    TransferResult::parse(result, out);
    return out;
}

QString WalletRpcClient::sweepAllAsync(const TransferRequest& request,
                                       const QString& correlationId)
{
    return callAsync(QStringLiteral("sweep_all"),
                     request.toJson(), correlationId);
}

TransferResult WalletRpcClient::sweepSingle(const TransferRequest& request)
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("sweep_single"), request.toJson());
    TransferResult out;
    TransferResult::parse(result, out);
    return out;
}

QString WalletRpcClient::sweepSingleAsync(const TransferRequest& request,
                                          const QString& correlationId)
{
    return callAsync(QStringLiteral("sweep_single"),
                     request.toJson(), correlationId);
}

// =============================================================================
// HistoryApi (3 methods)
// =============================================================================

TransferEntry WalletRpcClient::getTransfer(const QString& txHash)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_transfer"), params);
    TransferEntry out;
    TransferEntry::parse(result, out);
    return out;
}

QString WalletRpcClient::getTransferAsync(const QString& txHash,
                                          const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    return callAsync(QStringLiteral("get_transfer"), params, correlationId);
}

TransferList WalletRpcClient::getTransfers(const QString& filter,
                                           quint64 account_index,
                                           quint64 max_count)
{
    QJsonObject params;
    params.insert(QStringLiteral("filter"), filter);
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    if (max_count > 0) {
        params.insert(QStringLiteral("count"),
                      QJsonValue(static_cast<qint64>(max_count)));
    }
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_transfers"), params);
    TransferList out;
    TransferList::parse(result, out);
    return out;
}

QString WalletRpcClient::getTransfersAsync(const QString& filter,
                                           quint64 account_index,
                                           quint64 max_count,
                                           const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("filter"), filter);
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    if (max_count > 0) {
        params.insert(QStringLiteral("count"),
                      QJsonValue(static_cast<qint64>(max_count)));
    }
    return callAsync(QStringLiteral("get_transfers"), params, correlationId);
}

PaymentList WalletRpcClient::getPayments(const QString& payment_id)
{
    QJsonObject params;
    params.insert(QStringLiteral("payment_id"), payment_id);
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_payments"), params);
    PaymentList out;
    PaymentList::parse(result, out);
    return out;
}

QString WalletRpcClient::getPaymentsAsync(const QString& payment_id,
                                          const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("payment_id"), payment_id);
    return callAsync(QStringLiteral("get_payments"), params, correlationId);
}

// =============================================================================
// AddressBookApi (3 methods)
// =============================================================================

AddressBookEntryList WalletRpcClient::getAddressBook()
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_address_book"), {});
    AddressBookEntryList out;
    AddressBookEntryList::parse(result, out);
    return out;
}

QString WalletRpcClient::getAddressBookAsync(const QString& correlationId)
{
    return callAsync(QStringLiteral("get_address_book"), {}, correlationId);
}

quint64 WalletRpcClient::addAddressBook(const AddressBookEntry& entry)
{
    const QJsonObject result = HttpRpcClient::call(
        QStringLiteral("add_address_book"), entry.toJson());
    // wallet-rpc returns { index: <number> } on success.
    return static_cast<quint64>(result.value(QStringLiteral("index"))
                                    .toDouble(0.0));
}

QString WalletRpcClient::addAddressBookAsync(const AddressBookEntry& entry,
                                             const QString& correlationId)
{
    return callAsync(QStringLiteral("add_address_book"),
                     entry.toJson(), correlationId);
}

void WalletRpcClient::deleteAddressBook(quint64 index)
{
    QJsonObject params;
    params.insert(QStringLiteral("index"),
                  QJsonValue(static_cast<qint64>(index)));
    HttpRpcClient::call(QStringLiteral("delete_address_book"), params);
    // wallet-rpc returns {} on success; ignore.
}

QString WalletRpcClient::deleteAddressBookAsync(quint64 index,
                                                const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("index"),
                  QJsonValue(static_cast<qint64>(index)));
    return callAsync(QStringLiteral("delete_address_book"),
                     params, correlationId);
}

// =============================================================================
// UriApi (2 methods)
// =============================================================================

QString WalletRpcClient::makeUri(const MakeUriRequest& request)
{
    const QJsonObject result = HttpRpcClient::call(
        QStringLiteral("make_uri"), request.toJson());
    return result.value(QStringLiteral("uri")).toString();
}

QString WalletRpcClient::makeUriAsync(const MakeUriRequest& request,
                                      const QString& correlationId)
{
    return callAsync(QStringLiteral("make_uri"),
                     request.toJson(), correlationId);
}

ParsedUri WalletRpcClient::parseUri(const QString& uri)
{
    QJsonObject params;
    params.insert(QStringLiteral("uri"), uri);
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("parse_uri"), params);
    ParsedUri out;
    ParsedUri::parse(result, out);
    return out;
}

QString WalletRpcClient::parseUriAsync(const QString& uri,
                                       const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("uri"), uri);
    return callAsync(QStringLiteral("parse_uri"), params, correlationId);
}

// =============================================================================
// TxProofApi (4 methods)
// =============================================================================

QString WalletRpcClient::getTxKey(const QString& txHash)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    const QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_tx_key"), params);
    return result.value(QStringLiteral("tx_key")).toString();
}

QString WalletRpcClient::getTxKeyAsync(const QString& txHash,
                                       const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    return callAsync(QStringLiteral("get_tx_key"), params, correlationId);
}

CheckTxKeyResult WalletRpcClient::checkTxKey(const QString& txHash,
                                             const QString& txKey,
                                             const QString& address,
                                             quint64 expectedAmount)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    params.insert(QStringLiteral("tx_key"), txKey);
    params.insert(QStringLiteral("address"), address);
    if (expectedAmount > 0) {
        params.insert(QStringLiteral("amount"),
                      QJsonValue(static_cast<qint64>(expectedAmount)));
    }
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("check_tx_key"), params);
    CheckTxKeyResult out;
    CheckTxKeyResult::parse(result, out);
    return out;
}

QString WalletRpcClient::checkTxKeyAsync(const QString& txHash,
                                         const QString& txKey,
                                         const QString& address,
                                         quint64 expectedAmount,
                                         const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    params.insert(QStringLiteral("tx_key"), txKey);
    params.insert(QStringLiteral("address"), address);
    if (expectedAmount > 0) {
        params.insert(QStringLiteral("amount"),
                      QJsonValue(static_cast<qint64>(expectedAmount)));
    }
    return callAsync(QStringLiteral("check_tx_key"), params, correlationId);
}

QString WalletRpcClient::getTxProof(const QString& txHash,
                                    const QString& address,
                                    const QString& message)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    params.insert(QStringLiteral("address"), address);
    if (!message.isEmpty()) {
        params.insert(QStringLiteral("message"), message);
    }
    const QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_tx_proof"), params);
    return result.value(QStringLiteral("signature")).toString();
}

QString WalletRpcClient::getTxProofAsync(const QString& txHash,
                                         const QString& address,
                                         const QString& message,
                                         const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    params.insert(QStringLiteral("address"), address);
    if (!message.isEmpty()) {
        params.insert(QStringLiteral("message"), message);
    }
    return callAsync(QStringLiteral("get_tx_proof"), params, correlationId);
}

CheckTxProofResult WalletRpcClient::checkTxProof(const QString& txHash,
                                                 const QString& address,
                                                 const QString& message,
                                                 const QString& signature)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    params.insert(QStringLiteral("address"), address);
    params.insert(QStringLiteral("message"), message);
    params.insert(QStringLiteral("signature"), signature);
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("check_tx_proof"), params);
    CheckTxProofResult out;
    CheckTxProofResult::parse(result, out);
    return out;
}

QString WalletRpcClient::checkTxProofAsync(const QString& txHash,
                                           const QString& address,
                                           const QString& message,
                                           const QString& signature,
                                           const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("txid"), txHash);
    params.insert(QStringLiteral("address"), address);
    params.insert(QStringLiteral("message"), message);
    params.insert(QStringLiteral("signature"), signature);
    return callAsync(QStringLiteral("check_tx_proof"), params, correlationId);
}

// =============================================================================
// BalanceProofApi (2 methods)
// =============================================================================

QString WalletRpcClient::getReserveProof(bool all,
                                         quint64 account_index,
                                         quint64 amount,
                                         const QString& message)
{
    QJsonObject params;
    params.insert(QStringLiteral("all"), QJsonValue(all));
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    params.insert(QStringLiteral("amount"),
                  QJsonValue(static_cast<qint64>(amount)));
    if (!message.isEmpty()) {
        params.insert(QStringLiteral("message"), message);
    }
    const QJsonObject result = HttpRpcClient::call(
        QStringLiteral("get_reserve_proof"), params);
    return result.value(QStringLiteral("signature")).toString();
}

QString WalletRpcClient::getReserveProofAsync(bool all,
                                              quint64 account_index,
                                              quint64 amount,
                                              const QString& message,
                                              const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("all"), QJsonValue(all));
    params.insert(QStringLiteral("account_index"),
                  QJsonValue(static_cast<qint64>(account_index)));
    params.insert(QStringLiteral("amount"),
                  QJsonValue(static_cast<qint64>(amount)));
    if (!message.isEmpty()) {
        params.insert(QStringLiteral("message"), message);
    }
    return callAsync(QStringLiteral("get_reserve_proof"),
                     params, correlationId);
}

CheckReserveProofResult WalletRpcClient::checkReserveProof(
    const QString& message,
    const QString& signature)
{
    QJsonObject params;
    params.insert(QStringLiteral("message"), message);
    params.insert(QStringLiteral("signature"), signature);
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("check_reserve_proof"), params);
    CheckReserveProofResult out;
    CheckReserveProofResult::parse(result, out);
    return out;
}

QString WalletRpcClient::checkReserveProofAsync(const QString& message,
                                                const QString& signature,
                                                const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("message"), message);
    params.insert(QStringLiteral("signature"), signature);
    return callAsync(QStringLiteral("check_reserve_proof"),
                     params, correlationId);
}

// =============================================================================
// MultisigApi (2 methods)
// =============================================================================

MultisigInfo WalletRpcClient::makeMultisig(
    quint64 threshold,
    const QVector<QString>& multisigInfo)
{
    QJsonObject params;
    params.insert(QStringLiteral("threshold"),
                  QJsonValue(static_cast<qint64>(threshold)));
    QJsonArray arr;
    for (const auto& s : multisigInfo) {
        arr.append(s);
    }
    params.insert(QStringLiteral("multisig_info"), arr);
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("make_multisig"), params);
    MultisigInfo out;
    MultisigInfo::parse(result, out);
    return out;
}

QString WalletRpcClient::makeMultisigAsync(
    quint64 threshold,
    const QVector<QString>& multisigInfo,
    const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("threshold"),
                  QJsonValue(static_cast<qint64>(threshold)));
    QJsonArray arr;
    for (const auto& s : multisigInfo) {
        arr.append(s);
    }
    params.insert(QStringLiteral("multisig_info"), arr);
    return callAsync(QStringLiteral("make_multisig"), params, correlationId);
}

MultisigStatus WalletRpcClient::isMultisig()
{
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("is_multisig"), {});
    MultisigStatus out;
    MultisigStatus::parse(result, out);
    return out;
}

QString WalletRpcClient::isMultisigAsync(const QString& correlationId)
{
    return callAsync(QStringLiteral("is_multisig"), {}, correlationId);
}

// =============================================================================
// FreezeApi (1 method)
// =============================================================================

FreezeResult WalletRpcClient::freeze(quint64 amount, quint64 unlock_block_height)
{
    QJsonObject params;
    params.insert(QStringLiteral("amount"),
                  QJsonValue(static_cast<qint64>(amount)));
    params.insert(QStringLiteral("unlock_block_height"),
                  QJsonValue(static_cast<qint64>(unlock_block_height)));
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("freeze"), params);
    FreezeResult out;
    FreezeResult::parse(result, out);
    return out;
}

QString WalletRpcClient::freezeAsync(quint64 amount,
                                     quint64 unlock_block_height,
                                     const QString& correlationId)
{
    QJsonObject params;
    params.insert(QStringLiteral("amount"),
                  QJsonValue(static_cast<qint64>(amount)));
    params.insert(QStringLiteral("unlock_block_height"),
                  QJsonValue(static_cast<qint64>(unlock_block_height)));
    return callAsync(QStringLiteral("freeze"), params, correlationId);
}

// =============================================================================
// OtherApi (4 methods - wallet lifecycle)
// =============================================================================

void WalletRpcClient::createWallet(const CreateWalletRequest& request)
{
    HttpRpcClient::call(QStringLiteral("create_wallet"), request.toJson());
    // wallet-rpc returns {} on success.
}

QString WalletRpcClient::createWalletAsync(const CreateWalletRequest& request,
                                           const QString& correlationId)
{
    return callAsync(QStringLiteral("create_wallet"),
                     request.toJson(), correlationId);
}

// -----------------------------------------------------------------------------
// query_key (st-151)
// -----------------------------------------------------------------------------

SecureBuffer WalletRpcClient::queryKey(const QueryKeyRequest& request)
{
    // query_key returns {"key": "..."}.
    QJsonObject result = HttpRpcClient::call(
        QStringLiteral("query_key"), request.toJson());
    QString key = result.value(QStringLiteral("key")).toString();
    QByteArray keyBytes = key.toUtf8();
    SecureBuffer buffer(keyBytes);
    secureClear(key);
    secureClear(keyBytes);
    return buffer;
}

void WalletRpcClient::openWallet(const OpenWalletRequest& request)
{
    HttpRpcClient::call(QStringLiteral("open_wallet"), request.toJson());
}

QString WalletRpcClient::openWalletAsync(const OpenWalletRequest& request,
                                         const QString& correlationId)
{
    return callAsync(QStringLiteral("open_wallet"),
                     request.toJson(), correlationId);
}

void WalletRpcClient::closeWallet(const CloseWalletRequest& request)
{
    HttpRpcClient::call(QStringLiteral("close_wallet"), request.toJson());
}

QString WalletRpcClient::closeWalletAsync(const CloseWalletRequest& request,
                                          const QString& correlationId)
{
    return callAsync(QStringLiteral("close_wallet"),
                     request.toJson(), correlationId);
}

void WalletRpcClient::changeWalletPassword(
    const ChangeWalletPasswordRequest& request)
{
    HttpRpcClient::call(QStringLiteral("change_wallet_password"),
                       request.toJson());
}

QString WalletRpcClient::changeWalletPasswordAsync(
    const ChangeWalletPasswordRequest& request,
    const QString& correlationId)
{
    return callAsync(QStringLiteral("change_wallet_password"),
                     request.toJson(), correlationId);
}

void WalletRpcClient::generateFromKeys(const GenerateFromKeysRequest& request)
{
    HttpRpcClient::call(QStringLiteral("generate_from_keys"),
                        request.toJson());
    // wallet-rpc returns {} on success.
}

QString WalletRpcClient::generateFromKeysAsync(
    const GenerateFromKeysRequest& request,
    const QString& correlationId)
{
    return callAsync(QStringLiteral("generate_from_keys"),
                     request.toJson(), correlationId);
}

void WalletRpcClient::generateFromJson(const GenerateFromJsonRequest& request)
{
    HttpRpcClient::call(QStringLiteral("generate_from_json"),
                        request.toJson());
}

QString WalletRpcClient::generateFromJsonAsync(
    const GenerateFromJsonRequest& request,
    const QString& correlationId)
{
    return callAsync(QStringLiteral("generate_from_json"),
                     request.toJson(), correlationId);
}

// -----------------------------------------------------------------------------
// restore_deterministic_wallet (st-150)
// -----------------------------------------------------------------------------

void WalletRpcClient::restoreDeterministicWallet(
    const RestoreDeterministicRequest& request)
{
    HttpRpcClient::call(QStringLiteral("restore_deterministic_wallet"),
                          request.toJson());
}

QString WalletRpcClient::restoreDeterministicWalletAsync(
    const RestoreDeterministicRequest& request,
    const QString& correlationId)
{
    return callAsync(QStringLiteral("restore_deterministic_wallet"),
                     request.toJson(), correlationId);
}


} // namespace judecoin_wallet::core::rpc
