// =============================================================================
// Judecoin Wallet - UriApi.h (st-005, uri module)
//
// Strongly-typed request/response structs for the 2 wallet-rpc methods
// WalletRpcClient exposes as UriApi:
//
//   make_uri  -> { uri: <string> }    (encode address+amount+pid to URI)
//   parse_uri -> ParsedUri            (decode a URI to its components)
//
// V5.4.4 #6 (URI handling): per UI_DETAILS_TODO.md, the URI handler is split:
//   - UriApi (st-005, this file): low-level make_uri / parse_uri RPC wrappers.
//   - UriHandler (st-020): high-level paste-from-clipboard + QR generation.
//
// The two layers never share types directly: UriHandler calls the methods on
// WalletRpcClient and renders whatever the RPC returned. URI format is the
// Loki/Oxen fork standard (judecoin:<address>?amount=<x>&payment_id=<pid>).
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_URI_API_H
#define JUDECOIN_WALLET_CORE_RPC_URI_API_H

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// MakeUriRequest
//
// Parameters for make_uri. All fields except `address` are optional:
//   address       - 95-char mainnet address (required)
//   amount        - atomic units; 0 = no amount encoded in URI
//   payment_id    - short payment id (legacy); empty = no pid encoded
//   recipient_name - human-readable recipient label (some wallets render this)
//   tx_description - human-readable note (some wallets render this)
// -----------------------------------------------------------------------------
struct MakeUriRequest
{
    QString address;
    quint64 amount         = 0;
    QString payment_id;          // empty = none
    QString recipient_name;      // empty = none
    QString tx_description;      // empty = none

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, MakeUriRequest& out);
};

// -----------------------------------------------------------------------------
// ParsedUri
//
// Decoded URI fields. Mirrors the make_uri input shape so the UI can re-encode
// without losing information. `uri` is the original input verbatim.
// -----------------------------------------------------------------------------
struct ParsedUri
{
    QString uri;            // the original input string
    QString address;        // may be empty if URI is malformed
    quint64 amount = 0;     // 0 if no amount was encoded
    QString payment_id;     // empty if no pid
    QString recipient_name; // empty if none
    QString tx_description; // empty if none

    static bool parse(const QJsonObject& obj, ParsedUri& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_URI_API_H
