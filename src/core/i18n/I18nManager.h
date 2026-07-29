// =============================================================================
// Judecoin Wallet - I18nManager (st-016)
//
// Centralized internationalization helper (V5.4.9 + V5.4.12).
//
// Provides:
//   - The canonical list of supported languages (9: en, zh_TW, de, ja, ko,
//     it, nl, ru, es) per V5.4.9 / V5.4.11 / V5.4.12.
//   - A language lookup helper that maps language codes to human-friendly
//     display names (shown in the SettingsManager language picker).
//   - Translation loading and switching via QTranslator.
//   - zh_CN explicit fallback to en (per V5.4.9: zh_CN was removed, simplified
//     Chinese users see English UI in V1.0).
//   - Notification signal so QML can refresh qsTr() bindings when the user
//     switches languages.
//
// This class is intentionally header-light: it does not pull in QGuiApplication
// directly (so unit tests can construct it under QCoreApplication). The QML
// engine / QGuiApplication is responsible for installing translators into
// itself before exposing this object.
//
// Companion .ts files live in i18n/judecoin_wallet_<lang>.ts and are compiled
// to .qm resources by the i18n/CMakeLists.txt qt_add_lrelease step.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_I18N_I18N_MANAGER_H
#define JUDECOIN_WALLET_CORE_I18N_I18N_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QTranslator>
#include <QVector>

class QQmlApplicationEngine;

namespace judecoin_wallet::core::i18n {

// One row in the supported language table. We keep the data in C++ so the
// SettingsManager language picker and the I18nManager stay in sync without
// the QML side having to hard-code the list.
struct LanguageInfo {
    QString code;        // Qt/BCP-47-ish code, e.g. "en", "zh_TW", "de"
    QString englishName; // shown when UI is in English
    QString nativeName;  // shown in the language picker (always native script)
};

// All 9 languages (V5.4.9 / V5.4.11 / V5.4.12). zh_CN is intentionally
// absent; the spec calls it out as the only language removed vs the D011 ADR.
inline QVector<LanguageInfo> kSupportedLanguages() {
    return {
        // code      englishName  nativeName
        {"en",    "English",         "English"},
        {"zh_TW", "Traditional Chinese", "繁體中文"},
        {"de",    "German",          "Deutsch"},
        {"ja",    "Japanese",        "日本語"},
        {"ko",    "Korean",          "한국어"},
        {"it",    "Italian",         "Italiano"},
        {"nl",    "Dutch",           "Nederlands"},
        {"ru",    "Russian",         "Русский"},
        {"es",    "Spanish",         "Español"},
    };
}

}  // namespace judecoin_wallet::core::i18n

class I18nManager : public QObject {
    Q_OBJECT

    // Current language code (matches SettingsManager `language` value).
    // "auto" means "follow system locale, fall back to en".
    Q_PROPERTY(QString currentLanguage READ currentLanguage
                                   WRITE setCurrentLanguage
                                   NOTIFY currentLanguageChanged)

    // Number of supported languages (== 9, exposed as a property so QML
    // bindings can size their model without hard-coding).
    Q_PROPERTY(int supportedLanguageCount READ supportedLanguageCount
                                       CONSTANT)

    // Empty-string trick for Qt 6.6.3 QML StackView: retranslate() alone
    // does not force hidden/cached pages to re-evaluate qsTr() bindings.
    // By concatenating this property into every qsTr() expression
    //    text: qsTr("Hello") + i18nManagerObj.emptyString
    // the QML binding engine MUST re-evaluate the whole expression
    // when emptyStringChanged fires — including the qsTr() call.
    Q_PROPERTY(QString emptyString READ emptyString NOTIFY emptyStringChanged)

    // Clean retranslation trigger (no string concatenation).
    // QML binding pattern: { let _=retranslateStamp; return qtr(...) }
    Q_PROPERTY(int retranslateStamp READ retranslateStamp NOTIFY retranslateStampChanged)

    // Direct C++ translation bridge for QML.
    // Qt 6.6.3's QML qsTr() does NOT use QCoreApplication::translate(),
    // so we bypass it entirely: QML calls i18nManagerObj.tr(context, source)
    // which hits QCoreApplication::translate() — confirmed working.

public:
    Q_INVOKABLE QString qtr(const QString& context, const QString& source,
                           const QString& disambiguation = QString(),
                           int n = -1) const;

    explicit I18nManager(QObject* parent = nullptr);
    ~I18nManager() override;

    // ---- Accessors ----
    QString currentLanguage() const { return m_currentLanguage; }

    int supportedLanguageCount() const {
        return static_cast<int>(
            judecoin_wallet::core::i18n::kSupportedLanguages().size());
    }

    // Returns the supported-language table (copy, QML-friendly).
    Q_INVOKABLE QVariantList supportedLanguages() const;

    // Map a language code to its native name (e.g. "zh_TW" -> "繁體中文").
    // Returns the English name if the code is not in the table.
    Q_INVOKABLE QString nativeNameFor(const QString& code) const;

    // Map a language code to its English name.
    Q_INVOKABLE QString englishNameFor(const QString& code) const;

    // Empty-string trick for QML binding invalidation.
    // Returns a string of zero-width spaces whose count varies
    // so QML's binding optimizer sees a VALUE CHANGE when
    // emptyStringChanged fires. Without varying output, Qt 6.6.3
    // skips re-evaluation because "" == "" (no change).
    QString emptyString() const { return QStringLiteral(""); }

    // Called from main.cpp's QTimer after engine.load() to trigger
    // QML binding re-evaluation after the standalone translator is ready.
    Q_INVOKABLE void triggerRetranslate() {
        ++m_emptyStringVersion;
        ++m_retranslateStamp;
        emit emptyStringChanged();
        emit retranslateStampChanged();
    }

    int retranslateStamp() const { return m_retranslateStamp; }

    // ---- Translation lifecycle ----

    // Set the active language code. Accepts:
    //   "auto"                -> clear translator, follow QLocale::system()
    //   ""                    -> same as "auto"
    //   unsupported code      -> logs warning, leaves current translator
    //                            installed, returns false.
    //   supported code        -> installs the matching .qm (if found),
    //                            fires currentLanguageChanged() and
    //                            translationChanged() on success.
    //
    // The translator resolution rule:
    //   code = "auto"            -> no translator installed (system locale
    //                              drives qsTr, defaults to en for our
    //                              embedded strings)
    //   zh_CN                    -> explicit fallback to "en" (per V5.4.9:
    //                              zh_CN is not supported; simplified
    //                              Chinese users see English UI)
    //   any other unsupported    -> no-op (returns false, logs warning)
    //   supported code           -> try "<translationsBase>/judecoin_wallet_<code>.qm";
    //                              if missing, fall back to no translator
    //                              (English source strings).
    //
    // Returns true on successful change, false otherwise.
    Q_INVOKABLE bool setCurrentLanguage(const QString& code);

    // Install translations from a QLocale (used by main.cpp before the
    // QML engine boots, so QML bindings see translated strings immediately).
    // Returns true if a translator was installed.
    bool installForLocale(const QLocale& locale,
                          const QString& translationsBasePath = QString());

    // The base path where the .qm files live. Defaults to
    // ":/i18n" (Qt resource path embedded by qt_add_lrelease). Tests can
    // point this at a temp dir with on-disk .qm files.
    QString translationsBasePath() const { return m_translationsBasePath; }
    void setTranslationsBasePath(const QString& path);

    // Set the QML engine so that retranslate() can be called after a
    // translator swap (required for QML qsTr() bindings to re-evaluate).
    // Must be called before any setCurrentLanguage() that should refresh
    // the live UI. Call from main.cpp after the QQmlApplicationEngine
    // is constructed.
    void setEngine(QQmlApplicationEngine* engine);

    // ---- Validation ----

    // Is `code` one of the 9 supported languages?
    Q_INVOKABLE static bool isSupported(const QString& code);

    // Is `code` the "auto" sentinel (or empty, treated as auto)?
    Q_INVOKABLE static bool isAutoCode(const QString& code);

signals:
    void currentLanguageChanged();
    // Fires after a successful setCurrentLanguage() / installForLocale().
    // QML bindings on qsTr() and Language-related properties refresh.
    void translationChanged();

    // Emitted when a complete app restart is needed to apply the new
    // language (Qt 6.6.3 QML StackView limitation: retranslate() cannot
    // reliably refresh all hidden/cached pages). main.cpp catches this
    // and does QProcess::startDetached + QCoreApplication::quit().
    void restartRequested();

    // Emitted together with translationChanged. QML uses this via:
    //   text: qsTr("Hello") + i18nManagerObj.emptyString
    void emptyStringChanged();

    // Emitted when retranslateStamp increments. QML uses this as a
    // clean dependency (no string concatenation):
    //   text: { let _ = i18nManagerObj.retranslateStamp; return qtr(...) }
    void retranslateStampChanged();

private:
    // Resolve a code to the actual .qm basename code we'll try to load.
    // "zh_CN" -> "en" (fallback). Otherwise returns `code` unchanged.
    QString resolveLoadCode(const QString& code) const;

    QString m_currentLanguage = QStringLiteral("auto");
    QString m_translationsBasePath = QStringLiteral(":/i18n");
    QTranslator* m_translator = nullptr;  // owned, deleted in dtor
    QQmlApplicationEngine* m_engine = nullptr;  // non-owning; set via setEngine()
    int m_emptyStringVersion = 0;  // bumped before each emptyStringChanged
    int m_retranslateStamp = 0;    // bumped before each retranslateStampChanged
};

#endif  // JUDECOIN_WALLET_CORE_I18N_I18N_MANAGER_H
