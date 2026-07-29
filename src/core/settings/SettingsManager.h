// =============================================================================
// Judecoin Wallet - SettingsManager (st-014)
//
// Thin QSettings wrapper that exposes the 8 categories from
// SettingsSchema as Q_PROPERTYs and synchronous getters / setters.
//
// V1.0 design rules (V5.4.4 + V5.4.7):
//   - Synchronous only. QSettings reads on V1.0 run on the order of
//     microseconds (single .conf file, < 1 KB). Wrapping every getter
//     in QFuture would add noise without benefit.
//   - No password storage. AutoLockSeconds and TransferConfirmPassword
//     are session-policy knobs, not secrets. The wallet password itself
//     never touches QSettings (V5.4.7).
//   - Reset only touches keys SettingsManager owns. Wallet .keys,
//     address_book.json, tx_notes.json, account_tags.json,
//     subaddress_labels.json, and the data directory (default
//     $XDG_DATA_HOME/judecoin-mainnet/, fallback
//     $HOME/.local/share/judecoin-mainnet/ per st-134 XDG
//     migration) are all out of scope. reset() returns a ResetReport listing what was cleared and
//     what was deliberately left alone, so the UI can show
//     "12 settings reset, wallet and address book untouched".
//   - Change notifications fire as Qt signals so QML bindings update
//     in-place. Each setter emits the typed signal (e.g.
//     autoLockSecondsChanged) AND the catch-all settingsChanged().
//   - Default values come from SettingsSchema; SettingsManager holds no
//     defaults of its own. Schema is the single source of truth.
//
// Threading: SettingsManager is QObject-based. It does not guard
// concurrent writes; the UI thread is the only caller in V1.0. If a
// future stage introduces a background writer, add a QMutex inside
// each setter.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_SETTINGS_SETTINGS_MANAGER_H
#define JUDECOIN_WALLET_CORE_SETTINGS_SETTINGS_MANAGER_H

#include "core/settings/SettingsSchema.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace judecoin_wallet::core::settings {

// -----------------------------------------------------------------------------
// ResetReport
//
// Returned by SettingsManager::reset(). Records what was cleared (the
// owned keys) and what was deliberately left alone (the "untouched"
// list). Both lists contain logical names ("language", "auto_lock"),
// not raw QSettings keys - the UI presents this in human form.
//
//   clearedKeys      - logical names that were present and got removed.
//                      Empty if the user had never changed any setting
//                      (i.e. everything was already at default).
//   untouchedItems   - human-readable list of "wallet files",
//                      "address book", "tx notes", "account tags",
//                      "subaddress labels", "~/.judecoin-mainnet/".
//                      Constant for V1.0 - the "what we don't touch"
//                      list is hard-coded in reset() and mirrored here.
//   resetAtMs        - epoch-ms when reset was performed. Also stored
//                      under QSettings key kResetMarkerKey so the
//                      About page can show "Last reset: 2026-07-03
//                      03:14" if needed (V1.0 doesn't surface this yet).
// -----------------------------------------------------------------------------
struct ResetReport
{
    QStringList clearedKeys;
    QStringList untouchedItems;
    qint64      resetAtMs = 0;
};

// -----------------------------------------------------------------------------
// SettingsManager
//
// Owns a single QSettings instance. Construction is cheap; reads on
// first access load the whole .conf file into memory. The destructor
// syncs pending writes (QSettings does this automatically on destruction
// anyway, but we make it explicit in the doc).
// -----------------------------------------------------------------------------
class SettingsManager : public QObject
{
    Q_OBJECT

    // -- Q_PROPERTYs for QML -------------------------------------------------
    //
    // QML can bind directly to these. The NOTIFY signal is the per-property
    // changed signal; settingsChanged() is the catch-all that fires for
    // every write (so a single QML "loading" spinner can listen on one
    // connection instead of N).

    Q_PROPERTY(QString languageLocale READ languageLocale WRITE setLanguageLocale NOTIFY languageLocaleChanged)
    Q_PROPERTY(int     autoLockSeconds READ autoLockSeconds WRITE setAutoLockSeconds NOTIFY autoLockSecondsChanged)
    Q_PROPERTY(bool    transferConfirmDialog READ transferConfirmDialog WRITE setTransferConfirmDialog NOTIFY transferConfirmDialogChanged)
    Q_PROPERTY(bool    transferConfirmPassword READ transferConfirmPassword WRITE setTransferConfirmPassword NOTIFY transferConfirmPasswordChanged)

    // Read-only in V1.0 (no UI binding exposes these). The properties are
    // declared anyway so QML can render "Theme: System" labels.
    Q_PROPERTY(QString appearanceTheme READ appearanceTheme WRITE setAppearanceTheme NOTIFY appearanceThemeChanged)
    Q_PROPERTY(QString appearanceStyle READ appearanceStyle WRITE setAppearanceStyle NOTIFY appearanceStyleChanged)
    Q_PROPERTY(QString advancedLogLevel READ advancedLogLevel WRITE setAdvancedLogLevel NOTIFY advancedLogLevelChanged)
    Q_PROPERTY(bool    advancedAutoUpdate READ advancedAutoUpdate WRITE setAdvancedAutoUpdate NOTIFY advancedAutoUpdateChanged)
    Q_PROPERTY(bool    advancedTelemetry READ advancedTelemetry WRITE setAdvancedTelemetry NOTIFY advancedTelemetryChanged)

    // Network host/port are read-only in V1.0: SettingsManager surfaces
    // what the daemon reports at boot; the UI cannot override. st-023
    // will add the writable remote-node override.
    Q_PROPERTY(QString networkDaemonHost READ networkDaemonHost WRITE setNetworkDaemonHost NOTIFY networkDaemonHostChanged)
    Q_PROPERTY(int     networkDaemonPort READ networkDaemonPort WRITE setNetworkDaemonPort NOTIFY networkDaemonPortChanged)
    Q_PROPERTY(QString remoteDaemonHost READ remoteDaemonHost WRITE setRemoteDaemonHost NOTIFY remoteDaemonHostChanged)
    Q_PROPERTY(int     remoteDaemonPort READ remoteDaemonPort WRITE setRemoteDaemonPort NOTIFY remoteDaemonPortChanged)
    Q_PROPERTY(bool    isCustomRemote   READ isCustomRemote   WRITE setIsCustomRemote   NOTIFY isCustomRemoteChanged)
    Q_PROPERTY(QString presetNode       READ presetNode       WRITE setPresetNode       NOTIFY presetNodeChanged)

    // st-133c (P0 NEW) + st-134 (XDG migration): directory where
    // judecoind keeps blockchain + wallet state. StartupManager
    // forwards this to judecoind via --data-dir so the daemon doesn't
    // have to guess it from $HOME (which inside an AppImage mount is
    // wrong).
    //
    // Default (when no QSettings value is stored AND no
    // JUDECOIN_DATA_DIR env override applies) is XDG-aware per st-134:
    //   $XDG_DATA_HOME/judecoin-mainnet/
    //     falls back to $HOME/.local/share/judecoin-mainnet/
    //
    // Override at startup via the JUDECOIN_DATA_DIR environment
    // variable. main.cpp reads it and forwards it to
    // settingsManager.setDataDirectory() before StartupManager reads
    // its cachedDataDir_; the value is persisted to QSettings so
    // subsequent reads return the override even if JUDECOIN_DATA_DIR
    // is unset (intentional: lets the user edit the path in the
    // Settings page after a one-shot env override).
    //
    // st-134 scope: XDG-aware default + env override. Does NOT
    // auto-migrate legacy ~/.judecoin/ or /var/lib/judecoin-mainnet/
    // data; the new default is a fresh path (Fix C option γ per
    // audit/2026-07-17/2026-07-17-fix-c-data-dir-xdg-migration.md
    // §4.1).
    Q_PROPERTY(QString dataDirectory    READ dataDirectory    WRITE setDataDirectory    NOTIFY dataDirectoryChanged)

    // About - synthesised from QApplication + the loaded daemon binary.
    Q_PROPERTY(QString aboutVersion READ aboutVersion CONSTANT)
    Q_PROPERTY(QString aboutQtVersion READ aboutQtVersion CONSTANT)
    Q_PROPERTY(QString aboutDaemonVersion READ aboutDaemonVersion WRITE setAboutDaemonVersion NOTIFY aboutDaemonVersionChanged)

public:
    explicit SettingsManager(QObject* parent = nullptr);
    ~SettingsManager() override;

    // -- construction-time diagnostics ---------------------------------------
    //
    // Returns true iff the QSettings backend is usable. False means the
    // .conf file is missing AND unwritable - in that case all getters
    // return defaults, all setters are no-ops, and reset() returns an
    // empty report. The Settings page should still render; just nothing
    // will persist.

    bool isUsable() const;

    // Effective file path of the QSettings store. Useful for the About
    // page and for diagnostics. May be empty if the backend is INI but
    // no path was resolvable (rare).
    QString storagePath() const;

    // -- Language -------------------------------------------------------------

    QString languageLocale() const;
    void    setLanguageLocale(const QString& locale);

    // -- AutoLock -------------------------------------------------------------

    int  autoLockSeconds() const;
    void setAutoLockSeconds(int seconds);

    // -- Transfer confirm -----------------------------------------------------

    bool transferConfirmDialog() const;
    void setTransferConfirmDialog(bool on);

    bool transferConfirmPassword() const;
    void setTransferConfirmPassword(bool on);

    // -- Appearance (read-only in V1.0 UI, setters still callable for
    //    power users / .conf edits / future V1.1 toggles). --------------------

    QString appearanceTheme() const;
    void    setAppearanceTheme(const QString& theme);

    QString appearanceStyle() const;
    void    setAppearanceStyle(const QString& style);

    // -- Advanced (read-only in V1.0 UI) -------------------------------------

    QString advancedLogLevel() const;
    void    setAdvancedLogLevel(const QString& level);

    bool advancedAutoUpdate() const;
    void setAdvancedAutoUpdate(bool on);

    bool advancedTelemetry() const;
    void setAdvancedTelemetry(bool on);

    // -- Network (read-only in V1.0; st-023 will add writable remote node) ---

    QString networkDaemonHost() const;
    void    setNetworkDaemonHost(const QString& host);

    int  networkDaemonPort() const;
    void setNetworkDaemonPort(int port);

    // -- Remote node (st-023) ----------------------------------------------
    //
    // V1.1: user can pick a preset (kRemotePresetNodes) or type a custom
    // host:port. isCustomRemote() == true forces StartupManager to use the
    // raw remoteDaemonHost/Port fields; otherwise StartupManager looks up
    // presetNode() in the preset table.
    //
    // Empty remoteDaemonHost means "no remote node selected" and the
    // StartupManager falls back to spawning a local daemon (V1.0 default).

    QString remoteDaemonHost() const;
    void    setRemoteDaemonHost(const QString& host);

    int  remoteDaemonPort() const;
    void setRemoteDaemonPort(int port);

    bool isCustomRemote() const;
    void setIsCustomRemote(bool on);

    QString presetNode() const;
    void    setPresetNode(const QString& id);

    // st-133c: judecoind --data-dir target. Empty stored value
    // means "use the XDG-aware default" (see dataDirectory() above).
    QString dataDirectory() const;
    void    setDataDirectory(const QString& dir);

    // -- About ---------------------------------------------------------------

    QString aboutVersion() const;        // QApplication applicationVersion()
    QString aboutQtVersion() const;      // qVersion()
    QString aboutDaemonVersion() const;  // set by StartupManager after boot
    void    setAboutDaemonVersion(const QString& v);

    // -- Bulk access for diagnostics / Settings page -------------------------

    // Returns a plain map of logical-name -> current-value for every owned
    // setting. Used by the Settings page's "show current values" panel and
    // by the diagnostics dump in the About page.
    QHash<QString, QString> snapshot() const;

    // -- Reset ---------------------------------------------------------------

    // Wipe every QSettings key SettingsManager owns and put the .conf
    // back to a freshly-installed state. NEVER touches wallet files,
    // address books, or ~/.judecoin-mainnet/. Returns a ResetReport so
    // the UI can confirm "12 settings reset, wallet and address book
    // untouched".
    // st-093: Q_INVOKABLE so ResetSettings.qml's
    //   `if (typeof settingsManager.reset === "function")
    //        settingsManager.reset()`
    // guard fires. Without Q_INVOKABLE the reset() call is silently
    // skipped (typeof returns "undefined", not "function"), the
    // settingsChanged catch-all never fires, and test #29 fails.
    Q_INVOKABLE ResetReport reset();

signals:
    // Catch-all. Fires after every successful setter. QML can connect
    // a single "settings updated" indicator to this. The typed signal
    // for the specific property fires first; settingsChanged() comes
    // second so QML sees the per-property value update before the
    // generic pulse.
    void settingsChanged();

    void languageLocaleChanged(const QString& locale);
    void autoLockSecondsChanged(int seconds);
    void transferConfirmDialogChanged(bool on);
    void transferConfirmPasswordChanged(bool on);
    void appearanceThemeChanged(const QString& theme);
    void appearanceStyleChanged(const QString& style);
    void advancedLogLevelChanged(const QString& level);
    void advancedAutoUpdateChanged(bool on);
    void advancedTelemetryChanged(bool on);
    void networkDaemonHostChanged(const QString& host);
    void networkDaemonPortChanged(int port);
    void remoteDaemonHostChanged(const QString& host);
    void remoteDaemonPortChanged(int port);
    void isCustomRemoteChanged(bool on);
    void presetNodeChanged(const QString& id);
    void aboutDaemonVersionChanged(const QString& version);
    void dataDirectoryChanged(const QString& dir);

    // Fired by reset() AFTER all owned keys have been cleared and the
    // defaults have been re-applied. QML can connect to this and refresh
    // every binding in one shot. settingsChanged() also fires (once per
    // defaulted property).
    void settingsReset(const ResetReport& report);

private:
    // Set QCoreApplication org/app names from the schema constants.
    // Called once from the ctor. Idempotent: QCoreApplication remembers
    // the first call; subsequent calls are no-ops.
    static void ensureApplicationIdentity();

    // Internal write helper. Persists `value` under `key`, emits the
    // typed signal `sig` (if non-null), then emits settingsChanged().
    // Returns true iff a write actually happened (i.e. value differed
    // from what's already on disk).
    template <typename T, typename Signal>
    bool writeKey(const char* key, const T& value, Signal sig);

    // Internal read helper. Returns the stored value if present and
    // parsable; otherwise returns `defaultValue`.
    QString readString(const char* key, const QString& defaultValue) const;
    int     readInt(const char* key, int defaultValue) const;
    bool    readBool(const char* key, bool defaultValue) const;

    // Owns the QSettings instance. Pimpl-style so we don't have to
    // include <QSettings> in the header (faster builds + smaller
    // dependency surface for QML consumers).
    struct Private;
    std::unique_ptr<Private> d_;

    // Schema version we were constructed against. Used by reset() to
    // decide whether to also write a kResetMarkerKey stamp.
    int schemaVersion_ = kSchemaVersion;

    // The daemon version StartupManager publishes after boot. Lives
    // only for the current process - the daemon may be restarted with
    // a different binary, so we deliberately do not persist it in
    // QSettings. See aboutDaemonVersion() for the rationale.
    QString aboutDaemonVersion_;

    Q_DISABLE_COPY(SettingsManager)
};

} // namespace judecoin_wallet::core::settings

#endif // JUDECOIN_WALLET_CORE_SETTINGS_SETTINGS_MANAGER_H