// =============================================================================
// Judecoin Wallet - SettingsSchema (st-014)
//
// V5.4.4 / V5.4.9 / V5.4.12 decisions about the 8 categories of user-facing
// settings, frozen as plain C++ constants so the SettingsManager (the only
// code that talks to QSettings), the Settings QML page (st-014 UI hook),
// the StartupManager (st-015 reads daemon-side defaults at boot), and the
// i18n loader (st-016, picks the translation file matching the language
// setting) all agree on the same vocabulary.
//
// What lives here vs. SettingsManager
// -----------------------------------
// - SettingsSchema  : the *vocabulary* (categories, allowed values, defaults,
//                     QSettings key names, allowed-language list). Header
//                     only, no Qt dependencies beyond QString / QStringList.
//                     Safe to include from QML build artefacts and tests.
// - SettingsManager : the *runtime* (QSettings read/write, change
//                     notifications, Reset-to-defaults). .cpp + .h, links
//                     Qt6::Core.
//
// V1.0 mainnet-only: no per-network setting exists here. Daemon host / port
// defaults live in ProcessManager (st-006); the remote-node override is a
// separate stage (st-023) and will be added there, not patched into this
// file.
//
// V1.0 does not expose appearance / advanced in the UI (V5.4.4 A + A),
// but the schema lists them so a future V1.1 settings page can light up
// the keys without touching SettingsManager.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_SETTINGS_SETTINGS_SCHEMA_H
#define JUDECOIN_WALLET_CORE_SETTINGS_SETTINGS_SCHEMA_H

#include <QString>
#include <QStringList>

#include <optional>

namespace judecoin_wallet::core::settings {

// -----------------------------------------------------------------------------
// SchemaVersion
//
// Bumped whenever the on-disk QSettings layout changes in a way that
// invalidates previously-saved values. SettingsManager consults this on
// startup to decide whether to call migrate() (future work) or simply
// fall back to defaults for unknown keys.
//
// V1.0 ships at version 1. st-023 (remote node) will bump to 2 and add
// remote_daemon_host / remote_daemon_port / is_custom_remote / preset_node
// fields. We surface the bump here so the migration path is one `if`
// away, not a code-search exercise.
// -----------------------------------------------------------------------------
constexpr int kSchemaVersion = 2;

// -----------------------------------------------------------------------------
// QSettings organization
//
// QCoreApplication::setOrganizationName / setApplicationName are called by
// main.cpp before any SettingsManager ctor runs. We hard-code the strings
// here too so the unit tests, which construct SettingsManager in isolation,
// get the same storage path the production binary uses:
//     /home/<user>/.config/JudecoinWallet/JudecoinWallet.conf
// (Linux XDG default; macOS ~/Library/Preferences/JudecoinWallet.plist;
// Windows registry HKCU\Software\JudecoinWallet\JudecoinWallet.)
// -----------------------------------------------------------------------------
constexpr const char kOrganizationName[] = "JudecoinWallet";
constexpr const char kApplicationName[] = "JudecoinWallet";

// -----------------------------------------------------------------------------
// Category
//
// The 8 settings categories surfaced by the UI, in display order. The
// numeric value is the canonical position in the Settings page tree
// (Network first because the daemon can't even start without a valid
// host/port; About last so the version label sits at the bottom).
//
// V5.4.4 decisions:
//   language            - A: follow system locale by default; user can pin
//                              to one of the 9 supported languages.
//   auto_lock           - B: 5min default; 5/15/30/60/never 5 stops.
//   transfer_confirm    - C: pop confirmation + ask for password on send.
//   appearance          - A: Qt defaults + follow system theme; V1.0 does
//                              not expose a UI knob for it.
//   advanced            - A: V1.0 does not expose a UI knob for it.
//   reset               - A: single "Reset All Settings" button.
//   about               - A: read-only version label, no clickable action.
// -----------------------------------------------------------------------------
enum class Category {
    Network          = 0,
    Language         = 1,
    AutoLock         = 2,
    TransferConfirm  = 3,
    Appearance       = 4,
    Advanced         = 5,
    Reset            = 6,
    About            = 7,
};

// Human-readable label per category. i18n keys (not localised here).
QString categoryLabel(Category c);

// All 8 categories in display order, for UI tree builders and tests.
QList<Category> allCategories();

// -----------------------------------------------------------------------------
// Network settings (Category::Network)
//
// V1.0 mainnet-only; daemon host/port live in ProcessManager (st-006).
// st-023 will layer remote-node fields on top using a schema bump.
//
// kNetworkDaemonHost / kNetworkDaemonPort are NOT user-editable in V1.0 -
// they're read at startup and exposed here so the Settings page can show
// them as read-only "Daemon: 127.0.0.1:19061" labels. Writing them from
// the UI is forbidden and SettingsManager::setNetworkDaemon() asserts.
// -----------------------------------------------------------------------------
constexpr const char kNetworkDaemonHost[] = "network/daemon_host";
constexpr const char kNetworkDaemonPort[] = "network/daemon_port";

// V5.4.1 default detection values. 19061 is the mainnet daemon RPC port
// the wallet probes first; falls back to 19063 if 19061 is occupied.
constexpr const char kDefaultDaemonHost[] = "127.0.0.1";
constexpr int         kDefaultDaemonPort = 19061;

// -----------------------------------------------------------------------------
// Remote-node settings (Category::Network, st-023)
//
// V1.1 settings page exposes four knobs:
//   remote_daemon_host  - hostname or IP of a remote judecoind the user wants
//                         to talk to instead of running one locally. Empty
//                         string means "no remote node selected".
//   remote_daemon_port  - port of the remote judecoind. Always >= 1024 and
//                         <= 65535. Default 19061 (mainnet RPC).
//   is_custom_remote    - true if the user picked a custom (non-preset) node
//                         and typed host/port by hand. false if they picked
//                         one of the presets from kRemotePresetNodes below.
//   preset_node         - id of the preset the user picked. Empty string if
//                         is_custom_remote is true. Matches one of
//                         kRemotePresetNodes[i].id exactly.
//
// Presets are static (V1.0 has no auto-discovery) - the Settings page shows
// them in a ComboBox, the user picks one or switches to "Custom...".
//
// V5.4.2 mainnet-only: every preset points at the mainnet daemon port
// 19061. The struct still carries a nettype field so future releases can
// branch on testnet/stagenet without an ABI break.
// -----------------------------------------------------------------------------
constexpr const char kRemoteDaemonHostKey[] = "network/remote_daemon_host";
constexpr const char kRemoteDaemonPortKey[] = "network/remote_daemon_port";
constexpr const char kIsCustomRemoteKey[]   = "network/is_custom_remote";
constexpr const char kPresetNodeKey[]       = "network/preset_node";

// st-133c (P0 NEW): where judecoind keeps its blockchain + wallet state.
// V1.0 mainnet-only: the AppImage previously relied on the daemon
// resolving `~/.judecoin/` itself via $HOME; with the AppImage sandbox
// $HOME points inside the mount, so we now pass --data-dir explicitly.
// The default is `QDir::homePath() + "/.judecoin"` (computed at
// read-time because QDir::homePath() is process state, not a compile
// constant). Empty stored value == "use the default".
constexpr const char kDataDirectoryKey[]   = "network/data_directory";

constexpr const char kDefaultRemoteDaemonHost[] = "";
constexpr int         kDefaultRemoteDaemonPort = 19061;
constexpr bool        kDefaultIsCustomRemote   = false;
constexpr const char kDefaultPresetNode[]      = "";

/// One entry in the preset-nodes ComboBox. id is the QSettings value,
/// host/port is the connection target, label is shown to the user (i18n
/// keys, not localised here - the QML page translates via qsTr()).
struct RemotePresetNode {
    QString id;
    QString label;
    QString host;
    int     port;
};

/// All V1.0 remote-node presets, in display order. Three entries keep the
/// ComboBox short: the official Loki/Oxen public node, a community node,
/// and "Localhost" (127.0.0.1) for users who run their own daemon.
/// st-023 ships only these three; future stages can append more without
/// changing the on-disk schema (preset_node is a string id, not an enum).
inline QList<RemotePresetNode> remotePresetNodes()
{
    return QList<RemotePresetNode>{
        {QStringLiteral("official"),  QStringLiteral("Official Judecoin Node"),
            QStringLiteral("node.judecoin.xyz"),  19061},
        {QStringLiteral("community"), QStringLiteral("Community Node (backup)"),
            QStringLiteral("node2.judecoin.xyz"), 19061},
        {QStringLiteral("localhost"), QStringLiteral("Local Daemon (127.0.0.1)"),
            QStringLiteral("127.0.0.1"),          19061},
    };
}

/// Look up a preset by id. Returns std::nullopt if the id is not in
/// remotePresetNodes() (e.g. user typed garbage into the .conf).
///
/// gf-005 (P0): previous signature returned `const RemotePresetNode*` into a
/// temporary `QList<RemotePresetNode>` produced by `remotePresetNodes()`. The
/// pointer was invalidated the moment the function returned (use-after-free
/// if any caller dereferenced it across the temp's lifetime). We now return a
/// value-copy wrapped in std::optional so the lookup result outlives the
/// temp, and so callers see the "may be absent" semantics in the type.
/// std::optional is available because the project pins CMAKE_CXX_STANDARD 17.
inline std::optional<RemotePresetNode> findRemotePreset(const QString& id)
{
    for (const auto& p : remotePresetNodes()) {
        if (p.id == id) {
            // Copy out of the temp QList; std::optional owns the result so
            // it stays valid for the caller.
            return p;
        }
    }
    return std::nullopt;
}

/// Validators the SettingsManager uses before persisting a remote-node
/// write. Centralised here so the Settings page and StartupManager agree
/// on what counts as "valid host" / "valid port".
inline bool isValidRemoteHost(const QString& host)
{
    if (host.isEmpty()) {
        return true; // empty == "no remote node selected", which IS valid.
    }
    // Reject whitespace, control chars, anything that obviously can't
    // resolve as a hostname or IPv4/IPv6 literal. We do not try to be
    // a full RFC-952 validator - QHostAddress would be the deep check,
    // but the Settings page is too slow if it does DNS resolution on
    // every keystroke. This catches the obvious garbage.
    for (QChar c : host) {
        if (c.isSpace() || !c.isPrint()) {
            return false;
        }
    }
    return !host.isEmpty() && host.size() <= 253;
}

inline bool isValidRemotePort(int port)
{
    return port >= 1 && port <= 65535;
}

inline bool isValidPresetId(const QString& id)
{
    if (id.isEmpty()) {
        return true; // empty == "Custom...", stored when is_custom_remote is true.
    }
    return findRemotePreset(id).has_value();
}


// -----------------------------------------------------------------------------
// Language settings (Category::Language)
//
// V5.4.4 decision A: follow system locale by default (empty string = "auto").
// V5.4.9 / V5.4.12: 9 supported languages, zh_CN deliberately omitted
// (translation file not authored for V1.0). st-016 (i18n) will add the
// actual .ts / .qm files keyed by these locale codes.
//
// "auto" is stored as an empty string in QSettings so the QML combo box
// can bind to "currentText === ''" rather than parse a sentinel.
// -----------------------------------------------------------------------------
constexpr const char kLanguageKey[] = "language/locale";

// Canonical list, ordered by native name then alphabetically. This is the
// "what's available in V1.0" list, NOT the "what's translated" list -
// st-016 controls which of these have a real .qm behind them.
inline QStringList supportedLanguages()
{
    return QStringList{
        QStringLiteral("en"),     // English
        QStringLiteral("zh_TW"),  // Chinese (Traditional)
        QStringLiteral("de"),     // German
        QStringLiteral("ja"),     // Japanese
        QStringLiteral("ko"),     // Korean
        QStringLiteral("it"),     // Italian
        QStringLiteral("nl"),     // Dutch
        QStringLiteral("ru"),     // Russian
        QStringLiteral("es"),     // Spanish
    };
}

// Default: empty string = "follow system locale".
inline QString defaultLanguage() { return QString(); }

// True iff `locale` is one of the supportedLanguage() codes. The empty
// string ("auto") also counts as valid - it's the default and a legal
// stored value.
bool isValidLanguage(const QString& locale);

// Human-readable label for a locale code, in English. Used by the
// Settings page's language combo box. Empty string returns "Follow
// system locale".
QString languageDisplayName(const QString& locale);

// -----------------------------------------------------------------------------
// AutoLock settings (Category::AutoLock)
//
// V5.4.4 decision B: 5 stops, default 5 minutes, "Never" means "don't
// auto-lock at all" (manual lock only via the toolbar).
//
// We store the *seconds* value in QSettings, not the enum, so future
// versions can add intermediate stops (e.g. 10 / 20 / 45) without
// invalidating saved data. Storing the enum is fragile across schema
// renumbering.
// -----------------------------------------------------------------------------
constexpr const char kAutoLockKey[] = "auto_lock/seconds";

// Allowed stop list, in seconds. kNever = 0 (sentinel for "never lock").
constexpr int kAutoLockFiveMin   = 300;     // 5 minutes  - default
constexpr int kAutoLockFifteenMin = 900;     // 15 minutes
constexpr int kAutoLockThirtyMin  = 1800;    // 30 minutes
constexpr int kAutoLockOneHour    = 3600;    // 1 hour
constexpr int kAutoLockNever      = 0;       // "Never" (manual only)

inline int defaultAutoLockSeconds() { return kAutoLockFiveMin; }

inline QList<int> allowedAutoLockSeconds()
{
    return QList<int>{
        kAutoLockFiveMin,
        kAutoLockFifteenMin,
        kAutoLockThirtyMin,
        kAutoLockOneHour,
        kAutoLockNever,
    };
}

bool isValidAutoLockSeconds(int seconds);

// Display label for the combo box: "5 minutes" / "Never" / etc.
QString autoLockDisplayName(int seconds);

// -----------------------------------------------------------------------------
// TransferConfirm settings (Category::TransferConfirm)
//
// V5.4.4 decision C: pop a confirmation dialog AND ask for the wallet
// password before every send. The two booleans are independent so the
// UI can fall back to "confirm only" if the user disables the password
// re-prompt (some screen-reader / accessibility workflows need that).
// -----------------------------------------------------------------------------
constexpr const char kTransferConfirmDialogKey[] = "transfer_confirm/show_dialog";
constexpr const char kTransferConfirmPasswordKey[] = "transfer_confirm/require_password";

constexpr bool kDefaultTransferConfirmDialog   = true;
constexpr bool kDefaultTransferConfirmPassword = true;

// -----------------------------------------------------------------------------
// Appearance settings (Category::Appearance)
//
// V5.4.4 decision A: Qt defaults + follow system theme. V1.0 does NOT
// expose this in the UI; the keys exist so the persistence layer
// survives into V1.1 without schema migration, and so a power user
// can edit the .conf file by hand. SettingsManager rejects writes from
// UI bindings (read-only at the API level for V1.0).
// -----------------------------------------------------------------------------
constexpr const char kAppearanceThemeKey[] = "appearance/theme";
constexpr const char kAppearanceStyleKey[] = "appearance/style";

// "system" = follow OS, "light" / "dark" = forced. Default = "system".
inline QString defaultAppearanceTheme() { return QStringLiteral("system"); }
inline QString defaultAppearanceStyle() { return QStringLiteral("system"); }

bool isValidAppearanceTheme(const QString& theme);
bool isValidAppearanceStyle(const QString& style);

// -----------------------------------------------------------------------------
// Advanced settings (Category::Advanced)
//
// V5.4.4 decision A: V1.0 does not expose this in the UI. The keys are
// declared so QML can bind read-only and so we can flip on individual
// toggles later without a schema break.
//
// kAdvancedEnableAutoUpdate and kAdvancedEnableTelemetry default to
// false; even when V1.1 turns them on, the default for a fresh
// install is "off" (V5.4.4 D, privacy-first).
// -----------------------------------------------------------------------------
constexpr const char kAdvancedLogLevelKey[] = "advanced/log_level";
constexpr const char kAdvancedAutoUpdateKey[] = "advanced/auto_update";
constexpr const char kAdvancedTelemetryKey[] = "advanced/telemetry";

inline QString defaultAdvancedLogLevel() { return QStringLiteral("info"); }
constexpr bool kDefaultAdvancedAutoUpdate = false;
constexpr bool kDefaultAdvancedTelemetry  = false;

// "error" / "warn" / "info" / "debug"
bool isValidLogLevel(const QString& level);

// -----------------------------------------------------------------------------
// Reset (Category::Reset)
//
// V5.4.4 decision A: one "Reset All Settings" button. SettingsManager
// exposes reset() which clears every key it manages and leaves
// everything else on disk alone (wallet .keys, address_book.json,
// tx_notes.json, account_tags.json, subaddress_labels.json,
// ~/.judecoin-mainnet/). Reset is NOT a key, it's an action; listed
// here so the UI can iterate allCategories() to build the page.
// -----------------------------------------------------------------------------
constexpr const char kResetMarkerKey[] = "reset/last_reset_at_ms";

// -----------------------------------------------------------------------------
// About (Category::About)
//
// V5.4.4 decision A: read-only version label. Not a stored setting -
// SettingsManager synthesises aboutText() from QApplication::application-
// Version() and the daemon binary's version (read at boot by the
// CommandSetLoader). The key list intentionally has nothing here.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// KeySet
//
// Complete enumeration of every QSettings key SettingsManager owns. Used
// by reset() to walk the namespace and by the unit tests to assert no
// stray keys are written.
// -----------------------------------------------------------------------------
QStringList allOwnedKeys();

} // namespace judecoin_wallet::core::settings

#endif // JUDECOIN_WALLET_CORE_SETTINGS_SETTINGS_SCHEMA_H