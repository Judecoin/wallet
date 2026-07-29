// =============================================================================
// Judecoin Wallet - OtherApi.h (st-005, "other" module)
//
// Strongly-typed request/response structs for the 4 wallet-rpc methods
// WalletRpcClient exposes as OtherApi in V1.0:
//
//   create_wallet            -> { }    (empty success body)
//   open_wallet               -> { }    (empty success body)
//   close_wallet              -> { }    (empty success body)
//   change_wallet_password    -> { }    (empty success body)
//   generate_from_keys         -> { }    (empty success body)  (st-011)
//   generate_from_json         -> { }    (empty success body)  (st-011)
//
// V5.4.5 storage_policy is now extended by st-011: generate_from_keys and
// generate_from_json ARE exposed here (but V5.4.7 still holds: UI never
// reads .keys files; wallet-rpc does all encryption). View-only
// wallet recovery from spend key + view key is a V1.1 feature.
//
// V5.4.7: open_wallet is the *single* password verifier. The UI never reads
// key files. close_wallet must be called before open_wallet on an already-
// open wallet (wallet-rpc returns -6 "wallet already open" otherwise).
//
// V5.4.x V1.0 cut: set_description / get_description / set_attribute /
// get_attribute / set_log_level / set_log_categories / set_daemon /
// mining_status / rescan_blockchain are documented in CLI_COMMANDS.md 8.2
// under "other" but not exposed here. These are admin / debug endpoints
// not needed for the V1.0 user-facing UI flow.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_OTHER_API_H
#define JUDECOIN_WALLET_CORE_RPC_OTHER_API_H

#include <QJsonObject>
#include <QString>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// CreateWalletRequest
//
// Parameters for create_wallet. Fields:
//   filename       - filename within --wallet-dir (no path separators)
//   password       - wallet password (used as the wallet-rpc encryption key)
//   language       - seed language ("English" | "Chinese" | ...)
//                   default per docs/DECISIONS.md D011: "English"
//
// wallet-rpc returns {} on success. Any failure (filename clash, bad
// password length, etc.) surfaces as a JSON-RPC error.code (-14 wallet not
// found, -1 wrong number of arguments, etc.).
// -----------------------------------------------------------------------------
struct CreateWalletRequest
{
    QString filename;
    QString password;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    QString language = QStringLiteral("English");

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, CreateWalletRequest& out);
};

// -----------------------------------------------------------------------------
// QueryKeyRequest (st-151)
//
// Parameters for query_key — queries wallet keys/mnemonic from an
// open wallet. Used post-create_wallet to fetch the server-generated
// mnemonic seed so the UI can display it to the user.
//
// Fields:
//   key_type - "mnemonic" | "view_key" | "spend_key" | ...
//
// Response: {"key": "..."} on success.
// -----------------------------------------------------------------------------
struct QueryKeyRequest
{
    QString key_type = QStringLiteral("mnemonic");

    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// OpenWalletRequest
//
// Parameters for open_wallet. Same filename + password as create_wallet.
// V5.4.7: this is the single password verifier. UI must NEVER read .keys
// files; the password goes from the user to wallet-rpc through this method.
// -----------------------------------------------------------------------------
struct OpenWalletRequest
{
    QString filename;
    QString password;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, OpenWalletRequest& out);
};

// -----------------------------------------------------------------------------
// CloseWalletRequest
//
// Parameters for close_wallet. wallet-rpc accepts an empty body; filename
// is optional (closes whatever is currently open if omitted).
// -----------------------------------------------------------------------------
struct CloseWalletRequest
{
    QString filename;   // empty = close currently-open wallet

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, CloseWalletRequest& out);
};

// -----------------------------------------------------------------------------
// ChangeWalletPasswordRequest
//
// Parameters for change_wallet_password. All three fields are mandatory;
// wallet-rpc returns code -1 if any is missing. old_password is verified
// before new_password is applied.
// -----------------------------------------------------------------------------
struct ChangeWalletPasswordRequest
{
    QString old_password;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    QString new_password;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    QString filename;       // empty = currently-open wallet

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, ChangeWalletPasswordRequest& out);
};


// -----------------------------------------------------------------------------
// GenerateFromKeysRequest
//
// Parameters for generate_from_keys - the wallet-rpc "view-only / restore"
// variant that derives a wallet from an address + viewkey pair. This is the
// path V1.0 uses to import a public address + viewkey as a watch-only wallet
// or to attach metadata to a previously-known address. fullspendkey variants
// exist for hardware / cold-storage restore but are not exposed in V1.0.
//
// V1.0 fields (per devplan/st-011 spec_notes):
//   filename         - wallet filename within --wallet-dir
//   password         - new wallet password (used by wallet-rpc as the
//                      .keys encryption key per V5.4.7)
//   address          - public address (wallet-rpc validates the prefix
//                      /length) -- Case 4 B-template
//   viewkey          - private view key (hex) -- Case 4 B-template
//   spendkey         - private spend key (hex) -- Case 4 B-template
//                      (added in st-022; empty for Case 3 seed restore)
//   seed             - 25-word mnemonic seed -- Case 3 (added in st-012;
//                      empty for Case 4 backup restore)
//   restore_height   - block height to start scanning from (added st-022)
//   autosave_current - wallet-rpc autosave flag (added st-022)
//
// V5.4.7: wallet-rpc handles all encryption / derivation. The UI never
// touches the .keys file. wallet-rpc returns {} on success.
// -----------------------------------------------------------------------------
struct GenerateFromKeysRequest
{
    QString filename;
    QString password;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    QString address;
    QString viewkey;    // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().

    // st-022: optional spend private key for Case 4 (B-template backup
    // restore via BackupParser). Wallet-rpc validates the spendkey
    // against the supplied address; mismatch surfaces as RPC error.
    // Empty for Case 3 (seed) where wallet-rpc derives the spendkey.
    QString spendkey;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().

    // st-012: optional 25-word mnemonic seed for Case 3 recovery
    // (docs/UI_DETAILS_TODO.md Case 3). When non-empty, wallet-rpc is
    // expected to derive the address + viewkey + spendkey from the seed
    // and skip the user-supplied address/viewkey (V5.4.7: wallet-rpc is
    // the sole encryptor and key-deriver; the UI never derives keys).
    // address/viewkey/spendkey may be empty when seed is provided.
    QString seed;       // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().

    // st-022: optional restore-height hint for wallet-rpc. 0 = scan
    // from genesis (slow, full-chain). Non-zero = scan only from this
    // height onward (fast). Wallet-rpc ignores the value when seed is
    // set (it auto-detects the seed's birth height).
    quint64 restore_height = 0;

    // st-022: optional autosave_current flag (wallet-rpc default false).
    // Forwarded verbatim so the UI can let power users opt in.
    bool autosave_current = false;

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, GenerateFromKeysRequest& out);
};

// -----------------------------------------------------------------------------
// RestoreDeterministicRequest (st-150)
//
// Parameters for restore_deterministic_wallet — the wallet-rpc "seed
// restore" path that restores a wallet from a 25-word mnemonic seed.
// wallet-rpc derives address + viewkey + spendkey from the seed and
// creates a new wallet file.
//
// Fields:
//   filename       - wallet filename within --wallet-dir
//   password       - new wallet password (encryption key per V5.4.7)
//   seed           - 25-word mnemonic (space-separated, monero format)
//   restore_height - block height to start scanning from (0 = genesis)
//   language       - seed language, default "English"
//
// wallet-rpc returns {} on success.
// -----------------------------------------------------------------------------
struct RestoreDeterministicRequest
{
    QString filename;
    QString password;   // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    QString seed;       // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    quint64 restore_height = 0;
    QString language = QStringLiteral("English");

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, RestoreDeterministicRequest& out);
};

// -----------------------------------------------------------------------------
// GenerateFromJsonRequest
//
// Parameters for generate_from_json - the wallet-rpc "advanced restore" path
// that takes a full JSON snapshot (e.g. an exported wallet-keys file content
// parsed into a JSON object). wallet-rpc re-derives keys from the JSON and
// re-encrypts under the new password per V5.4.7.
//
// V1.0 fields:
//   filename    - wallet filename within --wallet-dir
//   password    - new wallet password
//   json_string - the JSON payload (string of text). Wallet-rpc parses it;
//                 we forward verbatim from UI without ever touching .keys.
//
// V5.4.7: UI does not parse or store .keys content. We forward the
// user-provided JSON text to wallet-rpc as-is.
// -----------------------------------------------------------------------------
struct GenerateFromJsonRequest
{
    QString filename;
    QString password;    // SENSITIVE (P0 #5): scrubbed in toJson() via secureClear().
    QString json_string; // SENSITIVE (P0 #5): full JSON snapshot; scrubbed in toJson().

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, GenerateFromJsonRequest& out);
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_OTHER_API_H
