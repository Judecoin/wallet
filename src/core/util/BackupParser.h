// =============================================================================
// Judecoin Wallet - BackupParser (st-022)
//
// Case 4 backup-restore B-template parser. Takes a user-supplied payload
// (file content, pasted text, or QR-decoded string) and extracts the 5
// fields wallet-rpc.generate_from_keys needs:
//
//   spendkey, viewkey, address, restore_height, date
//
// plus 2 secondary fields that V5.4.7 lets the UI touch:
//
//   filename  (target wallet name; user picks this in the wizard)
//   password  (typed twice; never persisted)
//
// Three input formats are auto-detected by sniffing the first non-blank
// character(s) of the payload (V5.4.13 design choice - keep one parser,
// three shapes, no library needed):
//
//   1. JSON  ({...)         - canonical, B-template fields are top-level keys
//   2. Text  (spend private:) - legacy monero export "key=value" lines
//                                (one key per line, colon or equals separator)
//   3. QR    (25 words)     - bare 25-word monero mnemonic; we treat this as
//                                a fallback path that flags the payload as
//                                MnemonicAndContinue so the UI can route it
//                                to the existing Case 3 mnemonic flow instead.
//
// V5.4.7 compliance (the "UI does not decrypt" rule):
//   - we only parse + validate format. We never derive keys, never re-encrypt
//     .keys, never read .keys files. The keys land in wallet-rpc via
//     generate_from_keys, which is the sole decryptor/encryptor.
//   - the parser keeps the spendkey/viewkey strings in memory only long
//     enough to populate GenerateFromKeysRequest, then drops them.
//
// V5.4.5 compliance (the "UI never persists secrets" rule):
//   - ParseResult does not implement QDebug / operator<<, and the field
//     accessors return QString by value (copy), not by reference, so a
//     caller cannot accidentally leave a reference to the secret string.
//   - on parse failure we keep no partial state (no half-populated
//     ParseResult with a leaked spendkey).
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_H
#define JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../util/SecureBuffer.h"

namespace judecoin_wallet::core::util {

// -----------------------------------------------------------------------------
// BackupFormat
//
// What shape did the input payload have? Used by the UI to choose the right
// success copy ("Imported JSON backup" vs. "Imported text backup" vs.
// "Detected 25-word mnemonic - switching to recovery flow").
// -----------------------------------------------------------------------------
enum class BackupFormat {
    Unknown,        // not parseable; see ParseResult::reason
    Json,           // { "version": 1, "address": ..., ... }
    TextKeyValue,   // spend private: <hex>\nview key: <hex>\n...
    Mnemonic25      // 25 monero English words (whitespace-separated)
};

// -----------------------------------------------------------------------------
// ParseResult
//
// Outcome of BackupParser::parse. ok=true iff every required field is
// present AND passes the per-field syntax checks below; address/spendkey/
// viewkey match is NOT checked here (we have no chain access in the
// parser; the wallet-rpc.generate_from_keys RPC does the on-chain check
// in its "daemon check #2" pass and surfaces a mismatch as RPC error).
//
// Required fields (ok=true):
//   - address        : non-empty, 95-106 chars, starts with a recognised
//                      prefix (judecoin mainnet 'J' / testnet '9' /
//                      stagenet 'B' - we accept any 1-char alpha prefix to
//                      stay loose across forks)
//   - spendkey       : 64-char lowercase hex (32 bytes)
//   - viewkey        : 64-char lowercase hex (32 bytes)
//
// Optional fields (always populated when ok=true; default if missing):
//   - restore_height : 0 = full scan from genesis (UI shows the slow-scan
//                      progress bar per V5.4.4-A). Non-zero must be a valid
//                      decimal integer (validated by QString::toULongLong).
//   - date           : ISO 8601 string ("2026-07-04T12:34:56Z"). Defaults
//                      to empty if missing; UI only uses it for display.
//   - format         : which of the 3 shapes we detected
// -----------------------------------------------------------------------------
struct ParseResult
{
    /// True iff every required field is present and passes syntax checks.
    bool ok = false;

    /// What shape the input was (Unknown when ok=false due to a syntax
    /// error; preserved as the detected shape when ok=false because the
    /// shape was recognised but a field was missing or malformed).
    BackupFormat format = BackupFormat::Unknown;

    /// Human-readable failure reason (empty when ok=true).
    /// Examples: "JSON: missing required field \"spendkey\"",
    ///           "spendkey must be 64 hex chars (got 63)",
    ///           "restore_height must be a non-negative integer (got \"abc\")".
    QString reason;

    /// Detected / normalised fields (populated when ok=true; empty otherwise
    /// so partial parses never leak half-extracted secrets).
    QString address;
    QString spendkey;    // SENSITIVE (P0 #8): zeroed in ~ParseResult via secureClear().
    QString viewkey;     // SENSITIVE (P0 #8): zeroed in ~ParseResult via secureClear().
    quint64 restoreHeight = 0;
    QString date;

    /// 25-word mnemonic (only set when format==Mnemonic25; lets the UI
    /// hand the same words straight to Case 3 recovery without a second
    /// paste round-trip). Empty for Json / TextKeyValue formats.
    QStringList mnemonicWords;

    ~ParseResult()
    {
        judecoin_wallet::core::util::secureClear(spendkey);
        judecoin_wallet::core::util::secureClear(viewkey);
    }
};

// -----------------------------------------------------------------------------
// BackupParser
//
// Stateless utility - all methods are static. We deliberately do not make
// it a QObject: the parser has no state, no signals, and no QML bindings,
// so a free function-style class is the cleanest fit.
//
// Thread safety: BackupParser::parse is reentrant (it touches no shared
// state). UI workers can call it from any thread.
// -----------------------------------------------------------------------------
class BackupParser
{
public:
    BackupParser() = delete;

    /// Parse a user-supplied payload. See struct docs for the rules.
    /// Empty / whitespace-only input returns ok=false, reason="empty input".
    static ParseResult parse(const QString& payload);

    /// Detect the shape without actually parsing fields. Useful for the
    /// UI to render a hint ("looks like JSON...") before the user clicks
    /// "Restore".
    static BackupFormat detectFormat(const QString& payload);

    /// Lowercase hex validator (no length check). Returns true iff the
    /// string is 1+ chars and every char is 0-9 / a-f.
    static bool isLowerHex(const QString& s);

    /// Strict monero-style address validator. Accepts any 1-char alpha
    /// prefix + base58 payload of length 92-105 (typical mainnet = 95,
    /// testnet/stagenet may be 106). We are deliberately permissive
    /// here: wallet-rpc does the authoritative check.
    static bool looksLikeAddress(const QString& s);

    /// Format the ParseResult as a one-line summary for log / debug
    /// (never logs the secret keys - returns "<format> ok" only).
    /// Safe to call when ok=false (returns "error: <reason>").
    static QString summary(const ParseResult& r);

private:
    // -- per-format implementations (only the public `parse` calls these) --
    static ParseResult parseJson(const QString& payload);
    static ParseResult parseTextKeyValue(const QString& payload);
    static ParseResult parseMnemonic25(const QString& payload);

    // -- shared validators (used by all three) --
    static ParseResult validateRequiredFields(ParseResult& r);  // populates r.reason, returns r
    static QString normaliseHexKey(const QString& raw,
                                    QString* outNormalised);
};

} // namespace judecoin_wallet::core::util

#endif // JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_H
