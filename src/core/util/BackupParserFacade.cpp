// =============================================================================
// Judecoin Wallet - BackupParserFacade implementation (st-022)
// =============================================================================

#include "core/util/BackupParserFacade.h"
#include "core/util/BackupParserTokenStore.h"
#include "core/util/SecureBuffer.h"

#include <QByteArray>
#include <QFile>
#include <QUrl>
#include <QVariantList>

namespace judecoin_wallet::core::util {

using judecoin_wallet::core::util::secureClear;

QVariantMap BackupParserFacade::parseBackupFile(const QString& path) const
{
    QUrl url(path);
    const QString filePath = url.isLocalFile() ? url.toLocalFile() : path;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QVariantMap m;
        m.insert(QStringLiteral("ok"), false);
        m.insert(QStringLiteral("reason"),
                 QStringLiteral("Could not read file (empty or permission denied)."));
        m.insert(QStringLiteral("tokenId"), -1);
        return m;
    }

    QByteArray rawBytes = f.readAll();
    f.close();

    QString payload = QString::fromUtf8(rawBytes);

    const ParseResult r = BackupParser::parse(payload);

    // The raw file content and its UTF-8 form are no longer needed now that
    // we have parsed it. Scrub both so the complete backup JSON (which
    // contains the keys inline) does not linger as C++ strings either.
    secureClear(rawBytes);
    secureClear(payload);

    QVariantMap m;
    m.insert(QStringLiteral("ok"), r.ok);
    m.insert(QStringLiteral("reason"), r.reason);
    m.insert(QStringLiteral("format"), static_cast<int>(r.format));
    m.insert(QStringLiteral("address"), r.address);
    m.insert(QStringLiteral("restoreHeight"),
             static_cast<qulonglong>(r.restoreHeight));
    m.insert(QStringLiteral("date"), r.date);

    if (r.ok) {
        const int tokenId = BackupParserTokenStore::instance().store(r);
        m.insert(QStringLiteral("tokenId"), tokenId);
    } else {
        m.insert(QStringLiteral("tokenId"), -1);
    }
    return m;
}

bool BackupParserFacade::releaseToken(int tokenId) const
{
    return BackupParserTokenStore::instance().release(tokenId);
}

QVariantMap BackupParserFacade::parse(const QString& payload) const
{
    const ParseResult r = BackupParser::parse(payload);
    QVariantMap m;
    m.insert(QStringLiteral("ok"), r.ok);
    m.insert(QStringLiteral("format"), static_cast<int>(r.format));
    m.insert(QStringLiteral("reason"), r.reason);
    m.insert(QStringLiteral("address"), r.address);
    m.insert(QStringLiteral("spendkey"), r.spendkey);
    m.insert(QStringLiteral("viewkey"), r.viewkey);
    m.insert(QStringLiteral("restoreHeight"),
             static_cast<qulonglong>(r.restoreHeight));
    m.insert(QStringLiteral("date"), r.date);
    // mnemonicWords as QVariantList of QStrings (QML side: string list)
    QVariantList words;
    for (const QString& w : r.mnemonicWords) {
        words.append(w);
    }
    m.insert(QStringLiteral("mnemonicWords"), words);
    return m;
}

QString BackupParserFacade::readFile(const QString& path) const
{
    // Resolve file:// URL to a native path.
    QUrl url(path);
    QString filePath = url.isLocalFile() ? url.toLocalFile() : path;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    return content;
}

bool BackupParserFacade::writeFile(const QString& path, const QString& content) const
{
    QUrl url(path);
    QString filePath = url.isLocalFile() ? url.toLocalFile() : path;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    qint64 written = f.write(content.toUtf8());
    f.close();
    return written == content.toUtf8().size();
}

QString BackupParserFacade::summary(const QVariantMap& result) const
{
    // Re-hydrate a ParseResult from the QVariantMap shape. We don't need
    // every field populated - summary() only reads ok + reason + format.
    ParseResult r;
    r.ok = result.value(QStringLiteral("ok"), false).toBool();
    r.reason = result.value(QStringLiteral("reason")).toString();
    r.format = static_cast<BackupFormat>(
        result.value(QStringLiteral("format"), 0).toInt());
    return BackupParser::summary(r);
}

} // namespace judecoin_wallet::core::util
