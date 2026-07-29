// =============================================================================
// Judecoin Wallet - VersionChecker (st-008 + st-115 extension) implementation
//
// Pure parsing utility; no Qt event loop, no signals, no threads. All
// operations complete in microseconds - suitable for synchronous use
// during startup or from any health-poll callback.
//
// st-115 (2026-07-13): added "v3.2.1-fd067b1ab" support. The v3.2.1
// daemon's get_info.version field looks like "3.2.1-fd067b1ab"
// (no leading 'v'), the binary's --version output looks like
// "Judecoin 'Judecoin 3.2.1' (v3.2.1-fd067b1ab)" (with the
// 'v'-prefixed form and a different codename from v3.2.0). Both forms
// normalise to VersionInfo {major=3, minor=2, patch=1, suffix="fd067b1ab"}
// via the existing regex; the codename table at the top of this file
// was extended to map "3.2.1" -> "Judecoin 3.2.1" for diagnostics.
// =============================================================================

#include "core/version/VersionChecker.h"

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace judecoin_wallet::core::version {

namespace {

// -----------------------------------------------------------------------------
// Codename table
// -----------------------------------------------------------------------------
// Loki's v3.x line adopted the "Chamaeleo" naming scheme: each minor
// version uses a different chameleon species. V1.0 only ships the
// command-set JSON for v3.2.0 ("Chamaeleo dilepis"); future versions
// can be appended here as the JSON files are authored. Unknown
// versions get an empty codename (still parseable, still testable).
//
// Codename references:
//   v3.0.0 - n/a (Loki/Chamaeleo line begins at 3.x)
//   v3.1.0 - n/a
//   v3.2.0 - "Chamaeleo dilepis"  (flap-necked chameleon)
//   v3.2.1 - "Judecoin 3.2.1"     (st-115: binary dropped the Chamaeleo
//                                  naming; official build is identified
//                                  by the version string itself)
// -----------------------------------------------------------------------------
QString codenameFor(int major, int minor, int patch)
{
    static const QHash<QString, QString> kTable = {
        {QStringLiteral("3.2.0"), QStringLiteral("Chamaeleo dilepis")},
        {QStringLiteral("3.2.1"), QStringLiteral("Judecoin 3.2.1")},
        // Add new entries as command-set JSON files are added.
    };
    const QString key = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    return kTable.value(key);
}

// -----------------------------------------------------------------------------
// Parsing helpers
// -----------------------------------------------------------------------------

// Try to extract the (X, Y, Z, suffix) tuple from a bare "vX.Y.Z[-suffix]"
// string. Returns VersionInfo with valid == false on mismatch.
VersionInfo parseBare(const QString& trimmed)
{
    // Accept: optional leading 'v', then 3 dotted integers, then an
    // optional `-suffix` / `_suffix` / `+suffix` / `.suffix`. The suffix
    // itself may contain dots and dashes (e.g. "v3.2.0-rc1.test").
    static const QRegularExpression kBareRe(
        QStringLiteral(R"(^v?(\d+)\.(\d+)\.(\d+)(?:[-_+]([\w-]+))?$)"));

    const QRegularExpressionMatch m = kBareRe.match(trimmed);
    if (!m.hasMatch()) {
        return VersionInfo{};
    }

    bool ok = false;
    VersionInfo info;
    info.major = m.captured(1).toInt(&ok);
    if (!ok || info.major < 0) { return VersionInfo{}; }
    info.minor = m.captured(2).toInt(&ok);
    if (!ok || info.minor < 0) { return VersionInfo{}; }
    info.patch = m.captured(3).toInt(&ok);
    if (!ok || info.patch < 0) { return VersionInfo{}; }
    if (m.lastCapturedIndex() >= 4 && !m.captured(4).isEmpty()) {
        info.suffix = m.captured(4);
    }
    info.codename = codenameFor(info.major, info.minor, info.patch);
    info.valid = true;
    return info;
}

// Try to extract the (X, Y, Z, suffix, codename) tuple from a wrapped
// full version_string like "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)".
//
// We deliberately accept both straight single quotes (') and Unicode
// curly quotes (\u2018 / \u2019) because some build chains pretty-print
// with smart quotes. Anything else inside the parens is treated as a
// version we delegate to parseBare().
VersionInfo parseWrapped(const QString& trimmed)
{
    // NAME 'CODENAME' (VERSION)
    //   ^^^^^            - any non-greedy run of non-paren, non-quote chars
    //        ^^^^^^^^^^   - quoted codename (straight or smart quotes)
    //                     ^^^^^^^   - the version we care about
    static const QRegularExpression kWrappedRe(
        QStringLiteral(
            // PCRE2 Unicode syntax: \x{NNNN}. Qt's QRegularExpression
            // is PCRE2-backed and rejects Perl's \uHHHH form, so we
            // use the explicit curly-brace hex escape for the smart
            // quotes we want to accept (\u2018 / \u2019) alongside
            // the ASCII single quote (\u0027).
            R"(^[^()]*[\x{27}\x{2018}]([^\x{27}\x{2019}]+)[\x{27}\x{2019}]\s*\(\s*(.+?)\s*\)$)"));

    const QRegularExpressionMatch m = kWrappedRe.match(trimmed);
    if (!m.hasMatch()) {
        return VersionInfo{};
    }

    VersionInfo info = parseBare(m.captured(2).trimmed());
    if (info.valid) {
        // The wrapped form embeds the codename directly, so it overrides
        // anything the lookup table would have returned. This also
        // gracefully handles future versions whose codename we haven't
        // hard-coded yet - the daemon's authoritative name wins.
        info.codename = m.captured(1).trimmed();
    }
    return info;
}

} // namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

VersionInfo VersionChecker::parse(const QString& versionString)
{
    const QString trimmed = versionString.trimmed();
    if (trimmed.isEmpty()) {
        return VersionInfo{};  // valid == false
    }

    // Try the wrapped form first because it's strictly more specific
    // (requires parens, quotes, AND a valid bare version). Falling
    // back to parseBare() then handles the simple "vX.Y.Z[-suffix]"
    // shape and any other unrecognised text is rejected.
    if (VersionInfo wrapped = parseWrapped(trimmed); wrapped.valid) {
        return wrapped;
    }
    return parseBare(trimmed);
}

bool VersionChecker::isSupported(const QString& versionString)
{
    return parse(versionString).isSupported();
}

} // namespace judecoin_wallet::core::version
