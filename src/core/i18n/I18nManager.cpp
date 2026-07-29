// =============================================================================
// Judecoin Wallet - I18nManager (st-016) implementation
// =============================================================================

#include "core/i18n/I18nManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcI18n, "judecoin.i18n")

namespace judecoin_wallet::core::i18n {

namespace {

// The "auto" sentinel we use in SettingsManager (empty == auto).
constexpr const char* kAutoCode = "auto";

// zh_CN is intentionally not in the supported table; if we see it
// (e.g. user upgraded from a build that had it), we fall back to English.
constexpr const char* kZhCnFallback = "en";

}  // namespace

}  // namespace judecoin_wallet::core::i18n

I18nManager::I18nManager(QObject* parent)
    : QObject(parent),
      m_translator(new QTranslator(this)) {
}

I18nManager::~I18nManager() {
    // Detach from QCoreApplication if we ever installed one.
    if (m_translator && m_translator->isEmpty() == false) {
        QCoreApplication::removeTranslator(m_translator);
    }
    delete m_translator;
    m_translator = nullptr;
}

QVariantList I18nManager::supportedLanguages() const {
    QVariantList result;
    const auto langs = judecoin_wallet::core::i18n::kSupportedLanguages();
    result.reserve(langs.size());
    for (const auto& lang : langs) {
        QVariantMap row;
        row[QStringLiteral("code")] = lang.code;
        row[QStringLiteral("englishName")] = lang.englishName;
        row[QStringLiteral("nativeName")] = lang.nativeName;
        result.push_back(row);
    }
    return result;
}

QString I18nManager::nativeNameFor(const QString& code) const {
    const auto langs = judecoin_wallet::core::i18n::kSupportedLanguages();
    for (const auto& lang : langs) {
        if (lang.code == code) {
            return lang.nativeName;
        }
    }
    // Unsupported (including zh_CN which we explicitly dropped): return
    // the code itself so the UI can show something distinguishable.
    return code;
}

QString I18nManager::englishNameFor(const QString& code) const {
    const auto langs = judecoin_wallet::core::i18n::kSupportedLanguages();
    for (const auto& lang : langs) {
        if (lang.code == code) {
            return lang.englishName;
        }
    }
    return code;
}

QString I18nManager::qtr(const QString& context, const QString& source,
                        const QString& disambiguation, int n) const
{
    // ULTIMATE FIX: Create a standalone translator loaded fresh from
    // the same .qm resource, completely independent of m_translator
    // (which gets corrupted/destroyed during QML init).
    //
    // This translator lives on the stack — created on every call.
    // Yes it's a performance hit, but it eliminates ALL side effects
    // from other code messing with the app's translator list.
    static QTranslator s_standalone;  // persistent, loaded once
    static QString s_loadedLang;
    static bool s_loaded = false;
    if (s_loadedLang != m_currentLanguage) {
        // Re-load standalone translator for the current language
        // just call load() again to replace data
        s_loadedLang = m_currentLanguage;
        s_loaded = false;
    }
    if (!s_loaded) {
        QString langCode = m_currentLanguage;
        if (langCode.isEmpty() || langCode == QStringLiteral("auto")) {
            langCode = QStringLiteral("en");
        }
        const QString qmName = QStringLiteral("judecoin_wallet_%1_manual.qm").arg(langCode);

        // (1) AppImage fusermount path — usr/share/judecoin/i18n_manual/
        //     relative to the binary (usr/bin/). Works on any machine.
        const QString appDir = QCoreApplication::applicationDirPath();
        bool loaded = s_standalone.load(
            appDir + QStringLiteral("/../share/judecoin/i18n_manual/") + qmName);
        if (loaded) qInfo().noquote() << "qtr: loaded" << langCode << "from AppImage path";

        // (2) Embedded resource :/i18n_manual/ (dev / Qt resource)
        if (!loaded) {
            QFile qmFile(QStringLiteral(":/i18n_manual/") + qmName);
            if (qmFile.open(QIODevice::ReadOnly)) {
                QByteArray data = qmFile.readAll();
                qmFile.close();
                loaded = s_standalone.load(
                    reinterpret_cast<const uchar*>(data.constData()),
                    static_cast<int>(data.size()));
                if (loaded) qInfo().noquote() << "qtr: loaded" << langCode << "from :/i18n_manual/";
            }
        }

        s_loaded = loaded;
        if (!loaded) {
            qWarning().noquote() << "qtr: FAILED to load translation for" << langCode;
        }
    }

    QByteArray ctxBytes = context.toUtf8();
    QByteArray srcBytes = source.toUtf8();

    QString result = s_standalone.translate(
        ctxBytes.constData(),
        srcBytes.constData(),
        disambiguation.isEmpty() ? nullptr
            : disambiguation.toUtf8().constData(),
        n);

    if (result.isEmpty()) {
        result = source;
    }

    // Diagnostic
    static QSet<QString> s_logged;
    static bool s_diag = false;
    if (!s_diag && context == QStringLiteral("OnboardingWizard")) {
        s_diag = true;
        QByteArray tc("OnboardingWizard");
        QByteArray ts("Welcome to Judecoin Wallet");
        QString r = s_standalone.translate(tc.constData(), ts.constData());
        qInfo().noquote() << "qtr DIAG: standalone.translate(OnboardingWizard, Welcome) =\""
                          << r << "\" isEmpty=" << r.isEmpty();
    }
    QString key = context + "/" + source;
    if (!s_logged.contains(key) && s_logged.size() < 20) {
        s_logged.insert(key);
        qInfo().noquote() << "qtr:" << context << "/" << source << "→\""
                          << result.left(30) << "\""
                          << (result == source ? "(untranslated)" : "(OK)");
    }
    return result;
}

bool I18nManager::isSupported(const QString& code) {
    if (isAutoCode(code)) {
        // "auto" is always "supported" - it's the sentinel for "no override".
        return true;
    }
    const auto langs = judecoin_wallet::core::i18n::kSupportedLanguages();
    for (const auto& lang : langs) {
        if (lang.code == code) {
            return true;
        }
    }
    return false;
}

bool I18nManager::isAutoCode(const QString& code) {
    return code.isEmpty() || code == QStringLiteral("auto");
}

QString I18nManager::resolveLoadCode(const QString& code) const {
    // zh_CN -> en (V5.4.9 fallback).
    if (code == QStringLiteral("zh_CN")) {
        return QStringLiteral("en");
    }
    // Anything else passes through; the caller's setCurrentLanguage()
    // already gated non-Auto codes against kSupportedLanguages().
    return code;
}

void I18nManager::setEngine(QQmlApplicationEngine* engine) {
    m_engine = engine;
}

void I18nManager::setTranslationsBasePath(const QString& path) {
    if (m_translationsBasePath == path) {
        return;
    }
    m_translationsBasePath = path;
    // Re-install with the current language so the new base takes effect.
    if (!isAutoCode(m_currentLanguage)) {
        setCurrentLanguage(m_currentLanguage);
    }
}

bool I18nManager::setCurrentLanguage(const QString& code) {
    qInfo().noquote() << "I18nManager::setCurrentLanguage called: code=" << code
                      << "current=" << m_currentLanguage;
    // Step 0: idempotency guard. If the caller is re-setting the same
    // language we already have, do nothing - the caller already saw
    // currentLanguageChanged() / translationChanged() from the prior
    // change. This is what makes
    //   setCurrentLanguage("de"); setCurrentLanguage("de");
    // a no-op for signal purposes.
    if (m_currentLanguage == code) {
        return true;
    }

    // Step 1: detach any previously installed translator.
    // We delete + recreate (instead of reusing) because Qt 6.6.3's
    // QTranslator can hold internal caches that survive load() calls,
    // causing retranslate() to skip already-translated contexts.
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    m_translator = new QTranslator(this);

    // Step 2: "auto" or "" -> leave nothing installed, system locale wins.
    if (isAutoCode(code)) {
        m_currentLanguage = code;
        emit currentLanguageChanged();
        emit translationChanged();
        emit restartRequested();
        return true;
    }

    // Step 3: validate the code is supported. zh_CN -> en (fallback).
    QString loadCode = resolveLoadCode(code);
    if (loadCode != code) {
        qCInfo(lcI18n) << "Language" << code
                       << "is not supported; falling back to" << loadCode;
    } else if (!isSupported(code)) {
        qCWarning(lcI18n) << "Language" << code << "is not supported";
        return false;
    }

    // Step 4: try to load the .qm from the Qt resource system.
    // We use QFile-based loading because QTranslator::load(QString)
    // with a ":/" resource prefix is unreliable across Qt versions —
    // QFileInfo-based path splitting inside the single-arg overload
    // can silently fail to open resource files. QFile + load(data, len)
    // is the documented robust path (Qt 6.2+ docs).
    //
    // Also try a plain on-disk check first so translators and tests
    // can drop fresh .qm files next to the binary without rebuilding.
    const QString qmFileName = QStringLiteral("judecoin_wallet_")
                             + loadCode
                             + QStringLiteral(".qm");

    bool loaded = false;

    // (a) On-disk path (for translator testing / dev).
    const QString qmDiskPath = m_translationsBasePath
                             + QStringLiteral("/")
                             + qmFileName;
    if (!m_translationsBasePath.startsWith(QStringLiteral(":/"))) {
        loaded = m_translator->load(qmDiskPath);
        if (loaded) {
            qCInfo(lcI18n) << "Loaded translation" << loadCode
                           << "from disk:" << qmDiskPath;
        }
    }

    // (b) Qt resource path (what ships in the AppImage).
    if (!loaded) {
        const QString qmResourcePath = QStringLiteral(":/i18n/") + qmFileName;
        QFile qmFile(qmResourcePath);
        if (qmFile.open(QIODevice::ReadOnly)) {
            const QByteArray qmData = qmFile.readAll();
            qmFile.close();
            loaded = m_translator->load(
                reinterpret_cast<const uchar*>(qmData.constData()),
                static_cast<int>(qmData.size()));
            if (loaded) {
                qCInfo(lcI18n) << "Loaded translation" << loadCode
                               << "from resource:" << qmResourcePath
                               << "(" << qmData.size() << "bytes)";
            }
        } else {
            qCWarning(lcI18n) << "Cannot open resource" << qmResourcePath
                              << ":" << qmFile.errorString();
        }
    }

    if (loaded) {
        QCoreApplication::installTranslator(m_translator);
        qCInfo(lcI18n) << "Translator installed for" << loadCode
                       << "| isEmpty:" << m_translator->isEmpty()
                       << "| file:" << qmFileName;
    } else {
        qCWarning(lcI18n) << "No translation file for" << loadCode
                          << "(checked disk:" << qmDiskPath
                          << "resource::/i18n/" << qmFileName
                          << "- using English source strings)";
    }

    m_currentLanguage = code;
    emit currentLanguageChanged();
    emit translationChanged();

    // Qt 6.6.3 QML runtime language switch is fundamentally broken.
    // After 3 rounds of trying retranslate() / setUiLanguage() /
    // clearComponentCache() / deferred execution / LanguageChange
    // events — none of them trigger qsTr() binding re-evaluation.
    //
    // The ONLY reliable approach: restart the app.
    // main.cpp handles restartRequested by spawning a new process
    // (QProcess::startDetached) with the same binary + args, then
    // quitting. The new process loads the translator before QML boots
    // → all pages show correct translations from the first frame.
    emit restartRequested();

    return true;
}

bool I18nManager::installForLocale(const QLocale& locale,
                                   const QString& translationsBasePath) {
    // Walk the preferred UI languages list and try the first supported one.
    // Falls back to "en" if the system locale is zh_CN.
    const QStringList candidates = locale.uiLanguages();
    QString chosen = QStringLiteral("auto");
    for (const QString& candidate : candidates) {
        QString normalized = candidate;
        normalized.replace('-', '_');
        if (normalized == QStringLiteral("zh_CN")) {
            // Per V5.4.9: zh_CN -> en.
            chosen = QStringLiteral("en");
            break;
        }
        if (isSupported(normalized)) {
            chosen = normalized;
            break;
        }
        // Try the language-only portion ("zh-Hant" -> "zh_TW", "de" -> "de").
        const int underscore = normalized.indexOf('_');
        if (underscore > 0) {
            const QString shortCode = normalized.left(underscore);
            if (isSupported(shortCode)) {
                chosen = shortCode;
                break;
            }
        }
    }

    if (!translationsBasePath.isEmpty()) {
        setTranslationsBasePath(translationsBasePath);
    }

    return setCurrentLanguage(chosen);
}
