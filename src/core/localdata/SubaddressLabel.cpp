// =============================================================================
// Judecoin Wallet - SubaddressLabel implementation (st-017)
// =============================================================================

#include "core/localdata/SubaddressLabel.h"

#include "core/util/SecureBuffer.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringBuilder>
#include <QStringList>

namespace judecoin_wallet::core::localdata {

SubaddressLabel::SubaddressLabel(QObject* parent)
    : QObject(parent)
{
}

SubaddressLabel::~SubaddressLabel() = default;

QString SubaddressLabel::fileName()
{
    return QStringLiteral("subaddress_labels.json");
}

QString SubaddressLabel::makeKey(quint32 accountIndex, quint32 subaddressIndex)
{
    return QString::asprintf("%u:%u", (unsigned)(accountIndex), (unsigned)(subaddressIndex));
}

bool SubaddressLabel::parseKey(const QString& key, quint32* accountIndex, quint32* subaddressIndex)
{
    if (!accountIndex || !subaddressIndex) return false;
    const int colon = key.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == key.size() - 1) return false;
    bool ok1 = false, ok2 = false;
    const quint32 a = key.left(colon).toUInt(&ok1);
    const quint32 s = key.mid(colon + 1).toUInt(&ok2);
    if (!ok1 || !ok2) return false;
    *accountIndex    = a;
    *subaddressIndex = s;
    return true;
}

bool SubaddressLabel::isEmpty() const
{
    return m_labels.isEmpty();
}

int SubaddressLabel::size() const
{
    return m_labels.size();
}

QString SubaddressLabel::labelFor(quint32 accountIndex, quint32 subaddressIndex) const
{
    return m_labels.value(makeKey(accountIndex, subaddressIndex));
}

bool SubaddressLabel::hasLabel(quint32 accountIndex, quint32 subaddressIndex) const
{
    return m_labels.contains(makeKey(accountIndex, subaddressIndex));
}

QHash <QString, QString> SubaddressLabel::labels() const
{
    return m_labels;
}

void SubaddressLabel::setLabel(quint32 accountIndex, quint32 subaddressIndex, const QString& text)
{
    const QString key = makeKey(accountIndex, subaddressIndex);
    if (text.isEmpty()) {
        if (m_labels.contains(key)) {
            m_labels.remove(key);
            emit labelCleared(accountIndex, subaddressIndex);
        }
        return;
    }
    m_labels.insert(key, text);
    emit labelSet(accountIndex, subaddressIndex);
}

bool SubaddressLabel::clearLabel(quint32 accountIndex, quint32 subaddressIndex)
{
    const QString key = makeKey(accountIndex, subaddressIndex);
    if (!m_labels.contains(key)) return false;
    m_labels.remove(key);
    emit labelCleared(accountIndex, subaddressIndex);
    return true;
}
// -----------------------------------------------------------------------------
// V5.4.5 / gf-004 memory scrubbing
//
// SubaddressLabel::secureClear() physically zeroes every label value via
// util::secureClear() before releasing the QHash. We iterate values (not
// keys) because the keys are non-sensitive indices; the values are the
// user's free-form labels and may contain context they would not expect
// to survive in freed heap pages after a Settings->Reset.
// -----------------------------------------------------------------------------

void SubaddressLabel::secureClear() noexcept
{
    if (m_labels.isEmpty()) {
        return;
    }
    using judecoin_wallet::core::util::secureClear;
    for (auto it = m_labels.begin(); it != m_labels.end(); ++it) {
        secureClear(it.value());
    }
    m_labels.clear();
    emit cleared();
}


void SubaddressLabel::clear()
{
    if (m_labels.isEmpty()) return;
    m_labels.clear();
    emit cleared();
}

QJsonObject SubaddressLabel::toJson(bool withExportedMetadata) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);

    QJsonObject obj;
    for (auto it = m_labels.constBegin(); it != m_labels.constEnd(); ++it) {
        obj.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("labels"), obj);

    if (withExportedMetadata) {
        root.insert(QStringLiteral("exported_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODate));
        root.insert(QStringLiteral("exported_by"),
                    QStringLiteral("JudecoinWallet v1.0.0"));
    }
    return root;
}

bool SubaddressLabel::fromJson(const QJsonObject& obj, QString* error)
{
    const QJsonValue v = obj.value(QStringLiteral("version"));
    if (!v.isUndefined() && v.toInt(-1) != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("unsupported subaddress_labels.json version: %1 (expected %2)")
                         .arg(v.toInt(-1))
                         .arg(kSchemaVersion);
        }
        return false;
    }

    m_labels.clear();

    const QJsonValue labelsVal = obj.value(QStringLiteral("labels"));
    if (!labelsVal.isObject()) {
        emit cleared();
        return true;
    }
    const QJsonObject labelsObj = labelsVal.toObject();
    for (auto it = labelsObj.constBegin(); it != labelsObj.constEnd(); ++it) {
        quint32 a = 0, s = 0;
        if (!parseKey(it.key(), &a, &s)) continue;
        const QString text = it.value().toString();
        if (text.isEmpty()) continue;
        m_labels.insert(it.key(), text);
    }
    emit cleared();
    return true;
}

bool SubaddressLabel::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1 for reading: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("parse error in %1 at offset %2: %3").arg(path).arg(perr.offset).arg(perr.errorString());
        return false;
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("%1 is not a JSON object").arg(path);
        return false;
    }
    return fromJson(doc.object(), error);
}

bool SubaddressLabel::save(const QString& path, QString* error) const
{
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot open %1 for writing: %2").arg(path, f.errorString());
        return false;
    }
    const QJsonDocument doc(toJson(false));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("short write to %1: %2").arg(path, f.errorString());
        return false;
    }
    if (!f.commit()) {
        if (error) *error = QStringLiteral("commit failed for %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

bool SubaddressLabel::exportFile(const QString& path, QString* error) const
{
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot open %1 for writing: %2").arg(path, f.errorString());
        return false;
    }
    const QJsonDocument doc(toJson(true));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("short write to %1: %2").arg(path, f.errorString());
        return false;
    }
    if (!f.commit()) {
        if (error) *error = QStringLiteral("commit failed for %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

bool SubaddressLabel::importFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.exists()) {
        if (error) *error = QStringLiteral("import file does not exist: %1").arg(path);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1 for reading: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("parse error in %1 at offset %2: %3").arg(path).arg(perr.offset).arg(perr.errorString());
        return false;
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("%1 is not a JSON object").arg(path);
        return false;
    }
    return fromJson(doc.object(), error);
}

} // namespace judecoin_wallet::core::localdata
