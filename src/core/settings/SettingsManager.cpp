// =============================================================================
// Judecoin Wallet - SettingsManager implementation (st-014)
//
// See SettingsManager.h for the design notes and SettingsSchema.h for
// the schema. This file is straight-line QSettings access; no
// asynchronous paths, no worker thread. Every setter:
//   1. Validates the value against SettingsSchema's "is valid X"
//      helpers. Invalid input is silently coerced to the default
//      (and an empty typed signal is emitted with the default value,
//      so QML sees the actual on-disk state).
//   2. Writes to QSettings.
//   3. Emits the typed signal (only if the value actually changed).
//   4. Emits settingsChanged() as a catch-all pulse.
// =============================================================================

#include "core/settings/SettingsManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QLatin1String>
#include <QSettings>
#include <QStringBuilder>
#include <QtGlobal> // qEnvironmentVariable (XDG_DATA_HOME probe, st-134)

namespace judecoin_wallet::core::settings {

// -----------------------------------------------------------------------------
// SettingsManager::Private
//
// Pimpl holding the QSettings instance. Keeping QSettings out of the
// header trims the include graph for QML consumers (which only need
// the public Q_PROPERTY API and shouldn't drag in <QSettings>).
// -----------------------------------------------------------------------------
struct SettingsManager::Private
{
    std::unique_ptr<QSettings> settings;
};

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent)
    , d_(std::make_unique<Private>())
{
    ensureApplicationIdentity();

    // QSettings ctor uses QCoreApplication::organizationName() +
    // applicationName() if not given a path. We've just set both above,
    // so this lands at:
    //   Linux:   ~/.config/JudecoinWallet/JudecoinWallet.conf
    //   macOS:   ~/Library/Preferences/JudecoinWallet.plist
    //   Windows: HKCU\Software\JudecoinWallet\JudecoinWallet
    //
    // Construct a fresh QSettings instance so the org/app names set
    // above take effect even if a previous module in the same process
    // already constructed one with different names.
    d_->settings = std::make_unique<QSettings>(QSettings::IniFormat,
                             QSettings::UserScope,
                             QString::fromLatin1(kOrganizationName),
                             QString::fromLatin1(kApplicationName));
}

SettingsManager::~SettingsManager() = default;

void SettingsManager::ensureApplicationIdentity()
{
    if (QCoreApplication::organizationName() != QLatin1String(kOrganizationName)) {
        QCoreApplication::setOrganizationName(QString::fromLatin1(kOrganizationName));
    }
    if (QCoreApplication::applicationName() != QLatin1String(kApplicationName)) {
        QCoreApplication::setApplicationName(QString::fromLatin1(kApplicationName));
    }
}

bool SettingsManager::isUsable() const
{
    // isWritable() returns true iff we can persist changes. On a fresh
    // install QSettings will create the .conf file on first write.
    return d_->settings->isWritable();
}

QString SettingsManager::storagePath() const
{
    return d_->settings->fileName();
}

// -----------------------------------------------------------------------------
// Read helpers
// -----------------------------------------------------------------------------

QString SettingsManager::readString(const char* key, const QString& defaultValue) const
{
    const QString v = d_->settings->value(QLatin1String(key)).toString();
    if (v.isNull()) {
        return defaultValue;
    }
    return v;
}

int SettingsManager::readInt(const char* key, int defaultValue) const
{
    const QVariant v = d_->settings->value(QLatin1String(key));
    if (!v.isValid() || !v.canConvert<int>()) {
        return defaultValue;
    }
    return v.toInt();
}

bool SettingsManager::readBool(const char* key, bool defaultValue) const
{
    const QVariant v = d_->settings->value(QLatin1String(key));
    if (!v.isValid() || !v.canConvert<bool>()) {
        return defaultValue;
    }
    return v.toBool();
}

// -----------------------------------------------------------------------------
// Write helper (template)
//
// Defined out-of-line in the header as private; here we use the
// non-template convenience overloads instead. Kept as a comment so
// future contributors know the intent: validate -> persist -> emit.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Language
// -----------------------------------------------------------------------------

QString SettingsManager::languageLocale() const
{
    const QString v = readString(kLanguageKey, defaultLanguage());
    if (!isValidLanguage(v)) {
        return defaultLanguage();
    }
    return v;
}

void SettingsManager::setLanguageLocale(const QString& locale)
{
    const QString normalised = isValidLanguage(locale) ? locale : defaultLanguage();
    if (normalised == languageLocale()) {
        return; // no-op; QSettings would still rewrite the file
    }
    d_->settings->setValue(QLatin1String(kLanguageKey), normalised);
    d_->settings->sync();
    emit languageLocaleChanged(normalised);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// AutoLock
// -----------------------------------------------------------------------------

int SettingsManager::autoLockSeconds() const
{
    const int v = readInt(kAutoLockKey, defaultAutoLockSeconds());
    if (!isValidAutoLockSeconds(v)) {
        return defaultAutoLockSeconds();
    }
    return v;
}

void SettingsManager::setAutoLockSeconds(int seconds)
{
    const int normalised = isValidAutoLockSeconds(seconds)
                               ? seconds
                               : defaultAutoLockSeconds();
    if (normalised == autoLockSeconds()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kAutoLockKey), normalised);
    d_->settings->sync();
    emit autoLockSecondsChanged(normalised);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// Transfer confirm
// -----------------------------------------------------------------------------

bool SettingsManager::transferConfirmDialog() const
{
    return readBool(kTransferConfirmDialogKey, kDefaultTransferConfirmDialog);
}

void SettingsManager::setTransferConfirmDialog(bool on)
{
    if (on == transferConfirmDialog()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kTransferConfirmDialogKey), on);
    d_->settings->sync();
    emit transferConfirmDialogChanged(on);
    emit settingsChanged();
}

bool SettingsManager::transferConfirmPassword() const
{
    return readBool(kTransferConfirmPasswordKey, kDefaultTransferConfirmPassword);
}

void SettingsManager::setTransferConfirmPassword(bool on)
{
    if (on == transferConfirmPassword()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kTransferConfirmPasswordKey), on);
    d_->settings->sync();
    emit transferConfirmPasswordChanged(on);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// Appearance
// -----------------------------------------------------------------------------

QString SettingsManager::appearanceTheme() const
{
    const QString v = readString(kAppearanceThemeKey, defaultAppearanceTheme());
    if (!isValidAppearanceTheme(v)) {
        return defaultAppearanceTheme();
    }
    return v;
}

void SettingsManager::setAppearanceTheme(const QString& theme)
{
    const QString normalised = isValidAppearanceTheme(theme) ? theme
                                                             : defaultAppearanceTheme();
    if (normalised == appearanceTheme()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kAppearanceThemeKey), normalised);
    d_->settings->sync();
    emit appearanceThemeChanged(normalised);
    emit settingsChanged();
}

QString SettingsManager::appearanceStyle() const
{
    const QString v = readString(kAppearanceStyleKey, defaultAppearanceStyle());
    if (!isValidAppearanceStyle(v)) {
        return defaultAppearanceStyle();
    }
    return v;
}

void SettingsManager::setAppearanceStyle(const QString& style)
{
    const QString normalised = isValidAppearanceStyle(style) ? style
                                                             : defaultAppearanceStyle();
    if (normalised == appearanceStyle()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kAppearanceStyleKey), normalised);
    d_->settings->sync();
    emit appearanceStyleChanged(normalised);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// Advanced
// -----------------------------------------------------------------------------

QString SettingsManager::advancedLogLevel() const
{
    const QString v = readString(kAdvancedLogLevelKey, defaultAdvancedLogLevel());
    if (!isValidLogLevel(v)) {
        return defaultAdvancedLogLevel();
    }
    return v;
}

void SettingsManager::setAdvancedLogLevel(const QString& level)
{
    const QString normalised = isValidLogLevel(level) ? level : defaultAdvancedLogLevel();
    if (normalised == advancedLogLevel()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kAdvancedLogLevelKey), normalised);
    d_->settings->sync();
    emit advancedLogLevelChanged(normalised);
    emit settingsChanged();
}

bool SettingsManager::advancedAutoUpdate() const
{
    return readBool(kAdvancedAutoUpdateKey, kDefaultAdvancedAutoUpdate);
}

void SettingsManager::setAdvancedAutoUpdate(bool on)
{
    if (on == advancedAutoUpdate()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kAdvancedAutoUpdateKey), on);
    d_->settings->sync();
    emit advancedAutoUpdateChanged(on);
    emit settingsChanged();
}

bool SettingsManager::advancedTelemetry() const
{
    return readBool(kAdvancedTelemetryKey, kDefaultAdvancedTelemetry);
}

void SettingsManager::setAdvancedTelemetry(bool on)
{
    if (on == advancedTelemetry()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kAdvancedTelemetryKey), on);
    d_->settings->sync();
    emit advancedTelemetryChanged(on);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// Network (read-only in V1.0 UI; setters exist for StartupManager to
// publish the discovered daemon host/port).
// -----------------------------------------------------------------------------

QString SettingsManager::networkDaemonHost() const
{
    return readString(kNetworkDaemonHost, QString::fromLatin1(kDefaultDaemonHost));
}

void SettingsManager::setNetworkDaemonHost(const QString& host)
{
    const QString normalised = host.isEmpty() ? QString::fromLatin1(kDefaultDaemonHost)
                                              : host;
    if (normalised == networkDaemonHost()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kNetworkDaemonHost), normalised);
    d_->settings->sync();
    emit networkDaemonHostChanged(normalised);
    emit settingsChanged();
}

int SettingsManager::networkDaemonPort() const
{
    return readInt(kNetworkDaemonPort, kDefaultDaemonPort);
}

void SettingsManager::setNetworkDaemonPort(int port)
{
    if (port <= 0 || port > 65535) {
        port = kDefaultDaemonPort;
    }
    if (port == networkDaemonPort()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kNetworkDaemonPort), port);
    d_->settings->sync();
    emit networkDaemonPortChanged(port);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// About
// -----------------------------------------------------------------------------

QString SettingsManager::aboutVersion() const
{
    return QCoreApplication::applicationVersion();
}

QString SettingsManager::aboutQtVersion() const
{
    return QString::fromLatin1(qVersion());
}

QString SettingsManager::aboutDaemonVersion() const
{
    // The daemon version is empty until StartupManager publishes it.
    // We deliberately don't persist this in QSettings - it's only
    // meaningful for the current process lifetime (the daemon may be
    // restarted with a different binary).
    return aboutDaemonVersion_;
}

void SettingsManager::setAboutDaemonVersion(const QString& v)
{
    if (v == aboutDaemonVersion_) {
        return;
    }
    aboutDaemonVersion_ = v;
    // NOT persisted to QSettings - see comment in aboutDaemonVersion().
    emit aboutDaemonVersionChanged(v);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Remote node (st-023)
// -----------------------------------------------------------------------------
//
// Four owned keys, three text-like and one bool. Validators in
// SettingsSchema guarantee the persisted values are sane; setters here
// are no-ops when the value hasn't actually changed (matches the rest
// of SettingsManager's "set same value -> no signal" rule).

QString SettingsManager::remoteDaemonHost() const
{
    return readString(kRemoteDaemonHostKey, kDefaultRemoteDaemonHost);
}

void SettingsManager::setRemoteDaemonHost(const QString& host)
{
    const QString normalised =
        isValidRemoteHost(host) ? host : kDefaultRemoteDaemonHost;
    if (normalised == remoteDaemonHost()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kRemoteDaemonHostKey), normalised);
    d_->settings->sync();
    emit remoteDaemonHostChanged(normalised);
    emit settingsChanged();
}

int SettingsManager::remoteDaemonPort() const
{
    const int v = readInt(kRemoteDaemonPortKey, kDefaultRemoteDaemonPort);
    return isValidRemotePort(v) ? v : kDefaultRemoteDaemonPort;
}

void SettingsManager::setRemoteDaemonPort(int port)
{
    const int normalised =
        isValidRemotePort(port) ? port : kDefaultRemoteDaemonPort;
    if (normalised == remoteDaemonPort()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kRemoteDaemonPortKey), normalised);
    d_->settings->sync();
    emit remoteDaemonPortChanged(normalised);
    emit settingsChanged();
}

bool SettingsManager::isCustomRemote() const
{
    return readBool(kIsCustomRemoteKey, kDefaultIsCustomRemote);
}

void SettingsManager::setIsCustomRemote(bool on)
{
    if (on == isCustomRemote()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kIsCustomRemoteKey), on);
    d_->settings->sync();
    emit isCustomRemoteChanged(on);
    emit settingsChanged();
}

QString SettingsManager::presetNode() const
{
    const QString v = readString(kPresetNodeKey, kDefaultPresetNode);
    return isValidPresetId(v) ? v : QString();
}

void SettingsManager::setPresetNode(const QString& id)
{
    const QString normalised = isValidPresetId(id) ? id : QString();
    if (normalised == presetNode()) {
        return;
    }
    d_->settings->setValue(QLatin1String(kPresetNodeKey), normalised);
    d_->settings->sync();
    emit presetNodeChanged(normalised);
    emit settingsChanged();
}

// -----------------------------------------------------------------------------
// Data directory (st-133c, P0 NEW)
//
// Where judecoind keeps blockchain + wallet state. Empty stored value
// means "use the default" (QDir::homePath() + "/.judecoin") so a fresh
// install just works without any QSettings write. The startup flow is:
//   1. SettingsManager::dataDirectory() returns the stored path OR the
//      default if nothing is stored.
//   2. StartupManager::stepReadSettings caches the result and creates
//      the directory with QDir().mkpath().
//   3. StartupManager::stepStartDaemon appends
//        --data-dir <dir>
//      to the judecoind argv (right after --non-interactive).
// Without an explicit --data-dir, judecoind resolves $HOME/.judecoin
// itself, which inside an AppImage mount is /tmp/.mount_JudecoXXX/...
// rather than the user's real home -- the lmdb ends up inside the
// mount, vanishes when the AppImage exits, and a fresh launch has to
// re-sync the whole blockchain.
// -----------------------------------------------------------------------------

QString SettingsManager::dataDirectory() const
{
    const QString v = readString(kDataDirectoryKey, QString());
    if (!v.isEmpty()) return v;

    // Empty == "use default". Per st-134 (XDG migration) the default
    // follows the XDG Base Directory Specification:
    //   - $XDG_DATA_HOME, when set, is the base directory
    //   - otherwise $HOME/.local/share is the base
    // Final path under that base is /judecoin-mainnet/ so the daemon
    // data lives alongside the conventional ~/.judecoin-mainnet/
    // wallet cache but in an XDG-compliant location instead of
    // writing the lmdb into $HOME directly.
    //
    // Override at startup via the JUDECOIN_DATA_DIR env var
    // (consumed by main.cpp -> settingsManager.setDataDirectory()
    // BEFORE QSettings reads; arrives through QSettings, NOT
    // through this fallback). main.cpp is the single entry point
    // for env overrides; this function owns the XDG default.
    //
    // No automatic migration of ~/.judecoin/ or
    // /var/lib/judecoin-mainnet/ legacy data -- a fresh start on
    // the new path is intentional (Fix C option γ per audit/2026-07-17/
    // 2026-07-17-fix-c-data-dir-xdg-migration.md §4.1).
    //
    // QDir::homePath() is process state (not a compile constant), so
    // the default is computed at read-time.
    const QString xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
    const QString base = xdgDataHome.isEmpty()
                             ? (QDir::homePath() + QStringLiteral("/.local/share"))
                             : xdgDataHome;
    return base + QStringLiteral("/judecoin-mainnet");
}

void SettingsManager::setDataDirectory(const QString& dir)
{
    // Empty input is valid: it means "revert to default". The next
    // dataDirectory() read will return the default again. We persist
    // the literal empty string so the user can see "I cleared this".
    // Whitespace-only input is treated as empty (defensive: a UI bug
    // that hands us " " shouldn't break the persistence layer).
    QString normalised = dir.trimmed();

    if (normalised == dataDirectory()) {
        return; // no-op; matches the "same value -> no signal" rule.
    }
    d_->settings->setValue(QLatin1String(kDataDirectoryKey), normalised);
    d_->settings->sync();
    // The signal payload is the EFFECTIVE data directory (post-default
    // resolution), not the raw stored value, so listeners always see
    // a usable path.
    emit dataDirectoryChanged(dataDirectory());
    emit settingsChanged();
}

// snapshot()
//
// Returns a logical-name -> stringified-value map for diagnostics /
// the Settings page. Logical names match the SettingsSchema enum so
// the UI can iterate allCategories() and pair each with a snapshot()
// entry.
// -----------------------------------------------------------------------------

QHash<QString, QString> SettingsManager::snapshot() const
{
    QHash<QString, QString> out;
    out.insert(QStringLiteral("network/daemon_host"),  networkDaemonHost());
    out.insert(QStringLiteral("network/daemon_port"),  QString::number(networkDaemonPort()));
    out.insert(QStringLiteral("network/remote_daemon_host"), remoteDaemonHost());
    out.insert(QStringLiteral("network/remote_daemon_port"), QString::number(remoteDaemonPort()));
    out.insert(QStringLiteral("network/is_custom_remote"),   isCustomRemote() ? QStringLiteral("true") : QStringLiteral("false"));
    out.insert(QStringLiteral("network/preset_node"),       presetNode());
    out.insert(QStringLiteral("network/data_directory"),    dataDirectory());
    out.insert(QStringLiteral("language/locale"),      languageLocale());
    out.insert(QStringLiteral("auto_lock/seconds"),    QString::number(autoLockSeconds()));
    out.insert(QStringLiteral("transfer_confirm/dialog"),
               transferConfirmDialog() ? QStringLiteral("true") : QStringLiteral("false"));
    out.insert(QStringLiteral("transfer_confirm/password"),
               transferConfirmPassword() ? QStringLiteral("true") : QStringLiteral("false"));
    out.insert(QStringLiteral("appearance/theme"),     appearanceTheme());
    out.insert(QStringLiteral("appearance/style"),     appearanceStyle());
    out.insert(QStringLiteral("advanced/log_level"),   advancedLogLevel());
    out.insert(QStringLiteral("advanced/auto_update"),
               advancedAutoUpdate() ? QStringLiteral("true") : QStringLiteral("false"));
    out.insert(QStringLiteral("advanced/telemetry"),
               advancedTelemetry() ? QStringLiteral("true") : QStringLiteral("false"));
    out.insert(QStringLiteral("about/version"),        aboutVersion());
    out.insert(QStringLiteral("about/qt_version"),     aboutQtVersion());
    out.insert(QStringLiteral("about/daemon_version"), aboutDaemonVersion());
    return out;
}

// -----------------------------------------------------------------------------
// reset()
//
// V5.4.4 decision A: clear every key SettingsManager owns. Wallet .keys,
// address book, tx notes, account tags, subaddress labels, and
// ~/.judecoin-mainnet/ are ALL OUT OF SCOPE - we only touch the keys
// listed in SettingsSchema::allOwnedKeys().
//
// The function walks allOwnedKeys() and removes each one from the
// QSettings instance. Then we sync() to flush. Finally we stamp the
// reset marker (epoch-ms) and build the ResetReport.
//
// We deliberately do NOT call QSettings::clear() - that would wipe
// ANY key under the org/app namespace, including keys owned by
// future modules (st-023 remote-node, etc.) that may have been
// added since SettingsManager was constructed. Surgical removal is
// safer.
// -----------------------------------------------------------------------------

ResetReport SettingsManager::reset()
{
    ResetReport report;
    report.resetAtMs = QDateTime::currentMSecsSinceEpoch();

    // Collect the keys we owned BEFORE removing them so the report can
    // list the logical names that had non-default values.
    for (const QString& key : allOwnedKeys()) {
        if (d_->settings->contains(key)) {
            // Map raw QSettings key to a logical name for the report.
            // We strip the prefix and lowercase the first letter.
            QString logical = key;
            const int slash = logical.indexOf(QLatin1Char('/'));
            if (slash >= 0) {
                logical = logical.mid(slash + 1);
            }
            if (!logical.isEmpty()) {
                logical[0] = logical[0].toLower();
            }
            report.clearedKeys.append(logical);
            d_->settings->remove(key);
        }
    }

    // Stamp the reset marker. This is an owned key (in
    // SettingsSchema::allOwnedKeys()), so it'll be reported on the
    // NEXT reset as "cleared" too - that's the intended UX: every
    // reset clears everything including the previous marker.
    d_->settings->setValue(QLatin1String(kResetMarkerKey), report.resetAtMs);
    d_->settings->sync();

    // Hard-coded "what we never touch" list. Mirrors the spec_notes
    // in the devplan for st-014 and the same list in ResetManager's
    // docs. Kept as a constant here so the UI's "Reset All" preview
    // can show it before the user confirms.
    report.untouchedItems = QStringList{
        QStringLiteral("wallet .keys files"),
        QStringLiteral("address_book.json"),
        QStringLiteral("tx_notes.json"),
        QStringLiteral("account_tags.json"),
        QStringLiteral("subaddress_labels.json"),
        QStringLiteral("~/.judecoin-mainnet/"),
    };

    // Refresh all QML bindings by emitting the typed signals for every
    // property. The cleanest way is to call each setter with the
    // current default value - but setters no-op if value is unchanged.
    // Instead, we emit the typed signals explicitly so QML re-binds
    // even if the value stayed at default (e.g. a binding that
    // overrode the previous non-default value should snap back).
    emit languageLocaleChanged(languageLocale());
    emit autoLockSecondsChanged(autoLockSeconds());
    emit transferConfirmDialogChanged(transferConfirmDialog());
    emit transferConfirmPasswordChanged(transferConfirmPassword());
    emit appearanceThemeChanged(appearanceTheme());
    emit appearanceStyleChanged(appearanceStyle());
    emit advancedLogLevelChanged(advancedLogLevel());
    emit advancedAutoUpdateChanged(advancedAutoUpdate());
    emit advancedTelemetryChanged(advancedTelemetry());
    emit networkDaemonHostChanged(networkDaemonHost());
    emit networkDaemonPortChanged(networkDaemonPort());
    emit remoteDaemonHostChanged(remoteDaemonHost());
    emit remoteDaemonPortChanged(remoteDaemonPort());
    emit isCustomRemoteChanged(isCustomRemote());
    emit presetNodeChanged(presetNode());
    emit dataDirectoryChanged(dataDirectory());
    emit settingsChanged();
    emit settingsReset(report);

    return report;
}

} // namespace judecoin_wallet::core::settings

// =============================================================================
// SettingsSchema free-function definitions
//
// These live in the .cpp (rather than the header) because they only
// need QStringList and they're not on any hot path. Keeping them out
// of the header means <QStringList> isn't pulled into every translation
// unit that just wants to know the default value of one field.
// =============================================================================

namespace judecoin_wallet::core::settings {

QString categoryLabel(Category c)
{
    switch (c) {
    case Category::Network:         return QStringLiteral("Network");
    case Category::Language:        return QStringLiteral("Language");
    case Category::AutoLock:        return QStringLiteral("Auto-Lock");
    case Category::TransferConfirm: return QStringLiteral("Transfer Confirm");
    case Category::Appearance:      return QStringLiteral("Appearance");
    case Category::Advanced:        return QStringLiteral("Advanced");
    case Category::Reset:           return QStringLiteral("Reset");
    case Category::About:           return QStringLiteral("About");
    }
    return QStringLiteral("Unknown");
}

QList<Category> allCategories()
{
    return QList<Category>{
        Category::Network,
        Category::Language,
        Category::AutoLock,
        Category::TransferConfirm,
        Category::Appearance,
        Category::Advanced,
        Category::Reset,
        Category::About,
    };
}

bool isValidLanguage(const QString& locale)
{
    if (locale.isEmpty()) {
        return true; // "auto" / "follow system"
    }
    return supportedLanguages().contains(locale);
}

QString languageDisplayName(const QString& locale)
{
    if (locale.isEmpty()) {
        return QStringLiteral("Follow system locale");
    }
    // V1.0 ships English-only labels for the language combo. st-016
    // (i18n) replaces these with localised strings once we wire
    // tr() into the QML side.
    if (locale == QLatin1String("en"))    return QStringLiteral("English");
    if (locale == QLatin1String("zh_TW")) return QStringLiteral("Chinese (Traditional)");
    if (locale == QLatin1String("de"))    return QStringLiteral("German");
    if (locale == QLatin1String("ja"))    return QStringLiteral("Japanese");
    if (locale == QLatin1String("ko"))    return QStringLiteral("Korean");
    if (locale == QLatin1String("it"))    return QStringLiteral("Italian");
    if (locale == QLatin1String("nl"))    return QStringLiteral("Dutch");
    if (locale == QLatin1String("ru"))    return QStringLiteral("Russian");
    if (locale == QLatin1String("es"))    return QStringLiteral("Spanish");
    return locale; // unknown -> show raw code so the user can still pick it
}

bool isValidAutoLockSeconds(int seconds)
{
    return allowedAutoLockSeconds().contains(seconds);
}

QString autoLockDisplayName(int seconds)
{
    switch (seconds) {
    case kAutoLockFiveMin:    return QStringLiteral("5 minutes");
    case kAutoLockFifteenMin: return QStringLiteral("15 minutes");
    case kAutoLockThirtyMin:  return QStringLiteral("30 minutes");
    case kAutoLockOneHour:    return QStringLiteral("1 hour");
    case kAutoLockNever:      return QStringLiteral("Never");
    }
    return QStringLiteral("Unknown");
}

bool isValidAppearanceTheme(const QString& theme)
{
    return theme == QLatin1String("system")
           || theme == QLatin1String("light")
           || theme == QLatin1String("dark");
}

bool isValidAppearanceStyle(const QString& style)
{
    return style == QLatin1String("system")
           || style == QLatin1String("fusion")
           || style == QLatin1String("material");
}

bool isValidLogLevel(const QString& level)
{
    return level == QLatin1String("error")
           || level == QLatin1String("warn")
           || level == QLatin1String("info")
           || level == QLatin1String("debug");
}

QStringList allOwnedKeys()
{
    // Every QSettings key SettingsManager reads or writes. Order
    // matches the order in SettingsSchema.h for grep-ability.
    return QStringList{
        QString::fromLatin1(kNetworkDaemonHost),
        QString::fromLatin1(kNetworkDaemonPort),
        QString::fromLatin1(kRemoteDaemonHostKey),
        QString::fromLatin1(kRemoteDaemonPortKey),
        QString::fromLatin1(kIsCustomRemoteKey),
        QString::fromLatin1(kPresetNodeKey),
        QString::fromLatin1(kDataDirectoryKey),
        QString::fromLatin1(kLanguageKey),
        QString::fromLatin1(kAutoLockKey),
        QString::fromLatin1(kTransferConfirmDialogKey),
        QString::fromLatin1(kTransferConfirmPasswordKey),
        QString::fromLatin1(kAppearanceThemeKey),
        QString::fromLatin1(kAppearanceStyleKey),
        QString::fromLatin1(kAdvancedLogLevelKey),
        QString::fromLatin1(kAdvancedAutoUpdateKey),
        QString::fromLatin1(kAdvancedTelemetryKey),
        QString::fromLatin1(kResetMarkerKey),
    };
}

} // namespace judecoin_wallet::core::settings