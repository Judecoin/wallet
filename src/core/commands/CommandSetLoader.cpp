// =============================================================================
// Judecoin Wallet - CommandSetLoader implementation (st-009)
//
// See CommandSetLoader.h for the design notes. This file is the
// straight-line QFile + QJsonDocument implementation; the algorithm is
// small enough that it doesn't need a separate Strategy class.
//
// Layout
// ------
//   1. CommandSet accessors
//   2. Version-string normalisation
//   3. Directory resolution
//   4. availableVersions / file scanning
//   5. load() + fallback chain
//   6. JSON -> CommandSet projection
// =============================================================================

#include "core/commands/CommandSetLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLatin1String>
#include <QRegularExpression>
#include <QStringBuilder>

#include <algorithm>
#include <utility>

#ifndef JUDECOIN_WALLET_AUDIT_DIR_FALLBACK
// No CMake-defined audit dir -> guess a relative path that matches the
// source-tree layout. The loader falls back to the first directory in
// resolveDefaultDirectory() that actually contains v*.json files, so a
// wrong guess here is harmless (just one wasted QDir::exists() check).
#define JUDECOIN_WALLET_AUDIT_DIR_FALLBACK "audit/cli_commands"
#endif

namespace judecoin_wallet::core::commands {

// -----------------------------------------------------------------------------
// Static constants
// -----------------------------------------------------------------------------

const char* const CommandSetLoader::kSentinelLatest = "__latest__";

// Source-tree default. CMakeLists.txt is expected to define
// JUDECOIN_WALLET_AUDIT_DIR at configure time and pass it via
// target_compile_definitions(); if it didn't (e.g. a downstream consumer
// pulled in our headers without rebuilding the static lib), we use a
// reasonable sibling-relative guess.
#ifdef JUDECOIN_WALLET_AUDIT_DIR
const char* const CommandSetLoader::kCompileTimeAuditDir = JUDECOIN_WALLET_AUDIT_DIR;
#else
const char* const CommandSetLoader::kCompileTimeAuditDir =
    JUDECOIN_WALLET_AUDIT_DIR_FALLBACK;
#endif

// =============================================================================
// 1. CommandSet accessors
// =============================================================================

QStringList CommandSet::allDaemonMethods() const
{
    QStringList out;
    out.reserve(daemon_rpc.size() * 4);  // heuristic; categories avg 4-5 entries
    for (auto it = daemon_rpc.cbegin(); it != daemon_rpc.cend(); ++it) {
        for (const QString& m : it.value()) {
            if (!out.contains(m)) {
                out.append(m);
            }
        }
    }
    return out;
}

QStringList CommandSet::allWalletMethods() const
{
    QStringList out;
    out.reserve(wallet_rpc_methods.size() * 4);
    for (auto it = wallet_rpc_methods.cbegin(); it != wallet_rpc_methods.cend(); ++it) {
        for (const QString& m : it.value()) {
            if (!out.contains(m)) {
                out.append(m);
            }
        }
    }
    return out;
}

bool CommandSet::hasWalletMethod(const QString& method) const
{
    for (auto it = wallet_rpc_methods.cbegin(); it != wallet_rpc_methods.cend(); ++it) {
        if (it.value().contains(method)) {
            return true;
        }
    }
    return false;
}

bool CommandSet::hasDaemonMethod(const QString& method) const
{
    for (auto it = daemon_rpc.cbegin(); it != daemon_rpc.cend(); ++it) {
        if (it.value().contains(method)) {
            return true;
        }
    }
    return false;
}

QStringList CommandSet::enabledWalletMethods(const QStringList& v1_0_methods) const
{
    if (v1_0_methods.isEmpty()) {
        return allWalletMethods();
    }
    // Walk the JSON's wallet_rpc_methods, in category order, and keep only
    // methods also present in v1_0_methods. Preserves JSON category order
    // so the UI's "method index" feels stable across runs.
    QStringList out;
    const QStringList available = allWalletMethods();
    for (const QString& m : available) {
        if (v1_0_methods.contains(m) && !out.contains(m)) {
            out.append(m);
        }
    }
    return out;
}

QStringList CommandSet::enabledDaemonMethods(const QStringList& v1_0_methods) const
{
    if (v1_0_methods.isEmpty()) {
        return allDaemonMethods();
    }
    QStringList out;
    const QStringList available = allDaemonMethods();
    for (const QString& m : available) {
        if (v1_0_methods.contains(m) && !out.contains(m)) {
            out.append(m);
        }
    }
    return out;
}

// =============================================================================
// 2. Version-string normalisation
// =============================================================================

// The wire formats we accept for "version":
//
//   "v3.2.0"            -> "v3.2.0"
//   "3.2.0"             -> "v3.2.0"
//   "v3.2.0-release"    -> "v3.2.0"  (suffix stripped)
//   "Judecoin '...' (v3.2.0-release)" -> "v3.2.0"
//   "garbage"           -> "" (empty)
//
// We deliberately do NOT reuse VersionChecker (st-008): this loader is
// called from the UI startup path BEFORE VersionChecker has classified
// the daemon. The format we need is also looser (we'll happily fall back
// on "v3.2.0" if the daemon reported "v3.2.0-something-weird"), so a
// dedicated tiny parser keeps things simple.
QString CommandSetLoader::normalizeVersionString(const QString& input)
{
    if (input.isEmpty() || input == QLatin1String(kSentinelLatest)) {
        return QString();
    }

    // Find the first run of [digit.digit.digit] - optional leading 'v',
    // optional -suffix. Wrapped full version_string
    // ("Judecoin 'Chamaeleo dilepis' (v3.2.0-release)") reaches the same
    // branch because the regex anchors on the digits, not on the wrapper.
    static const QRegularExpression kTriple(
        QStringLiteral(R"((?:[vV])?(\d+)\.(\d+)\.(\d+))"));
    const QRegularExpressionMatch m = kTriple.match(input);
    if (!m.hasMatch()) {
        return QString();
    }

    bool okMajor = false, okMinor = false, okPatch = false;
    const int major = m.captured(1).toInt(&okMajor);
    const int minor = m.captured(2).toInt(&okMinor);
    const int patch = m.captured(3).toInt(&okPatch);
    if (!okMajor || !okMinor || !okPatch) {
        return QString();
    }
    if (major < 0 || major > 999 || minor < 0 || minor > 999 ||
        patch < 0 || patch > 999) {
        return QString();
    }
    return QStringLiteral("v%1.%2.%3").arg(major).arg(minor).arg(patch);
}

bool CommandSetLoader::isValidVersionString(const QString& input)
{
    return !normalizeVersionString(input).isEmpty();
}

// =============================================================================
// 3. Directory resolution
// =============================================================================

namespace {

// Append a candidate directory to `out`, unless it's empty / duplicates
// an entry already in `out` (after QDir::cleanPath normalisation).
void appendCandidate(QStringList& out, const QString& candidate)
{
    if (candidate.isEmpty()) {
        return;
    }
    const QString cleaned = QDir::cleanPath(candidate);
    if (!out.contains(cleaned)) {
        out.append(cleaned);
    }
}

} // namespace

QString CommandSetLoader::resolveDefaultDirectory()
{
    // Build the candidate list, then return the first one that has at
    // least one v*.json file. The list is:
    //
    //   1. $JUDECOIN_CLI_COMMANDS_DIR (env override - set by AppImage
    //      AppRun + by tests)
    //   2. <appdir>/usr/share/judecoin-wallet/audit/cli_commands
    //      (AppImage layout; QCoreApplication::applicationDirPath()
    //       points at <appdir>/usr/bin in that case)
    //   3. <appdir>/../share/judecoin-wallet/audit/cli_commands
    //      (tarball / system install layout where data sits one level
    //       up from the binary)
    //   4. <src>/audit/cli_commands (compile-time default baked in by
    //      CMakeLists.txt; the last-resort guess is the same path
    //      relative to CWD if the macro wasn't defined).
    QStringList candidates;

    const QByteArray envDir = qgetenv("JUDECOIN_CLI_COMMANDS_DIR");
    if (!envDir.isEmpty()) {
        appendCandidate(candidates, QString::fromLocal8Bit(envDir));
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    appendCandidate(candidates,
                    appDir + QStringLiteral("/../share/judecoin-wallet/audit/cli_commands"));
    appendCandidate(candidates,
                    appDir + QStringLiteral("/usr/share/judecoin-wallet/audit/cli_commands"));

    // Compile-time / source-tree default. CMake bakes this in as an
    // absolute path; if it didn't, we use the fallback relative path.
    appendCandidate(candidates, QLatin1String(kCompileTimeAuditDir));

    for (const QString& dir : candidates) {
        if (!QDir(dir).exists()) {
            continue;
        }
        // Has at least one v*.json? If so, declare it the winner.
        const QStringList entries =
            QDir(dir).entryList(QStringList() << QStringLiteral("v*.json"),
                                QDir::Files, QDir::Name);
        if (!entries.isEmpty()) {
            return QDir::cleanPath(dir);
        }
    }

    // Nothing matched. Return the first candidate (typically the env or
    // the compile-time default) so error messages point at something
    // sensible. Caller checks QDir::exists() before reading.
    return candidates.isEmpty() ? QLatin1String(kCompileTimeAuditDir)
                                : candidates.first();
}

// =============================================================================
// 4. availableVersions / file scanning
// =============================================================================

namespace {

// Compare two "vMAJOR.MINOR.PATCH" version strings. Returns:
//   < 0 if a <  b
//   0   if a == b
//   > 0 if a >  b
//
// We do NOT reuse VersionChecker here because that's a string parser
// (not a comparator) and we want strict numerical ordering, not
// support-rule ordering. The inputs are guaranteed to already match
// the vX.Y.Z shape by the time this is called.
int compareVersions(const QString& a, const QString& b)
{
    // Both are guaranteed "vX.Y.Z" or the empty string. Empty sorts last.
    if (a.isEmpty() && b.isEmpty()) return 0;
    if (a.isEmpty()) return  1;
    if (b.isEmpty()) return -1;

    auto parts = [](const QString& v) -> std::array<int, 3> {
        const QStringList segs = v.mid(1).split(QLatin1Char('.'));
        std::array<int, 3> out = {0, 0, 0};
        for (int i = 0; i < 3 && i < segs.size(); ++i) {
            bool ok = false;
            const int n = segs.at(i).toInt(&ok);
            if (ok) {
                out.at(i) = n;
            }
        }
        return out;
    };
    const auto pa = parts(a);
    const auto pb = parts(b);
    for (int i = 0; i < 3; ++i) {
        if (pa.at(i) != pb.at(i)) {
            return pa.at(i) - pb.at(i);
        }
    }
    return 0;
}

} // namespace

QString CommandSetLoader::bestMatchForVersion(const QStringList& available,
                                            const QString& requested)
{
    if (available.isEmpty()) {
        return QString();
    }
    // requested == ""  or  sentinel "latest"  -> just take the highest.
    if (requested.isEmpty() || requested == QLatin1String(kSentinelLatest)) {
        return available.first();  // sorted descending
    }

    // Normalise the request the same way load() does. If the caller
    // passes something we can't parse, treat it like a miss and let
    // the highest-fallback path take over. We deliberately do NOT
    // return an empty string for unparseable input: the public load()
    // contract says "fall back and warn", and that's what this helper
    // helps with.
    const QString canonical = normalizeVersionString(requested);
    if (canonical.isEmpty()) {
        return available.first();
    }

    // 1. Exact match.
    if (available.contains(canonical)) {
        return canonical;
    }

    // 2. Patchless (major.minor) fallback. v3.2.1 -> "v3.2".
    // We do this even if the patchless prefix sorts BELOW the highest
    // entry: the user explicitly asked for v3.2.x and a v3.2.json
    // exists, that's a stronger signal than "highest available" (which
    // might be v4.0.0 - clearly a different line).
    const QString majorMinor =
        canonical.left(canonical.lastIndexOf(QLatin1Char('.')));
    if (!majorMinor.isEmpty() && available.contains(majorMinor)) {
        return majorMinor;
    }

    // 3. Highest available.
    return available.first();
}

QStringList CommandSetLoader::availableVersions(const QString& directory)
{
    QStringList out;
    const QDir dir(directory);
    if (!dir.exists()) {
        return out;
    }
    const QStringList entries =
        dir.entryList(QStringList() << QStringLiteral("v*.json"),
                     QDir::Files, QDir::Name);
    for (const QString& e : entries) {
    }
    for (const QString& name : entries) {
        QString stem = name;
        if (stem.endsWith(QLatin1String(kFilePatternSuffix),
                          Qt::CaseInsensitive)) {
            stem.chop(int(strlen(kFilePatternSuffix)));
        }
        if (isValidVersionString(stem)) {
            const QString canon = normalizeVersionString(stem);
            if (!canon.isEmpty() && !out.contains(canon)) {
                out.append(canon);
            }
        }
    }
    // Sort descending so callers can grab the first entry as "latest".
    std::sort(out.begin(), out.end(),
              [](const QString& a, const QString& b) {
                  return compareVersions(a, b) > 0;
              });
    return out;
}

// =============================================================================
// 5. load() + fallback chain
// =============================================================================

namespace {

// Read a JSON file from disk. Populates `doc` on success; returns an
// empty QString on success and a non-empty error message on failure.
QString readJsonFile(const QString& path, QJsonDocument* doc)
{
    QFile f(path);
    if (!f.exists()) {
        return QStringLiteral("file does not exist: %1").arg(path);
    }
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringLiteral("cannot open %1: %2")
            .arg(path, f.errorString());
    }
    const QByteArray bytes = f.readAll();
    f.close();
    QJsonParseError perr{};
    *doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError) {
        return QStringLiteral("JSON parse error in %1 at offset %2: %3")
            .arg(path)
            .arg(perr.offset)
            .arg(perr.errorString());
    }
    if (!doc->isObject()) {
        return QStringLiteral("top-level JSON in %1 is not an object")
            .arg(path);
    }
    return QString();
}

// Project one QJsonObject (the audit file) into a CommandSet. We don't
// do schema validation beyond "is it a JSON object" - if a field is
// missing it just stays empty / default, which matches the spec's
// "error popup, process exit" policy only at the OUTER load() level (corrupt file
// vs. unknown-but-present file).
CommandSet projectJson(const QJsonObject& root, const QString& loadedFromPath)
{
    CommandSet cs;
    cs.loadedFromPath = loadedFromPath;
    cs.raw = root;

    cs.version              = root.value(QStringLiteral("version")).toString();
    cs.binary_codename      = root.value(QStringLiteral("binary_codename")).toString();
    cs.binary_version_string= root.value(QStringLiteral("binary_version_string")).toString();
    cs.verified_date        = root.value(QStringLiteral("verified_date")).toString();

    // daemon_rpc: { category: [method, ...], ... }
    const QJsonObject daemonObj =
        root.value(QStringLiteral("daemon_rpc")).toObject();
    for (auto it = daemonObj.begin(); it != daemonObj.end(); ++it) {
        if (!it.value().isArray()) {
            continue;
        }
        QStringList methods;
        const QJsonArray arr = it.value().toArray();
        methods.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            if (v.isString()) {
                methods.append(v.toString());
            }
        }
        cs.daemon_rpc.insert(it.key(), methods);
    }

    // wallet_rpc_methods: same shape as daemon_rpc.
    const QJsonObject walletObj =
        root.value(QStringLiteral("wallet_rpc_methods")).toObject();
    for (auto it = walletObj.begin(); it != walletObj.end(); ++it) {
        if (!it.value().isArray()) {
            continue;
        }
        QStringList methods;
        const QJsonArray arr = it.value().toArray();
        methods.reserve(arr.size());
        for (const QJsonValue& v : arr) {
            if (v.isString()) {
                methods.append(v.toString());
            }
        }
        cs.wallet_rpc_methods.insert(it.key(), methods);
    }

    // default_ports: { network: { port_key: port_number, ... }, ... }
    const QJsonObject portsObj =
        root.value(QStringLiteral("default_ports")).toObject();
    for (auto netIt = portsObj.begin(); netIt != portsObj.end(); ++netIt) {
        if (!netIt.value().isObject()) {
            continue;
        }
        QHash<QString, QString> ports;
        const QJsonObject portsForNet = netIt.value().toObject();
        for (auto pIt = portsForNet.begin(); pIt != portsForNet.end(); ++pIt) {
            // Port values in the v3.2.0 JSON are integers; we keep them
            // as QString for the QML/QSettings-friendly interface but
            // accept strings too for forward-compat (some future
            // JSON could store "19060-19070" ranges as strings).
            const QJsonValue v = pIt.value();
            if (v.isDouble()) {
                ports.insert(pIt.key(),
                             QString::number(v.toInt()));
            } else if (v.isString()) {
                ports.insert(pIt.key(), v.toString());
            }
        }
        cs.default_ports.insert(netIt.key(), ports);
    }

    // binary_files: { name: { path, size_bytes, build_date, note? } }
    const QJsonObject filesObj =
        root.value(QStringLiteral("binary_files")).toObject();
    for (auto fIt = filesObj.begin(); fIt != filesObj.end(); ++fIt) {
        if (!fIt.value().isObject()) {
            continue;
        }
        QHash<QString, QString> entry;
        const QJsonObject fields = fIt.value().toObject();
        for (auto kIt = fields.begin(); kIt != fields.end(); ++kIt) {
            // The v3.2.0 JSON uses strings for everything except
            // size_bytes (an integer). Normalise everything to string
            // for the same QML-friendly surface.
            const QJsonValue v = kIt.value();
            if (v.isString()) {
                entry.insert(kIt.key(), v.toString());
            } else if (v.isDouble()) {
                entry.insert(kIt.key(), QString::number(v.toInt()));
            } else if (!v.isNull() && !v.isUndefined()) {
                // best-effort: stringify other JSON types
                entry.insert(kIt.key(), v.toVariant().toString());
            }
        }
        cs.binary_files.insert(fIt.key(), entry);
    }

    return cs;
}

} // namespace

LoadResult CommandSetLoader::load(const QString& directory, const QString& requested)
{
    LoadResult result;
    result.requested = normalizeVersionString(requested);

    if (!QDir(directory).exists()) {
        // st-175 (2026-07-19): audit/ is intentionally excluded
        // from production AppImages. Return an empty CommandSet with
        // ok=true so the startup step succeeds; no toast is shown.
        result.ok = true;
        result.loaded = QString();
        return result;
    }

    // Always list available versions up front. We need it for both the
    // fallback path AND for a fast "no_versions" error when the dir is
    // empty (a misconfigured AppImage, or a first-ever run with no
    // bundled JSON yet).
    const QStringList versions = availableVersions(directory);
    if (versions.isEmpty()) {
        result.ok = false;
        result.error_kind   = QStringLiteral("no_versions");
        result.error_message = QStringLiteral(
            "no v*.json command-set files found in %1").arg(directory);
        return result;
    }

    // Pick the file to load. Delegates to bestMatchForVersion() which
    // implements the priority chain:
    //
    //   a) requested == "" / kSentinelLatest  -> highest available
    //   b) requested EXACT match in versions   -> exact v3.2.1.json
    //   c) requested parseable but no exact    -> patchless v3.2.json
    //                                            fallback (if on disk)
    //   d) requested unparseable OR            -> highest available,
    //      no exact, no patchless                 fallback_used = true
    //
    // We compute the chosen target first, then translate the "no exact
    // match" cases into appropriate fallback_used / error_message so
    // the LoadResult still tells the caller what happened. This keeps
    // bestMatchForVersion() pure (no side effects on the result
    // object) and load() focused on user-facing messaging.
    const QString target = bestMatchForVersion(versions, requested);

    if (requested.isEmpty() || requested == QLatin1String(kSentinelLatest)) {
        // (a) explicit "latest" request -> silent (no warning).
    } else if (result.requested.isEmpty()) {
        // (d-ish) Input was unparseable as a version triple but not
        // empty (e.g. "garbage"). bestMatchForVersion() still picked
        // something; warn so the UI can show a banner.
        result.fallback_used = true;
        result.error_message = QStringLiteral(
            "requested version \"%1\" did not parse; using latest %2")
            .arg(requested, target);
    } else if (target == result.requested) {
        // (b) exact match -> no fallback, no warning.
    } else if (target == result.requested.left(result.requested.lastIndexOf(QLatin1Char('.')))) {
        // (c) patchless fallback. e.g. asked for v3.2.1, loaded v3.2.
        result.fallback_used = true;
        result.error_message = QStringLiteral(
            "no command-set JSON for requested %1; using patchless fallback %2")
            .arg(result.requested, target);
    } else {
        // (d) No exact, no patchless -> highest available.
        result.fallback_used = true;
        result.error_message = QStringLiteral(
            "no command-set JSON for requested %1; using latest %2")
            .arg(result.requested, target);
    }

    // Load + project.
    const QString path =
        QDir(directory).filePath(target + QLatin1String(kFilePatternSuffix));
    QJsonDocument doc;
    const QString readErr = readJsonFile(path, &doc);
    if (!readErr.isEmpty()) {
        result.ok = false;
        result.error_kind   = QStringLiteral("parse_error");
        result.error_message = readErr;
        return result;
    }

    result.ok = true;
    result.loaded = target;
    result.set = projectJson(doc.object(), path);
    result.set.fallback_used = result.fallback_used;

    // The JSON's own `version` field is the canonical answer for "what
    // version did we actually load?". Overwrite the struct's copy with
    // whatever the file says, in case the loader matched on a
    // canonicalised stem but the file has a different label (e.g. a
    // future v3.2.0.1.json that still says "version": "v3.2.0").
    if (!result.set.version.isEmpty()) {
        result.loaded = result.set.version;
    }
    return result;
}

LoadResult CommandSetLoader::load(const QString& requested)
{
    return load(resolveDefaultDirectory(), requested);
}

} // namespace judecoin_wallet::core::commands
