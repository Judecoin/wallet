// =============================================================================
// Judecoin Wallet - UriHandler (st-020, V5.4.4 #6)
//
// Implementation notes:
//
//  - parseUri() does TWO things:
//      (a) delegates to WalletRpcClient::parseUri for the 5 standard fields.
//          The RPC also re-validates the address and computes the amount
//          in canonical atomic units.
//      (b) re-parses the original URI's query string to populate
//          `unknown_parameters` -- the 5 standard fields are kept verbatim
//          from the RPC, the extra keys are added on top.
//
//  - makeUri() is a thin adapter: build a MakeUriRequest, call
//    WalletRpcClient::makeUri, return the resulting URI string.
//
//  - parseUriLocally() is a pure string parser used by the static helper
//    and by the unit tests. It must NOT contact the wallet-rpc. The 5
//    standard fields are extracted best-effort; unknown_parameters always
//    contains every query key that is not in the standard 5.
//
//  - looksLikeJudecoinUri() is a fast gate for the "Paste URI" UX in the
//    Send page (so a paste of e.g. an HTTPS URL into the address field
//    does not get mangled by parseUri).
// =============================================================================

#include "core/uri/UriHandler.h"

#include "core/rpc/HttpRpcClient.h"
#include "core/rpc/UriApi.h"
#include "core/rpc/WalletRpcClient.h"

#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace judecoin_wallet::core::uri {

using judecoin_wallet::core::rpc::MakeUriRequest;
using judecoin_wallet::core::rpc::RpcException;
using judecoin_wallet::core::rpc::WalletRpcClient;

// -----------------------------------------------------------------------------
// knownParameterKeys
// -----------------------------------------------------------------------------
const QStringList& UriHandler::knownParameterKeys()
{
    // Mirrors the 5 fields in UriApi.h. Order is irrelevant for the
    // set-membership check in parseUriLocally(); the only requirement
    // is that the 5 standard wallet-rpc field names are listed.
    static const QStringList keys = {
        QStringLiteral("amount"),
        QStringLiteral("payment_id"),
        QStringLiteral("recipient_name"),
        QStringLiteral("tx_description"),
    };
    return keys;
}

// -----------------------------------------------------------------------------
// ctor / dtor
// -----------------------------------------------------------------------------
UriHandler::UriHandler(QObject* parent)
    : QObject(parent)
{
}

UriHandler::UriHandler(WalletRpcClient* wallet, QObject* parent)
    : QObject(parent)
    , m_wallet(wallet)
{
}

UriHandler::~UriHandler() = default;

// -----------------------------------------------------------------------------
// setWallet
// -----------------------------------------------------------------------------
void UriHandler::setWallet(WalletRpcClient* wallet)
{
    m_wallet = wallet;
}

// -----------------------------------------------------------------------------
// parseUri
//
// Throws RpcException on transport / protocol / server error from wallet-rpc.
// The caller is expected to catch and forward to errorOccurred() if using
// the async path; for the sync path (QML JS), the call should be wrapped
// in try/catch by the view-model.
// -----------------------------------------------------------------------------
ParsedUri UriHandler::parseUri(const QString& uriString)
{
    if (m_wallet == nullptr) {
        RpcException e(RpcException::Category::Unknown,
                       -1,
                       QStringLiteral("UriHandler: no WalletRpcClient wired"));
        emit errorOccurred(e);
        throw e;
    }

    // (a) standard 5 fields come from the RPC, with proper validation +
    //     amount canonicalisation. We construct a ParsedUri directly from
    //     the RPC result and then layer (b) on top.
    const auto rpcParsed = m_wallet->parseUri(uriString);

    ParsedUri out;
    out.uri            = rpcParsed.uri.isEmpty() ? uriString : rpcParsed.uri;
    out.address        = rpcParsed.address;
    out.amount         = rpcParsed.amount;
    out.payment_id     = rpcParsed.payment_id;
    out.recipient_name = rpcParsed.recipient_name;
    out.tx_description = rpcParsed.tx_description;

    // (b) capture any non-standard query parameters. We re-parse the
    //     original input so the unknown_parameters reflect exactly what
    //     the sender wrote, not what the wallet-rpc re-emitted. The
    //     standard 5 fields are NEVER in unknown_parameters -- they are
    //     already populated above from the RPC.
    out.unknown_parameters = parseUriLocally(uriString).unknown_parameters;

    emit parseCompleted(out);
    return out;
}

// -----------------------------------------------------------------------------
// makeUri
// -----------------------------------------------------------------------------
QString UriHandler::makeUri(const QString& address,
                            quint64 amount,
                            const QString& payment_id,
                            const QString& recipient_name,
                            const QString& tx_description)
{
    if (m_wallet == nullptr) {
        RpcException e(RpcException::Category::Unknown,
                       -1,
                       QStringLiteral("UriHandler: no WalletRpcClient wired"));
        emit errorOccurred(e);
        throw e;
    }

    MakeUriRequest req;
    req.address        = address;
    req.amount         = amount;
    req.payment_id     = payment_id;
    req.recipient_name = recipient_name;
    req.tx_description = tx_description;

    const QString uri = m_wallet->makeUri(req);
    emit makeCompleted(uri);
    return uri;
}

// -----------------------------------------------------------------------------
// parseUriLocally
//
// Pure string parser. Used by parseUri() to fill `unknown_parameters` and
// also exposed as a static for tests / debug. Does NOT contact wallet-rpc,
// so the standard 5 fields returned here are best-effort: address is the
// raw text between "judecoin:" and "?", amount is the decoded integer (or 0
// if absent or unparseable), and the other three are raw decoded strings.
//
// Contract: `unknown_parameters` NEVER contains the 5 standard keys, even
// when their values are malformed. Malformed standard values are simply
// dropped (e.g. amount="abc" leaves amount=0 and does NOT add an unknown
// entry) so the parseUri() caller can trust the map only carries extras.
// -----------------------------------------------------------------------------
ParsedUri UriHandler::parseUriLocally(const QString& uriString)
{
    ParsedUri out;
    out.uri = uriString;

    if (uriString.isEmpty()) {
        return out;
    }

    // Split scheme / path / query. We accept both "judecoin:addr" and
    // "judecoin:addr?k=v" (no //, no authority component -- this is the
    // Monero-style URI, not an http URL).
    QString remainder = uriString.trimmed();

    const QString schemePrefix = QStringLiteral("judecoin:");
    if (remainder.left(schemePrefix.size()).compare(schemePrefix, Qt::CaseInsensitive) == 0) {
        remainder = remainder.mid(schemePrefix.size());
    } else if (remainder.contains(QStringLiteral("://"))) {
        // Has a // but not our scheme -> not a judecoin URI.
        return out;
    }

    // Split off the query string.
    QString queryPart;
    const int qIdx = remainder.indexOf(QChar('?'));
    if (qIdx >= 0) {
        queryPart = remainder.mid(qIdx + 1);
        remainder = remainder.left(qIdx);
    }

    // The "path" portion is the address (no extra path components in a
    // Monero-style payment URI; if there is one, we still take it as the
    // address -- wallet-rpc will reject if it does not validate).
    out.address = QUrl::fromPercentEncoding(remainder.toUtf8());

    if (queryPart.isEmpty()) {
        return out;
    }

    // Parse query items. Standard 5 fields are filled in; everything else
    // is bucketed into unknown_parameters.
    const QUrlQuery query(queryPart);
    const auto items = query.queryItems(QUrl::FullyDecoded);

    for (const auto& item : items) {
        const QString& key = item.first;
        const QString& value = item.second;
        if (key == QStringLiteral("amount")) {
            bool ok = false;
            const quint64 v = value.toULongLong(&ok);
            if (ok) {
                out.amount = v;
            }
            // If unparseable: amount stays 0; the key is NOT mirrored
            // into unknown_parameters (we only carry non-standard keys
            // there -- see contract above).
        } else if (key == QStringLiteral("payment_id")) {
            out.payment_id = value;
        } else if (key == QStringLiteral("recipient_name")) {
            out.recipient_name = value;
        } else if (key == QStringLiteral("tx_description")) {
            out.tx_description = value;
        } else {
            // Unknown key -- capture for the UI warning.
            out.unknown_parameters.insert(key, value);
        }
    }

    return out;
}

// -----------------------------------------------------------------------------
// looksLikeJudecoinUri
// -----------------------------------------------------------------------------
bool UriHandler::looksLikeJudecoinUri(const QString& uriString)
{
    if (uriString.isEmpty()) {
        return false;
    }
    const QString s = uriString.trimmed();
    return s.left(QStringLiteral("judecoin:").size())
            .compare(QStringLiteral("judecoin:"), Qt::CaseInsensitive) == 0;
}

} // namespace judecoin_wallet::core::uri
