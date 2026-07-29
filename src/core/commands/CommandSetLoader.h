// =============================================================================
// Judecoin Wallet - CommandSetLoader (st-009)
//
// Reads `audit/cli_commands/v<MAJOR>.<MINOR>.<PATCH>.json` and exposes the
// command / RPC-method set the rest of the codebase needs to drive the
// daemon and wallet-rpc of that binary version.
//
//   Input  : a version string ("v3.2.0", "3.2.0", or one of the wrapped
//            daemon shapes that VersionChecker accepts) **OR** the sentinel
//            "latest" / empty, meaning "fall back to the highest version we
//            ship on disk".
//   Output : a CommandSet struct: every daemon_rpc group (core, blocks,
//            transactions, ...) and every wallet_rpc_methods group, as
//            QStringList, plus default_ports and binary_files metadata.
//
// Why this exists
// ---------------
// The wallet ships ONE command-set JSON file per daemon X.Y.Z it supports
// (see audit/cli_commands/v3.2.0.json + README.md). The UI startup
// sequence (per v3.2.0.json::ui_implementation_strategy.config_strategy)
// step 5 calls `daemon.get_info`, picks up `version`, then loads
// `audit/cli_commands/v<version>.json` to know which methods to expose.
//
// V5.4.4 decision B keeps support at v3.2.0+. If the
// running daemon reports v3.3.0 and we have not authored v3.3.0.json yet,
// CommandSetLoader falls back to the highest available JSON (v3.2.0 today)
// and emits a non-fatal warning so the UI still functions with a known-good
// command set. We never crash on "newer version we haven't catalogued".
//
// V5.4.2 mainnet-only: the only entries that matter are mainnet defaults
// (19060 / 19061 / 19062 / 19063 / 19071). The loader still surfaces the
// full `default_ports` block from JSON so a future multi-network build
// could honour it without code changes.
//
// V1.0 mainnet-only - this loader does not validate nettype. That belongs
// in DaemonClient (st-004). Here we only project the JSON into C++.
//
// Synchronous only. Loading is one QFile read + one QJsonDocument::fromJson
// + a handful of lookups. Microseconds for the spec'd v3.2.0.json (26 KB).
// Safe to call from the UI thread on startup or after a daemon reconnect.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_COMMANDS_COMMAND_SET_LOADER_H
#define JUDECOIN_WALLET_CORE_COMMANDS_COMMAND_SET_LOADER_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace judecoin_wallet::core::commands {

// -----------------------------------------------------------------------------
// CommandSet
//
// The projection of one audit/cli_commands/v<X.Y.Z>.json into plain C++.
// Kept as a value type so callers can copy / share it freely across threads;
// all members are immutable after CommandSetLoader::load() returns.
//
// Field reference:
//   version         - canonical "vMAJOR.MINOR.PATCH" pulled from the loaded
//                     JSON's own `version` field. May differ from the
//                     requested input if fallback kicked in.
//   binary_codename - "Chamaeleo dilepis" etc. Empty if unknown.
//   binary_version_string
//                   - the full string the binary printed
//                     (e.g. "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)").
//                     Empty if the JSON didn't carry one.
//   verified_date   - "YYYY-MM-DD" provenance stamp from the JSON. Empty
//                     for hand-rolled test fixtures.
//   daemon_rpc      - category -> [method, ...]. Matches the JSON's
//                     `daemon_rpc` object verbatim. Categories present in
//                     v3.2.0: core / blocks / transactions / network /
//                     mining / hardfork / loki_service_nodes / stats /
//                     system.
//   wallet_rpc_methods
//                   - same shape under the JSON's `wallet_rpc_methods`
//                     object. Categories present in v3.2.0: core /
//                     transactions / tx_proofs / balance_proofs / sign /
//                     uri / address_book / account_tags / multisig /
//                     wallet_ops / description / attribute / log / freeze /
//                     loki_service_nodes / other.
//   default_ports   - network -> { p2p, rpc, ... } from the JSON's
//                     `default_ports` object. V5.4.2 mainnet-only has just
//                     `mainnet`.
//   binary_files    - binary_name -> { path, size_bytes, build_date, note? }
//                     from the JSON's `binary_files` object. Used by the
//                     Settings page (st-014) to display "what binary are
//                     we actually running".
//   fallback_used   - true if the requested version was NOT on disk and we
//                     loaded a different (highest available) version
//                     instead. UI surfaces a soft warning when this is true.
//   loadedFromPath  - absolute filesystem path of the JSON we actually
//                     read. Useful for diagnostics / Settings page.
//   raw             - the full QJsonObject. Future stages (UI feature
//                     toggles driven by `ui_implementation_strategy`,
//                     V1.1 SN-panel bits, etc.) consume subtrees we don't
//                     yet project here; raw keeps the door open without a
//                     parallel file-re-read.
// -----------------------------------------------------------------------------
struct CommandSet
{
    QString version;
    QString binary_codename;
    QString binary_version_string;
    QString verified_date;

    QHash<QString, QStringList> daemon_rpc;
    QHash<QString, QStringList> wallet_rpc_methods;

    // network ("mainnet"/"testnet"/...) -> { port_key -> port_number_as_qstring }
    QHash<QString, QHash<QString, QString>> default_ports;

    // binary_name ("judecoind"/"judecoin-wallet-rpc"/...) ->
    //   { "path" -> "...", "size_bytes" -> "22781928",
    //     "build_date" -> "2026-05-22", "note" -> "..." (optional) }
    QHash<QString, QHash<QString, QString>> binary_files;

    bool    fallback_used = false;
    QString loadedFromPath;
    QJsonObject raw;

    // ---- Convenience accessors (no allocation unless noted) ----

    // Flat list of every daemon_rpc method, with duplicates removed and
    // original (insertion) order preserved. Allocates only on first call
    // per CommandSet instance.
    QStringList allDaemonMethods() const;

    // Flat list of every wallet_rpc method, deduped, ordered.
    QStringList allWalletMethods() const;

    // True iff `method` appears in wallet_rpc_methods under ANY category.
    // Linear in the number of categories (16 in v3.2.0); fine for V1.0.
    bool hasWalletMethod(const QString& method) const;

    // True iff `method` appears in daemon_rpc under ANY category.
    bool hasDaemonMethod(const QString& method) const;

    // The "V1.0 enabled methods" list, intersected against the JSON's
    // wallet_rpc_methods. If `v1_0_methods` is empty, returns the full
    // allWalletMethods() list (treats "no policy" as "everything enabled").
    // `v1_0_methods` is the v1_0_scope.wallet_rpc_methods_used array from
    // the JSON's ui_implementation_strategy (50 entries in v3.2.0).
    QStringList enabledWalletMethods(const QStringList& v1_0_methods) const;

    // Same idea for daemon (V1.0 only uses get_info, but the helper is
    // symmetric for future stages).
    QStringList enabledDaemonMethods(const QStringList& v1_0_methods) const;
};

// -----------------------------------------------------------------------------
// LoadResult
//
// Status of a CommandSetLoader::load() call. The loader is exception-free
// - all failures are reported through this struct so callers can decide
// whether to abort startup, fall back to a sane default, or hard-fail.
//
//   ok              - true iff the call produced a usable CommandSet.
//   fallback_used   - propagated from CommandSet::fallback_used; lets the
//                     caller surface a UI warning without re-walking the
//                     returned struct.
//   requested       - the version the caller asked for, canonicalised
//                     ("v3.2.0"). For "latest" / empty this is "".
//   loaded          - the version actually loaded ("v3.2.0"). Equal to
//                     requested on success-without-fallback.
//   error_message   - human-readable description of the failure. Empty on
//                     success.
//   error_kind      - machine-readable kind:
//                       "ok"            - load succeeded
//                       "dir_missing"   - configured dir does not exist
//                       "no_versions"   - dir exists but no v*.json files
//                       "not_found"     - dir has files but none match
//                                          the requested version
//                       "parse_error"   - file exists but JSON is corrupt
//                       "io_error"      - other I/O failure (perm denied,
//                                          etc.)
//                     Always one of the strings above; callers can switch
//                     on it without substring matching.
// -----------------------------------------------------------------------------
struct LoadResult
{
    bool        ok              = false;
    bool        fallback_used   = false;
    QString     requested;
    QString     loaded;
    QString     error_message;
    QString     error_kind = QStringLiteral("ok");
    CommandSet  set;             // valid only when ok == true
};

// -----------------------------------------------------------------------------
// CommandSetLoader
//
// Static utility. No instance state needed; the only piece of
// configuration is the directory that holds the v*.json files, which is
// resolved at call time (see resolveDefaultDirectory()).
//
// V1.0 mainnet-only: there is no per-network or per-testnet variant of
// this loader. Network selection lives in DaemonClient (st-004).
// -----------------------------------------------------------------------------
class CommandSetLoader
{
public:
    // Filename pattern (V5.4.4 B): one file per daemon X.Y.Z, lowercase
    // 'v' prefix, literal dots. We accept both "v3.2.0" and "v3.2.0.json"
    // when matching, and we always normalise to the latter for I/O.
    static constexpr const char* kFilePatternPrefix = "v";
    static constexpr const char* kFilePatternSuffix = ".json";

    // Sentinel for `requested` that means "no specific version requested,
    // pick the highest one on disk". Equivalent to passing "".
    static const char* const kSentinelLatest;

    // -------------------------------------------------------------------------
    // Discovery
    // -------------------------------------------------------------------------

    // Scan `directory` for files matching `v*.json` and return them as a
    // sorted-by-semver-descending list of canonical "vX.Y.Z" strings.
    // Files whose name doesn't parse as vMAJOR.MINOR.PATCH are skipped
    // silently (they're assumed to be hand-written notes like
    // "v3.2.0.draft.json"). Returns an empty list if the directory does
    // not exist.
    //
    // This is the canonical "what versions do we ship?" query; the
    // fallback path of load() calls it internally.
    static QStringList availableVersions(const QString& directory);

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------

    // Pure lookup helper (st-115). Given a list of `available` version
    // strings (already canonicalised "vX.Y.Z", sorted descending) and
    // a `requested` version string, return the BEST available match:
    //
    //   1. exact match      -> e.g. requested "v3.2.1", have "v3.2.1"
    //   2. patchless match  -> e.g. requested "v3.2.1", have "v3.2"  (use major.minor)
    //   3. highest fallback -> return `available.first()` (sorted desc)
    //
    // Returns an empty string if `available` is empty. Otherwise the
    // returned string is always an element of `available`. The function
    // does NOT itself consult the filesystem; callers (load(), unit
    // tests) feed in their own `available` list.
    //
    // Used by load() to pick the target file. Exposed publicly so tests
    // can verify the priority chain without touching disk.
    static QString bestMatchForVersion(const QStringList& available,
                                       const QString& requested);

    // Resolve `requested` against `directory` and return a fully populated
    // CommandSet. Behaviour (st-115 updated):
    //
    //   requested == ""  | kSentinelLatest
    //     -> scan directory, pick the highest semver, load it.
    //        Returns error_kind == "no_versions" if the dir has none.
    //
    //   requested == "v3.2.1" / "3.2.1"
    //     -> look for `v3.2.1.json` exactly. If found, load it.
    //        If NOT found, try patchless fallback `v3.2.json` (highest
    //        shared major.minor) - this is the "newer patch on a known
    //        line" path. If THAT also missing, pick the HIGHEST available
    //        version, set fallback_used == true. Never a hard error.
    //
    //   requested == "garbage"
    //     -> not parsable as vMAJOR.MINOR.PATCH. Falls back to latest
    //        available, sets fallback_used == true. Surfaces a warning
    //        in error_message but ok is still true (UI keeps working).
    //
    // File errors (missing, corrupt, unreadable) populate error_kind and
    // error_message, set ok == false. They NEVER set fallback_used.
    static LoadResult load(const QString& directory, const QString& requested);

    // Convenience: load with the default directory resolution (see
    // resolveDefaultDirectory). The single most common entry point.
    static LoadResult load(const QString& requested);

    // -------------------------------------------------------------------------
    // Directory resolution
    // -------------------------------------------------------------------------

    // Where the loader looks for v*.json by default. Resolution order:
    //
    //   1. Env override: JUDECOIN_CLI_COMMANDS_DIR (set by tests + by
    //      AppImage's run script; lets the packaged binary point at its
    //      own bundled JSON without baking an absolute path into the
    //      binary).
    //   2. Sibling-of-binary: <dir-of-judecoin-wallet-ui>/../share/
    //      judecoin-wallet/audit/cli_commands (AppImage layout).
    //   3. Source-tree default (development): $PROJECT_ROOT/audit/
    //      cli_commands, where PROJECT_ROOT is the parent of the source
    //      dir at compile time. CMake bakes this in as
    //      JUDECOIN_WALLET_AUDIT_DIR; the header holds a fallback for
    //      consumers that don't define it.
    //
    // The first directory that contains at least one v*.json file wins.
    // If none do, step 3's path is returned even if empty (so error
    // messages point somewhere sensible).
    static QString resolveDefaultDirectory();

    // Raw compile-time default. Falls back to a relative guess when
    // JUDECOIN_WALLET_AUDIT_DIR wasn't baked in at build time.
    static const char* const kCompileTimeAuditDir;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    // Convert any of "v3.2.0", "3.2.0", "v3.2.0-release" to the canonical
    // "v3.2.0" filename stem. Returns empty string if the input can't be
    // parsed as vMAJOR.MINOR.PATCH (no leading-v required; the suffix
    // is stripped if present).
    static QString normalizeVersionString(const QString& input);

    // True iff `input` parses as vMAJOR.MINOR.PATCH (suffix optional).
    // Accepts both "v3.2.0" and "3.2.0" with or without a "-suffix" tail.
    // Used internally; exposed so callers can pre-validate without
    // catching LoadResult.error_kind.
    static bool isValidVersionString(const QString& input);
};

} // namespace judecoin_wallet::core::commands

#endif // JUDECOIN_WALLET_CORE_COMMANDS_COMMAND_SET_LOADER_H
