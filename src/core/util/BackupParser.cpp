// =============================================================================
// Judecoin Wallet - BackupParser implementation (st-022)
//
// See BackupParser.h for the design rationale (V5.4.7 / V5.4.5 / V5.4.13).
//
// Parsing strategy:
//   1. detectFormat()  sniff first non-blank char(s) -> pick a parser
//   2. parseJson()     QJsonDocument strict-mode parse + field validation
//   3. parseTextKeyValue()  line-based key/value extraction with two
//                            accepted separators (":" or "=")
//   4. parseMnemonic25()  word-list count + (optionally) wordlist check
//                          against MnemonicWordList (re-used from Case 3).
//   5. validateRequiredFields()  shared check applied to all three
// =============================================================================

#include "core/util/BackupParser.h"
#include "core/wallet/MnemonicWordList.h"

#include <QChar>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>

namespace judecoin_wallet::core::util {

using judecoin_wallet::core::wallet::MnemonicWordList;

// =============================================================================
// Public API
// =============================================================================

BackupFormat BackupParser::detectFormat(const QString& payload)
{
    if (payload.trimmed().isEmpty()) {
        return BackupFormat::Unknown;
    }

    // JSON: opens with '{'
    if (payload.trimmed().startsWith(QLatin1Char('{'))) {
        return BackupFormat::Json;
    }

    // Text KV: contains a 'spend private:' or 'spendkey' / 'viewkey' line
    // (case-insensitive prefix match). The legacy monero export uses
    // "spend private:" / "view key:" / "public address:"; the modern
    // judecoin export uses "spendkey:" / "viewkey:" / "address:". We
    // accept either.
    const QString lower = payload.toLower();
    if (lower.contains(QStringLiteral("spend private:")) ||
        lower.contains(QStringLiteral("spendkey:")) ||
        lower.contains(QStringLiteral("view key:")) ||
        lower.contains(QStringLiteral("viewkey:"))) {
        return BackupFormat::TextKeyValue;
    }

    // 25-word mnemonic: count whitespace-separated tokens. We don't
    // require wordlist validity here (that's parseMnemonic25's job).
    const QStringList tokens = payload.split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    if (tokens.size() == 25) {
        return BackupFormat::Mnemonic25;
    }

    return BackupFormat::Unknown;
}

ParseResult BackupParser::parse(const QString& payload)
{
    ParseResult r;
    if (payload.trimmed().isEmpty()) {
        r.ok = false;
        r.format = BackupFormat::Unknown;
        r.reason = QStringLiteral("empty input");
        return r;
    }

    const BackupFormat fmt = detectFormat(payload);
    r.format = fmt;
    switch (fmt) {
        case BackupFormat::Json:           return parseJson(payload);
        case BackupFormat::TextKeyValue:   return parseTextKeyValue(payload);
        case BackupFormat::Mnemonic25:     return parseMnemonic25(payload);
        case BackupFormat::Unknown:
        default:
            r.ok = false;
            r.reason = QStringLiteral(
                "unrecognised format (expected JSON object, 'spend private:' "
                "text, or 25-word mnemonic)");
            return r;
    }
}

bool BackupParser::isLowerHex(const QString& s)
{
    if (s.isEmpty()) return false;
    for (QChar c : s) {
        const ushort u = c.unicode();
        const bool isDigit = (u >= '0' && u <= '9');
        const bool isLower = (u >= 'a' && u <= 'f');
        if (!isDigit && !isLower) return false;
    }
    return true;
}

bool BackupParser::looksLikeAddress(const QString& s)
{
    // Permissive: 90 < len < 110, starts with a single uppercase letter
    // or a digit 1-9, rest is base58-ish (we don't re-check base58 here
    // because wallet-rpc does the authoritative validation; this is just
    // a "doesn't look like junk" gate).
    if (s.size() < 90 || s.size() > 110) return false;
    const QChar first = s.at(0);
    const ushort u = first.unicode();
    const bool okFirst = (u >= 'A' && u <= 'Z') || (u >= '1' && u <= '9');
    if (!okFirst) return false;
    for (int i = 1; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const ushort cu = c.unicode();
        // base58 alphabet excludes 0OIl; we accept anything alphanumeric
        // for the same reason (loose pre-check; wallet-rpc is strict).
        if (!((cu >= '0' && cu <= '9') ||
              (cu >= 'A' && cu <= 'Z') ||
              (cu >= 'a' && cu <= 'z'))) {
            return false;
        }
    }
    return true;
}

QString BackupParser::summary(const ParseResult& r)
{
    if (!r.ok) {
        return QStringLiteral("error: %1").arg(r.reason);
    }
    switch (r.format) {
        case BackupFormat::Json:         return QStringLiteral("JSON ok");
        case BackupFormat::TextKeyValue: return QStringLiteral("text ok");
        case BackupFormat::Mnemonic25:   return QStringLiteral("mnemonic ok");
        default:                          return QStringLiteral("unknown ok");
    }
}

// =============================================================================
// JSON parser
// =============================================================================

ParseResult BackupParser::parseJson(const QString& payload)
{
    ParseResult r;
    r.format = BackupFormat::Json;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(
        payload.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        r.ok = false;
        r.reason = QStringLiteral("JSON parse error at offset %1: %2")
                       .arg(perr.offset)
                       .arg(perr.errorString());
        return r;
    }
    if (!doc.isObject()) {
        r.ok = false;
        r.reason = QStringLiteral("JSON: top-level value must be an object");
        return r;
    }
    const QJsonObject obj = doc.object();

    // Accept both 'spendkey' and 'spend_key' (legacy export used underscore).
    auto pick = [&](const QStringList& keys) -> QString {
        for (const QString& k : keys) {
            if (obj.contains(k) && obj.value(k).isString()) {
                return obj.value(k).toString();
            }
        }
        return QString();
    };

    r.address = pick({QStringLiteral("address"),
                       QStringLiteral("public_address")});
    r.spendkey = pick({QStringLiteral("spendkey"),
                        QStringLiteral("spend_key"),
                        QStringLiteral("spend_private")});
    r.viewkey = pick({QStringLiteral("viewkey"),
                       QStringLiteral("view_key")});

    // restore_height: accept both numeric and string forms; reject negative.
    if (obj.contains(QStringLiteral("restore_height")) ||
        obj.contains(QStringLiteral("height"))) {
        const QJsonValue v = obj.contains(QStringLiteral("restore_height"))
                                 ? obj.value(QStringLiteral("restore_height"))
                                 : obj.value(QStringLiteral("height"));
        if (v.isDouble()) {
            const double d = v.toDouble();
            if (d < 0.0) {
                r.ok = false;
                r.reason = QStringLiteral(
                    "restore_height must be >= 0 (got %1)").arg(d);
                return r;
            }
            r.restoreHeight = static_cast<quint64>(d);
        } else if (v.isString()) {
            bool ok = false;
            const quint64 parsed = v.toString().toULongLong(&ok);
            if (!ok) {
                r.ok = false;
                r.reason = QStringLiteral(
                    "restore_height must be a non-negative integer "
                    "(got \"%1\")").arg(v.toString());
                return r;
            }
            r.restoreHeight = parsed;
        } else if (!v.isNull()) {
            r.ok = false;
            r.reason = QStringLiteral(
                "restore_height must be a number or numeric string");
            return r;
        }
    }

    // date: optional ISO 8601 string; if present we just store it as-is
    // (we don't try to parse it here because the UI only displays it).
    if (obj.contains(QStringLiteral("date"))) {
        const QJsonValue v = obj.value(QStringLiteral("date"));
        if (v.isString()) {
            r.date = v.toString();
        }
    }

    return validateRequiredFields(r);
}

// =============================================================================
// Text key/value parser
// =============================================================================

ParseResult BackupParser::parseTextKeyValue(const QString& payload)
{
    ParseResult r;
    r.format = BackupFormat::TextKeyValue;

    // Walk line by line. Accept "key: value" or "key=value" (first
    // occurrence wins; later duplicates are ignored for safety).
    const QStringList lines = payload.split(QChar('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        // skip comments
        if (line.startsWith(QLatin1Char('#'))) continue;

        // Find first separator (':' or '='); allow ':' after the key
        // name even when value contains more colons (e.g. IPv6).
        int sepIdx = -1;
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line.at(i);
            if (c == QLatin1Char(':') || c == QLatin1Char('=')) {
                sepIdx = i;
                break;
            }
        }
        if (sepIdx <= 0) continue;

        const QString key = line.left(sepIdx).trimmed().toLower();
        QString value = line.mid(sepIdx + 1).trimmed();
        // Strip optional surrounding quotes (legacy exports sometimes
        // wrap hex in double quotes).
        if (value.size() >= 2 &&
            ((value.startsWith(QLatin1Char('"')) &&
              value.endsWith(QLatin1Char('"'))) ||
             (value.startsWith(QLatin1Char('\'')) &&
              value.endsWith(QLatin1Char('\''))))) {
            value = value.mid(1, value.size() - 2);
        }

        // Apply only the first occurrence of each known key.
        if (r.address.isEmpty() &&
            (key == QStringLiteral("address") ||
             key == QStringLiteral("public_address") ||
             key == QStringLiteral("public address"))) {
            r.address = value;
        } else if (r.spendkey.isEmpty() &&
                   (key == QStringLiteral("spendkey") ||
                    key == QStringLiteral("spend_key") ||
                    key == QStringLiteral("spend private") ||
                    key == QStringLiteral("spend_private"))) {
            r.spendkey = value;
        } else if (r.viewkey.isEmpty() &&
                   (key == QStringLiteral("viewkey") ||
                    key == QStringLiteral("view_key") ||
                    key == QStringLiteral("view key") ||
                    key == QStringLiteral("view_private"))) {
            r.viewkey = value;
        } else if (key == QStringLiteral("restore_height") ||
                   key == QStringLiteral("height")) {
            if (r.restoreHeight == 0) {
                bool ok = false;
                const quint64 parsed = value.toULongLong(&ok);
                if (ok) r.restoreHeight = parsed;
            }
        } else if (r.date.isEmpty() && key == QStringLiteral("date")) {
            r.date = value;
        }
    }

    return validateRequiredFields(r);
}

// =============================================================================
// 25-word mnemonic parser
// =============================================================================

ParseResult BackupParser::parseMnemonic25(const QString& payload)
{
    ParseResult r;
    r.format = BackupFormat::Mnemonic25;

    QStringList words = payload.split(
        QRegularExpression(QStringLiteral("\\s+")),
        Qt::SkipEmptyParts);
    // Normalise: lowercase + trim.
    for (QString& w : words) {
        w = w.trimmed().toLower();
    }
    if (words.size() != 25) {
        r.ok = false;
        r.reason = QStringLiteral(
            "expected exactly 25 words, got %1").arg(words.size());
        return r;
    }

    // Wordlist check - re-use Case 3's embedded English wordlist. We
    // accept up to 0 invalid words silently in V1.0 (different forks
    // have different wordlists; wallet-rpc does the authoritative check
    // during generate_from_keys with seed).
    QVector<int> invalidIndices;
    for (int i = 0; i < words.size(); ++i) {
        if (!MnemonicWordList::isValidEnglishWord(words.at(i))) {
            invalidIndices.push_back(i);
        }
    }

    // Even if some words aren't in our wordlist, still hand the words
    // through - the caller (Case 3 recovery) will route them to
    // generate_from_keys with seed and wallet-rpc will do its own check.
    r.mnemonicWords = words;
    r.spendkey.clear();
    r.viewkey.clear();
    r.address.clear();
    r.restoreHeight = 0;

    // Mnemonic25 is a "fallback" path: the ParseResult is technically
    // ok=false so the UI is forced to render the "switch to recovery"
    // copy; the words are still preserved in mnemonicWords for the
    // caller to consume. (V5.4.13: a 25-word backup is really Case 3.)
    r.ok = false;
    r.reason = invalidIndices.isEmpty()
                   ? QStringLiteral(
                         "25-word mnemonic detected - this is Case 3 "
                         "(seed recovery); please use the recovery flow")
                   : QStringLiteral(
                         "25-word mnemonic detected but %1 word(s) are not "
                         "in the monero English wordlist - please use the "
                         "recovery flow")
                         .arg(invalidIndices.size());
    return r;
}

// =============================================================================
// Shared required-field validation
// =============================================================================

ParseResult BackupParser::validateRequiredFields(ParseResult& r)
{
    // Mnemonic25 path: leave ok alone (it set its own ok=false earlier).
    if (r.format == BackupFormat::Mnemonic25) {
        return r;
    }

    if (r.address.isEmpty()) {
        r.ok = false;
        r.reason = QStringLiteral("missing required field: address");
        // Wipe any partial state to avoid leaking half-parsed secrets.
        r.spendkey.clear();
        r.viewkey.clear();
        return r;
    }
    if (!looksLikeAddress(r.address)) {
        r.ok = false;
        r.reason = QStringLiteral(
            "address has wrong shape (got %1 chars, expected 90-110; "
            "must start with a letter or digit 1-9)").arg(r.address.size());
        r.address.clear();
        r.spendkey.clear();
        r.viewkey.clear();
        return r;
    }

    if (r.spendkey.isEmpty()) {
        r.ok = false;
        r.reason = QStringLiteral("missing required field: spendkey");
        r.viewkey.clear();
        return r;
    }
    QString normalisedSpend;
    const QString sStatus = normaliseHexKey(r.spendkey, &normalisedSpend);
    if (!sStatus.isEmpty()) {
        r.ok = false;
        r.reason = sStatus;
        r.viewkey.clear();
        return r;
    }
    r.spendkey = normalisedSpend;

    if (r.viewkey.isEmpty()) {
        r.ok = false;
        r.reason = QStringLiteral("missing required field: viewkey");
        r.spendkey.clear();
        return r;
    }
    QString normalisedView;
    const QString vStatus = normaliseHexKey(r.viewkey, &normalisedView);
    if (!vStatus.isEmpty()) {
        r.ok = false;
        r.reason = vStatus;
        r.spendkey.clear();
        return r;
    }
    r.viewkey = normalisedView;

    r.ok = true;
    r.reason.clear();
    return r;
}

QString BackupParser::normaliseHexKey(const QString& raw,
                                       QString* outNormalised)
{
    if (raw.size() != 64) {
        return QStringLiteral(
            "hex key must be 64 chars (got %1)").arg(raw.size());
    }
    if (!isLowerHex(raw)) {
        // Try case-insensitive accept + lowercase normalise.
        QString lower = raw.toLower();
        if (!isLowerHex(lower)) {
            return QStringLiteral(
                "hex key contains non-hex characters");
        }
        *outNormalised = lower;
        return QString();
    }
    *outNormalised = raw;
    return QString();
}

} // namespace judecoin_wallet::core::util
