// =============================================================================
// Judecoin Wallet - AccountTag implementation (st-017)
// =============================================================================

#include "core/localdata/AccountTag.h"

#include "core/util/SecureBuffer.h"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

namespace judecoin_wallet::core::localdata {

AccountTag::AccountTag(QObject* parent)
    : QObject(parent)
{
}

AccountTag::~AccountTag() = default;

QString AccountTag::fileName()
{
    return QStringLiteral("account_tags.json");
}

// -----------------------------------------------------------------------------
// Validation helpers
// -----------------------------------------------------------------------------

namespace {

// CSS hex color: "#RGB", "#RRGGBB", or "#RRGGBBAA" - the formats QColor
// understands. V1.0 keeps it simple; we do NOT support named colors
// ("red") or hsl/rgb().
bool isValidHexColor(const QString& c)
{
    static const QRegularExpression re(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    return re.match(c).hasMatch();
}

} // namespace

// -----------------------------------------------------------------------------
// V5.4.5 / gf-004 memory scrubbing
//
// AccountTagEntry::secure_clear() physically zeroes both QString fields
// so the tag name does not survive in freed heap pages after clearAll().
// The fields are scrubbed in declaration order; each scrubbed QChar is
// forced to 0 via a volatile-pointer write loop in util::secureClear()
// to defeat dead-store elimination.
// -----------------------------------------------------------------------------

void AccountTagEntry::secure_clear() noexcept
{
    using judecoin_wallet::core::util::secureClear;
    secureClear(tag);
    secureClear(color);
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool AccountTag::isEmpty() const
{
    return m_tags.isEmpty();
}

int AccountTag::size() const
{
    return m_tags.size();
}

QVector <AccountTagEntry> AccountTag::tags() const
{
    return m_tags;
}

AccountTagEntry AccountTag::tagById(qint64 id) const
{
    for (const auto& t : m_tags) {
        if (t.id == id) {
            return t;
        }
    }
    return AccountTagEntry{};
}

qint64 AccountTag::idForName(const QString& name) const
{
    if (name.isEmpty()) return -1;
    for (const auto& t : m_tags) {
        if (t.tag == name) return t.id;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Mutation
// -----------------------------------------------------------------------------

void AccountTag::recomputeNextId()
{
    qint64 maxId = 0;
    for (const auto& t : m_tags) {
        if (t.id > maxId) maxId = t.id;
    }
    m_nextId = maxId + 1;
}

qint64 AccountTag::addTag(const QString& name, const QString& color)
{
    if (name.trimmed().isEmpty()) return -1;
    if (!isValidHexColor(color)) return -1;
    AccountTagEntry t;
    t.id    = m_nextId++;
    t.tag   = name;
    t.color = color;
    m_tags.append(t);
    emit tagAdded(t.id);
    return t.id;
}

bool AccountTag::updateTag(qint64 id, const QString& name, const QString& color)
{
    for (auto& t : m_tags) {
        if (t.id == id) {
            if (!name.isEmpty() && !name.isNull()) t.tag = name;
            if (!color.isNull() && isValidHexColor(color)) t.color = color;
            emit tagUpdated(id);
            return true;
        }
    }
    return false;
}

bool AccountTag::removeTag(qint64 id)
{
    for (int i = 0; i < m_tags.size(); ++i) {
        if (m_tags[i].id == id) {
            m_tags.removeAt(i);
            emit tagRemoved(id);
            return true;
        }
    }
    return false;
}

void AccountTag::secureClear() noexcept
{
    using judecoin_wallet::core::util::secureClear;
    if (m_tags.isEmpty()) {
        return;
    }
    // Scrub every entry's QString fields while the entries are still
    // alive in m_tags, so the heap pages holding the tag names are
    // physically zeroed before the vector releases them.
    for (auto& t : m_tags) {
        t.secure_clear();
    }
    m_tags.clear();
    m_nextId = 1;
    emit cleared();
}

void AccountTag::clear()
{
    if (m_tags.isEmpty()) return;
    m_tags.clear();
    m_nextId = 1;
    emit cleared();
}

// -----------------------------------------------------------------------------
// JSON snapshot
// -----------------------------------------------------------------------------

QJsonObject AccountTag::toJson(bool withExportedMetadata) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);

    QJsonArray arr;
    for (const auto& t : m_tags) {
        QJsonObject o;
        o.insert(QStringLiteral("id"),    t.id);
        o.insert(QStringLiteral("tag"),   t.tag);
        o.insert(QStringLiteral("color"), t.color);
        arr.append(o);
    }
    root.insert(QStringLiteral("tags"), arr);

    if (withExportedMetadata) {
        root.insert(QStringLiteral("exported_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODate));
        root.insert(QStringLiteral("exported_by"),
                    QStringLiteral("JudecoinWallet v1.0.0"));
    }
    return root;
}

bool AccountTag::fromJson(const QJsonObject& obj, QString* error)
{
    const QJsonValue v = obj.value(QStringLiteral("version"));
    if (!v.isUndefined() && v.toInt(-1) != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("unsupported account_tags.json version: %1 (expected %2)")
                         .arg(v.toInt(-1))
                         .arg(kSchemaVersion);
        }
        return false;
    }

    m_tags.clear();
    m_nextId = 1;

    const QJsonArray arr = obj.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();
        AccountTagEntry t;
        t.id    = o.value(QStringLiteral("id")).toInt(0);
        t.tag   = o.value(QStringLiteral("tag")).toString();
        t.color = o.value(QStringLiteral("color")).toString();
        if (t.tag.isEmpty() || !isValidHexColor(t.color)) {
            continue; // skip malformed
        }
        m_tags.append(t);
    }
    recomputeNextId();
    emit cleared();
    return true;
}

// -----------------------------------------------------------------------------
// File I/O (same shape as AddressBook)
// -----------------------------------------------------------------------------

bool AccountTag::load(const QString& path, QString* error)
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

bool AccountTag::save(const QString& path, QString* error) const
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

bool AccountTag::exportFile(const QString& path, QString* error) const
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

bool AccountTag::importFile(const QString& path, QString* error)
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
