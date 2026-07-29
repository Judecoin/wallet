// =============================================================================
// Judecoin Wallet - TxTag implementation (st-112)
//
// File-backed storage for per-tx tag strings. Mirrors TxNote (st-062) but
// stores the "structured tag" half of V5.4 §8 decision C - the user-facing
// tag string shown alongside the free-form note in TxDetailDialog.qml.
// =============================================================================

#include "core/localdata/TxTag.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include "core/util/SecureBuffer.h"
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace judecoin_wallet::core::localdata {

TxTag::TxTag(QObject* parent)
    : QObject(parent)
{
}

TxTag::~TxTag() = default;

QString TxTag::fileName()
{
    return QStringLiteral("tx_tags.json");
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool TxTag::isEmpty() const
{
    return m_tags.isEmpty();
}

int TxTag::size() const
{
    return m_tags.size();
}

QString TxTag::tagFor(const QString& txid) const
{
    return m_tags.value(txid); // returns empty string if missing
}

bool TxTag::hasTag(const QString& txid) const
{
    return m_tags.contains(txid);
}

QHash <QString, QString> TxTag::tags() const
{
    return m_tags;
}

QStringList TxTag::txids() const
{
    return m_tags.keys();
}

// -----------------------------------------------------------------------------
// Mutation
// -----------------------------------------------------------------------------

void TxTag::setTag(const QString& txid, const QString& tag)
{
    if (txid.isEmpty()) {
        return; // refuse to store tags under the empty-key
    }
    m_tags.insert(txid, tag);
    emit tagSet(txid);
}

bool TxTag::removeTag(const QString& txid)
{
    if (!m_tags.contains(txid)) {
        return false;
    }
    m_tags.remove(txid);
    emit tagRemoved(txid);
    return true;
}

// -----------------------------------------------------------------------------
// V5.4.5 / gf-004 memory scrubbing
//
// TxTag::secureClear() physically zeroes every tag text via
// util::secureClear() before releasing the QHash. We iterate the values
// (not keys) because the keys are non-sensitive txids (already public
// on-chain). Each tag's text is the user's free-form classification
// and may contain wallet addresses, amounts, or other context they
// would not expect to survive in freed heap pages after a
// Settings->Reset.
// -----------------------------------------------------------------------------

void TxTag::secureClear() noexcept
{
    if (m_tags.isEmpty()) {
        return;
    }
    using judecoin_wallet::core::util::secureClear;
    for (auto it = m_tags.begin(); it != m_tags.end(); ++it) {
        secureClear(it.value());
    }
    m_tags.clear();
    emit cleared();
}

void TxTag::clear()
{
    if (m_tags.isEmpty()) {
        return;
    }
    m_tags.clear();
    emit cleared();
}

// -----------------------------------------------------------------------------
// JSON snapshot
// -----------------------------------------------------------------------------

QJsonObject TxTag::toJson(bool withExportedMetadata) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);

    QJsonObject tagsObj;
    for (auto it = m_tags.constBegin(); it != m_tags.constEnd(); ++it) {
        tagsObj.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("tags"), tagsObj);

    if (withExportedMetadata) {
        root.insert(QStringLiteral("exported_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODate));
        root.insert(QStringLiteral("exported_by"),
                    QStringLiteral("JudecoinWallet v1.0.0"));
    }
    return root;
}

bool TxTag::fromJson(const QJsonObject& obj, QString* error)
{
    const QJsonValue v = obj.value(QStringLiteral("version"));
    if (!v.isUndefined() && v.toInt(-1) != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("unsupported tx_tags.json version: %1 (expected %2)")
                         .arg(v.toInt(-1))
                         .arg(kSchemaVersion);
        }
        return false;
    }

    m_tags.clear();

    const QJsonValue tagsVal = obj.value(QStringLiteral("tags"));
    if (!tagsVal.isObject()) {
        // No tags key (or it's wrong type) - treat as empty rather than
        // a hard error, since the file is otherwise well-formed.
        emit cleared();
        return true;
    }
    const QJsonObject tagsObj = tagsVal.toObject();
    for (auto it = tagsObj.constBegin(); it != tagsObj.constEnd(); ++it) {
        const QString txid = it.key();
        if (txid.isEmpty()) {
            continue;
        }
        m_tags.insert(txid, it.value().toString());
    }
    emit cleared();
    return true;
}

// -----------------------------------------------------------------------------
// File I/O
// -----------------------------------------------------------------------------

bool TxTag::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.exists()) {
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("cannot open %1 for reading: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("parse error in %1 at offset %2: %3")
                         .arg(path)
                         .arg(perr.offset)
                         .arg(perr.errorString());
        }
        return false;
    }
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("%1 is not a JSON object").arg(path);
        }
        return false;
    }
    return fromJson(doc.object(), error);
}

bool TxTag::save(const QString& path, QString* error) const
{
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot open %1 for writing: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    const QJsonDocument doc(toJson(false));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size()) {
        if (error) {
            *error = QStringLiteral("short write to %1: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    if (!f.commit()) {
        if (error) {
            *error = QStringLiteral("commit failed for %1: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    return true;
}

bool TxTag::exportFile(const QString& path, QString* error) const
{
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot open %1 for writing: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    const QJsonDocument doc(toJson(true));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size()) {
        if (error) {
            *error = QStringLiteral("short write to %1: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    if (!f.commit()) {
        if (error) {
            *error = QStringLiteral("commit failed for %1: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    return true;
}

bool TxTag::importFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.exists()) {
        if (error) {
            *error = QStringLiteral("import file does not exist: %1").arg(path);
        }
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("cannot open %1 for reading: %2")
                         .arg(path, f.errorString());
        }
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("parse error in %1 at offset %2: %3")
                         .arg(path)
                         .arg(perr.offset)
                         .arg(perr.errorString());
        }
        return false;
    }
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("%1 is not a JSON object").arg(path);
        }
        return false;
    }
    return fromJson(doc.object(), error);
}

} // namespace judecoin_wallet::core::localdata