// =============================================================================
// Judecoin Wallet - TxNote implementation (st-017)
// =============================================================================

#include "core/localdata/TxNote.h"

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

TxNote::TxNote(QObject* parent)
    : QObject(parent)
{
}

TxNote::~TxNote() = default;

QString TxNote::fileName()
{
    return QStringLiteral("tx_notes.json");
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool TxNote::isEmpty() const
{
    return m_notes.isEmpty();
}

int TxNote::size() const
{
    return m_notes.size();
}

QString TxNote::noteFor(const QString& txid) const
{
    return m_notes.value(txid); // returns empty string if missing
}

bool TxNote::hasNote(const QString& txid) const
{
    return m_notes.contains(txid);
}

QHash <QString, QString> TxNote::notes() const
{
    return m_notes;
}

QStringList TxNote::txids() const
{
    return m_notes.keys();
}

// -----------------------------------------------------------------------------
// Mutation
// -----------------------------------------------------------------------------

void TxNote::setNote(const QString& txid, const QString& text)
{
    if (txid.isEmpty()) {
        return; // refuse to store notes under the empty-key
    }
    const bool wasPresent = m_notes.contains(txid);
    m_notes.insert(txid, text);
    Q_UNUSED(wasPresent);
    emit noteSet(txid);
}

bool TxNote::removeNote(const QString& txid)
{
    if (!m_notes.contains(txid)) {
        return false;
    }
    m_notes.remove(txid);
    emit noteRemoved(txid);
    return true;
}

// -----------------------------------------------------------------------------
// V5.4.5 / gf-004 memory scrubbing
//
// TxNote::secureClear() physically zeroes every note text via
// util::secureClear() before releasing the QHash. We iterate the values
// (not keys) because the keys are non-sensitive txids (already public
// on-chain). Each note's text is the user's free-form annotation and
// may contain wallet addresses, amounts, or other context they would
// not expect to survive in freed heap pages after a Settings->Reset.
// -----------------------------------------------------------------------------

void TxNote::secureClear() noexcept
{
    if (m_notes.isEmpty()) {
        return;
    }
    using judecoin_wallet::core::util::secureClear;
    for (auto it = m_notes.begin(); it != m_notes.end(); ++it) {
        secureClear(it.value());
    }
    m_notes.clear();
    emit cleared();
}

void TxNote::clear()
{
    if (m_notes.isEmpty()) {
        return;
    }
    m_notes.clear();
    emit cleared();
}

// -----------------------------------------------------------------------------
// JSON snapshot
// -----------------------------------------------------------------------------

QJsonObject TxNote::toJson(bool withExportedMetadata) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);

    QJsonObject notesObj;
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        notesObj.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("notes"), notesObj);

    if (withExportedMetadata) {
        root.insert(QStringLiteral("exported_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODate));
        root.insert(QStringLiteral("exported_by"),
                    QStringLiteral("JudecoinWallet v1.0.0"));
    }
    return root;
}

bool TxNote::fromJson(const QJsonObject& obj, QString* error)
{
    const QJsonValue v = obj.value(QStringLiteral("version"));
    if (!v.isUndefined() && v.toInt(-1) != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("unsupported tx_notes.json version: %1 (expected %2)")
                         .arg(v.toInt(-1))
                         .arg(kSchemaVersion);
        }
        return false;
    }

    m_notes.clear();

    const QJsonValue notesVal = obj.value(QStringLiteral("notes"));
    if (!notesVal.isObject()) {
        // No notes key (or it's wrong type) - treat as empty rather than
        // a hard error, since the file is otherwise well-formed.
        emit cleared();
        return true;
    }
    const QJsonObject notesObj = notesVal.toObject();
    for (auto it = notesObj.constBegin(); it != notesObj.constEnd(); ++it) {
        const QString txid = it.key();
        if (txid.isEmpty()) {
            continue;
        }
        m_notes.insert(txid, it.value().toString());
    }
    emit cleared();
    return true;
}

// -----------------------------------------------------------------------------
// File I/O
// -----------------------------------------------------------------------------

bool TxNote::load(const QString& path, QString* error)
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

bool TxNote::save(const QString& path, QString* error) const
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

bool TxNote::exportFile(const QString& path, QString* error) const
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

bool TxNote::importFile(const QString& path, QString* error)
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
