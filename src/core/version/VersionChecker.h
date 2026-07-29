// =============================================================================
// Judecoin Wallet - VersionChecker (st-008)
//
// Parses the daemon `get_info.version_string` field and decides whether the
// running daemon is one the wallet UI knows how to drive.
//
//   Input  : a free-form version string reported by judecoind, e.g.
//            "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)" (full version
//            string) or just the bracketed core "v3.2.0-release" / "v3.2.0".
//   Output : a VersionInfo struct with major/minor/patch/codename, plus a
//            boolean isSupported() that the rest of the codebase queries.
//
// Why this exists
// ---------------
// The wallet ships one command-set JSON file per daemon major.minor that it
// supports (see CommandSetLoader, st-009). That filename is
//   audit/cli_commands/v<MAJOR>.<MINOR>.<PATCH>.json
// so the parser has to recover a clean "vX.Y.Z" identifier. The daemons
// tend to be liberal with surrounding text (release tag, codename in single
// quotes, parentheses) so we do the extraction once, here, and let the rest
// of the codebase consume a tidy struct.
//
// V5.4.4 decision B (st-008):
//   Supported : daemon version >= v3.2.0  (major == 3 && minor >= 2)
//   Rejected  : v2.x, v3.0.x, v3.1.x, v4.x, anything malformed
//
// V5.4.2 mainnet-only: this module does not validate nettype. That belongs
// in DaemonClient (st-004); here we only look at the numeric version.
//
// Synchronous only. Parsing is a handful of integer conversions plus one
// regex match - microseconds. Safe to call from the UI thread on the
// startup path or as part of any health-poll response handler.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_VERSION_VERSION_CHECKER_H
#define JUDECOIN_WALLET_CORE_VERSION_VERSION_CHECKER_H

#include <QString>

namespace judecoin_wallet::core::version {

// -----------------------------------------------------------------------------
// Support-rule constants (V5.4.4 decision B).
//
// Exposed at namespace scope so both VersionInfo::isSupported() and
// VersionChecker can reference them without dragging each other in.
// -----------------------------------------------------------------------------
inline constexpr int kSupportedMajorVersion = 3;   // only major == 3 is OK
inline constexpr int kSupportedMinMinorVersion = 2; // ...and minor >= 2

// -----------------------------------------------------------------------------
// VersionInfo - the parsed form of a daemon version string.
//
// Field reference:
//   major / minor / patch - the three numeric components of the daemon
//                            version. The daemon reports them as integers;
//                            we never store them as a single packed
//                            number so we can compare them with simple
//                            relational operators.
//   suffix              - anything after the dash, e.g. "release" from
//                            "v3.2.0-release". Empty if the daemon didn't
//                            tag the build. Not used for support
//                            decisions; surfaced for diagnostics / UI.
//   codename            - human-friendly project codename, looked up
//                            from the version in a hard-coded table
//                            (e.g. "Chamaeleo dilepis" for v3.2.0).
//                            Empty when no codename is known.
//   valid               - true if parse() succeeded. Useful when callers
//                            want to distinguish "v0.0.0" (an impossible
//                            but validly parsed version, e.g. for
//                            defensive testing) from a totally bogus
//                            input that we couldn't parse at all.
//
// Two support rules live as member functions so callers don't need to
// know the kSupported* constants by heart:
//   isSupported() - V5.4.4 decision B. v3.2.0+ only.
//   toVersionString() - canonical "vMAJOR.MINOR.PATCH", used as the
//                       command-set filename stem by CommandSetLoader
//                       (st-009).
// -----------------------------------------------------------------------------
struct VersionInfo
{
    int     major    = 0;
    int     minor    = 0;
    int     patch    = 0;
    QString suffix;     // e.g. "release"
    QString codename;   // e.g. "Chamaeleo dilepis"
    bool    valid    = false;

    // True iff the parser recognised the input as a version triple.
    bool isValid() const noexcept { return valid; }

    // V5.4.4 B amendment: v3.2.0+ only.
    //   v2.x      -> not supported (too old)
    //   v3.0.x    -> not supported (pre-codename-change release line)
    //   v3.1.x    -> not supported (last "Chamaeleo" naming window pre-baseline)
    //   v3.2.x    -> supported
    //   v3.3.x    -> supported
    //   v4.x      -> not supported (forward-incompatible protocol)
    bool isSupported() const noexcept
    {
        return valid
            && major == kSupportedMajorVersion
            && minor >= kSupportedMinMinorVersion;
    }

    // Canonical "vMAJOR.MINOR.PATCH". Used by CommandSetLoader (st-009)
    // to assemble `audit/cli_commands/vX.Y.Z.json`.
    QString toVersionString() const
    {
        return QStringLiteral("v%1.%2.%3")
            .arg(major).arg(minor).arg(patch);
    }
};

} // namespace judecoin_wallet::core::version

// Q_DECLARE_METATYPE intentionally omitted: VersionInfo is consumed internally
// by VersionChecker only and never crosses a signal/slot boundary. If a
// future consumer (e.g. st-009 CommandSetLoader) needs to ship it through
// queued connections, add Q_DECLARE_METATYPE + <QMetaType> here then.

namespace judecoin_wallet::core::version {

// -----------------------------------------------------------------------------
// VersionChecker
//
// Static-only utility. No instance state; the parse() / isSupported()
// entry points can be called from any thread without setup.
// -----------------------------------------------------------------------------
class VersionChecker
{
public:
    // V5.4.4 B amendment. Documented as
    // static-constexpr for clarity in the public API.
    static constexpr int kMinMajor = kSupportedMajorVersion;
    static constexpr int kMinMinor = kSupportedMinMinorVersion;

    // Parse a daemon version string.
    //
    // Accepts two input shapes:
    //   1. bare / tagged version - "vX.Y.Z" or "vX.Y.Z-suffix"
    //      (e.g. "v3.2.0", "v3.2.0-release", "v3.2.0-master")
    //   2. wrapped full version_string -
    //      "NAME 'CODENAME' (vX.Y.Z[-suffix])"
    //      (e.g. the v3.2.0 binary emits
    //       "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)")
    //
    // Leading / trailing whitespace is trimmed.
    //
    // Returns a VersionInfo with valid == false when the input doesn't
    // match either shape - isSupported() then returns false, so callers
    // don't have to special-case parse failures.
    //
    // Out-of-range numeric components (negative numbers, numbers that
    // overflow int) also yield valid == false; we never throw.
    static VersionInfo parse(const QString& versionString);

    // Convenience: parse() + isSupported().
    // Returns false for both "unsupported" and "couldn't parse".
    static bool isSupported(const QString& versionString);
};

} // namespace judecoin_wallet::core::version

#endif // JUDECOIN_WALLET_CORE_VERSION_VERSION_CHECKER_H
