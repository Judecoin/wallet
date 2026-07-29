// =============================================================================
// Judecoin Wallet - BalanceApi.h (st-005, balance module)
//
// Strongly-typed request/response structs for the 6 wallet-rpc methods in
// the "core" category that WalletRpcClient exposes as BalanceApi:
//
//   get_balance          -> BalanceInfo       (total + unlocked, atomic units)
//   get_unlocked_balance -> BalanceInfo       (sub-account aware)
//   get_address          -> AccountAddress    (single sub-address)
//   get_accounts         -> AccountList       (label + base + unused indices)
//   get_version          -> WalletVersion     (numeric + string)
//   get_height           -> BlockHeight       (chain height as seen by wallet)
//
// V5.4.2 (mainnet-only): all amounts are in atomic units (1 JUDE = 1e12). The
// UI converts at display time; this layer never multiplies or formats.
//
// V5.4.5: sub-address labels and account tags are NOT exposed via wallet-rpc
// in V1.0 — they are stored in UI-local .json files (LocalDataManager, st-017).
// get_accounts therefore returns indices + base addresses only.
//
// All methods are synchronous in this header (the async wrappers are emitted
// from WalletRpcClient itself).
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_BALANCE_API_H
#define JUDECOIN_WALLET_CORE_RPC_BALANCE_API_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// BalanceInfo
//
// Atomic-unit balance for a wallet (or sub-account). `unlocked` is the portion
// currently spendable; `total` includes locked outputs. Both are uint64
// because amounts in atomic units fit comfortably within 64 bits.
// -----------------------------------------------------------------------------
struct BalanceInfo
{
    quint64 total   = 0;
    quint64 unlocked = 0;

    /// Parse a get_balance / get_unlocked_balance result. Returns false if
    /// either field is missing or non-integer.
    static bool parse(const QJsonObject& obj, BalanceInfo& out);

    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// AccountAddress
//
// Single sub-address record. `address` is the 95-char Loki/Oxen base58 public
// address (V1.0 mainnet only). `address_index` is the sub-address index
// (0 = primary). `account_index` is the wallet account index.
// -----------------------------------------------------------------------------
struct AccountAddress
{
    quint64 account_index  = 0;
    quint64 address_index  = 0;
    QString address;
    bool    used           = false;  // G27: wallet-rpc marks true when funds received
    quint64 balance        = 0;  // st-131: from get_accounts per-account total
    quint64 unlocked_balance = 0;  // st-131: from get_accounts per-account available

    /// Parse a single sub-address object from get_address / get_accounts.
    /// Returns false on missing fields.
    static bool parse(const QJsonObject& obj, AccountAddress& out);

    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// AccountList
//
// All sub-addresses belonging to a wallet, returned by get_accounts. `primary`
// is duplicated from `addresses[0]` for the UI's convenience (the UI
// typically only displays the primary address prominently).
// -----------------------------------------------------------------------------
struct AccountList
{
    QVector<AccountAddress> addresses;
    QString primary;     // == addresses[0].address if non-empty

    /// Parse the { subaddr_accounts: [...] } envelope. Tolerates an absent
    /// field (returns true with empty addresses) because some wallet-rpc
    /// versions omit it for new wallets.
    static bool parse(const QJsonObject& obj, AccountList& out);

    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// WalletVersion
//
// get_version returns { version: <number>, release: <bool|string> }.
//
// Field reference (per Loki/Oxen v3.2.0 binary `judecoin-wallet-rpc`):
//   version          - numeric, RPC build version (e.g. 65558 for v3.2.0)
//   is_release       - bool, true for tagged release builds (e.g. v3.2.0-release),
//                      false for dev/RC branches. Real v3.2.0 wallet-rpc
//                      emits `release` as a BOOL, not a string.
//   release          - QString, backward-compatible mirror of `release`:
//                      - bool true  -> "true"
//                      - bool false -> "false"
//                      - string     -> the string verbatim
//                      Kept so existing call-sites that test `release.contains(...)`
//                      against a mocked STRING response continue to work.
//   version_string   - human-readable semver derived from `version`
//                      (65558 -> "v3.2.0", see WalletVersion::parse for the
//                      full mapping table).
// -----------------------------------------------------------------------------
struct WalletVersion
{
    quint64 version = 0;
    bool    is_release = false;
    QString release;        // legacy: string-mirror of the bool field
    QString version_string; // e.g. "v3.2.0"

    static bool parse(const QJsonObject& obj, WalletVersion& out);

    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// BlockHeight
//
// get_height returns { height: <number> }. Thin wrapper so the UI can mock
// responses uniformly.
// -----------------------------------------------------------------------------
struct BlockHeight
{
    quint64 height = 0;

    static bool parse(const QJsonObject& obj, BlockHeight& out);

    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_BALANCE_API_H
