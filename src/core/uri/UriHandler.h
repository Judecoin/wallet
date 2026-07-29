// =============================================================================
// Judecoin Wallet - UriHandler (st-020, V5.4.4 #6)
//
// High-level wrapper over the wallet-rpc URI methods (parse_uri / make_uri)
// defined in WalletRpcClient (st-005). Lives in the "uri" module next to the
// future QML view-models that will be wired to the Send page.
//
// What it adds on top of WalletRpcClient:
//
//   1. A flat 5-argument signature for make_uri() that matches the devplan
//      spec (address, amount, payment_id, recipient_name, tx_description)
//      and that QML / JS controllers can call directly without having to
//      build a MakeUriRequest by hand.
//
//   2. A parse_uri() return shape that contains the 5 standard fields PLUS a
//      `unknown_parameters` map. The wallet-rpc itself only returns the
//      5 known fields, so UriHandler re-parses the original URI's query
//      string and captures any extra key/value pairs the sender encoded
//      (e.g. `?label=foo` from a non-judecoin wallet, future params from
//      a wallet-rpc version we don't yet know about, etc.). The UI can
//      then warn the user "this URI has unrecognized parameters".
//
//   3. Async signals (parseCompleted / makeCompleted / errorOccurred) so
//      QML can use the bindings without touching the JSON-RPC layer.
//
// V5.4.4 #6 sub-decisions enforced by this module:
//   A) Send page "Paste URI" button -> parseUri() auto-fills
//      To / Amount / Payment ID fields.
//   B) Send page "Generate URI" button -> makeUri() output rendered as
//      text + Copy button + collapsible detail list.
//   A) payment_id is hidden in the V1.0 Send form (subaddress replaces it)
//      but the parser still understands it for compat with old URIs.
//
// This is a thin wrapper: it never parses URIs itself, never validates
// addresses, never computes amount atomic units. All heavy lifting is
// delegated to wallet-rpc.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_URI_URI_HANDLER_H
#define JUDECOIN_WALLET_CORE_URI_URI_HANDLER_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace judecoin_wallet::core::rpc {
class WalletRpcClient;
class RpcException;
}

namespace judecoin_wallet::core::uri {

// -----------------------------------------------------------------------------
// UriHandler::ParsedUri
//
// Return shape of parseUri(). `uri` is the original input verbatim;
// `address` / `amount` / `payment_id` / `recipient_name` / `tx_description`
// are the standard 5 fields the wallet-rpc returns (see UriApi.h).
// `unknown_parameters` is the bonus UriHandler adds: any non-standard
// query parameter from the original URI, captured verbatim (key -> value).
// It is empty when the URI only carried standard fields.
// -----------------------------------------------------------------------------
struct ParsedUri
{
    QString uri;                                    // original input
    QString address;                                // may be empty if malformed
    quint64 amount = 0;                             // 0 if not encoded
    QString payment_id;                             // empty if none
    QString recipient_name;                         // empty if none
    QString tx_description;                         // empty if none
    QMap<QString, QString> unknown_parameters;      // extra query keys

    bool isValid() const { return !address.isEmpty(); }
};

// -----------------------------------------------------------------------------
// UriHandler
// -----------------------------------------------------------------------------
class UriHandler : public QObject
{
    Q_OBJECT

public:
    /// Query keys the wallet-rpc understands; any other key from the URI's
    /// query string ends up in ParsedUri::unknown_parameters. Exposed as a
    /// static const for tests / debuggers.
    static const QStringList& knownParameterKeys();

    explicit UriHandler(QObject* parent = nullptr);
    explicit UriHandler(judecoin_wallet::core::rpc::WalletRpcClient* wallet,
                        QObject* parent = nullptr);
    ~UriHandler() override;

    // -- wiring ---------------------------------------------------------------

    void setWallet(judecoin_wallet::core::rpc::WalletRpcClient* wallet);
    judecoin_wallet::core::rpc::WalletRpcClient* wallet() const { return m_wallet; }

    // -- synchronous API (throws on RPC failure) ------------------------------

    /// Parse a URI string into its components. Calls wallet-rpc parse_uri;
    /// additionally captures unknown_parameters from the URI query string.
    ParsedUri parseUri(const QString& uriString);

    /// Build a URI string from the 5 standard fields. Empty optional fields
    /// are omitted. Calls wallet-rpc make_uri.
    QString makeUri(const QString& address,
                    quint64 amount,
                    const QString& payment_id,
                    const QString& recipient_name,
                    const QString& tx_description);

    // -- static helpers (no RPC needed) ---------------------------------------

    /// Extract the standard 5 fields AND the unknown_parameters map from a
    /// raw URI string. Pure string parsing; does not contact wallet-rpc.
    /// The `address` and `amount` returned here are best-effort
    /// (URL-decoded, taken at face value) and should be cross-checked
    /// against parseUri() for anything that goes to the chain.
    static ParsedUri parseUriLocally(const QString& uriString);

    /// True if `address` is non-empty and starts with the scheme
    /// "judecoin:" (case-insensitive). Used to gate the "Paste URI"
    /// auto-fill UX in the Send page.
    static bool looksLikeJudecoinUri(const QString& uriString);

signals:
    void parseCompleted(::judecoin_wallet::core::uri::ParsedUri parsed);
    void makeCompleted(QString uri);
    void errorOccurred(::judecoin_wallet::core::rpc::RpcException error);

private:
    judecoin_wallet::core::rpc::WalletRpcClient* m_wallet = nullptr;  // not owned
};

} // namespace judecoin_wallet::core::uri

#endif // JUDECOIN_WALLET_CORE_URI_URI_HANDLER_H
