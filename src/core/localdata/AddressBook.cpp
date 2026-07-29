// =============================================================================
// Judecoin Wallet - AddressBook implementation (st-017)
//
// All file I/O is synchronous and cheap (file is < 50 KB even with 1000
// entries; QJsonDocument round-trip is sub-millisecond on V1.0 hardware).
// We do NOT wrap reads/writes in QFuture - matching SettingsManager's
// synchronous-only rule from st-014.
// =============================================================================

#include "core/localdata/AddressBook.h"

#include "core/util/SecureBuffer.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringBuilder>

namespace judecoin_wallet::core::localdata {

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

AddressBook::AddressBook(QObject* parent)
    : QObject(parent)
{
}

AddressBook::~AddressBook() = default;

QString AddressBook::fileName()
{
    return QStringLiteral("address_book.json");
}

// -----------------------------------------------------------------------------
// Timestamp helper
//
// Matches docs/UI_DETAILS_TODO.md section 6 example format:
//   "2026-07-02 12:00"
// Local time (not UTC) because the spec example is local and the
// address book is a UI convenience, not a cross-timezone audit trail.
// -----------------------------------------------------------------------------
QString AddressBook::nowLocalTimestamp() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// -----------------------------------------------------------------------------
// V5.4.5 / gf-004 memory scrubbing
//
// AddressBookEntry::secure_clear() physically zeroes every QString field
// so the entry text does not survive in freed heap pages after the struct
// goes out of scope. The fields are scrubbed in declaration order; each
// scrubbed QChar is forced to 0 via a volatile-pointer write loop in
// util::secureClear() to defeat dead-store elimination.
// -----------------------------------------------------------------------------

void AddressBookEntry::secure_clear() noexcept
{
    using judecoin_wallet::core::util::secureClear;
    secureClear(address);
    secureClear(label);
    secureClear(description);
    secureClear(paymentId);
    secureClear(createdAt);
    secureClear(updatedAt);
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool AddressBook::isEmpty() const
{
    return m_entries.isEmpty();
}

int AddressBook::size() const
{
    return m_entries.size();
}

QVector <AddressBookEntry> AddressBook::entries() const
{
    return m_entries;
}

AddressBookEntry AddressBook::entryById(qint64 id) const
{
    for (const auto& e : m_entries) {
        if (e.id == id) {
            return e;
        }
    }
    return AddressBookEntry{};
}

qint64 AddressBook::idForAddress(const QString& address) const
{
    if (address.isEmpty()) {
        return -1;
    }
    for (const auto& e : m_entries) {
        if (e.address == address) {
            return e.id;
        }
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Mutation
// -----------------------------------------------------------------------------

void AddressBook::recomputeNextId()
{
    qint64 maxId = 0;
    for (const auto& e : m_entries) {
        if (e.id > maxId) {
            maxId = e.id;
        }
    }
    m_nextId = maxId + 1;
}

qint64 AddressBook::addEntry(const QString& address,
                             const QString& label,
                             const QString& description,
                             const QString& paymentId)
{
    if (address.trimmed().isEmpty()) {
        return -1; // address is mandatory
    }

    // Dedupe by address: if the user re-adds an address already in the
    // book, return the existing id and update the user-facing fields
    // (label / description / payment_id) that are explicitly provided.
    // Matches docs/UI_DETAILS_TODO.md section 6 "monotonic id, never
    // reused" - dedup keeps the address book free of duplicate rows.
    for (auto& e : m_entries) {
        if (e.address == address) {
            if (!label.isNull())       e.label       = label;
            if (!description.isNull()) e.description = description;
            if (!paymentId.isNull())   e.paymentId   = paymentId;
            e.updatedAt = nowLocalTimestamp();
            emit entryUpdated(e.id);
            return e.id;
        }
    }

    AddressBookEntry e;
    e.id          = m_nextId++;
    e.address     = address;
    e.label       = label;
    e.description = description;
    e.paymentId   = paymentId;
    const QString ts = nowLocalTimestamp();
    e.createdAt   = ts;
    e.updatedAt   = ts;
    m_entries.append(e);
    emit entryAdded(e.id);
    return e.id;
}

bool AddressBook::updateEntry(qint64 id,
                              const QString& label,
                              const QString& description,
                              const QString& paymentId)
{
    for (auto& e : m_entries) {
        if (e.id == id) {
            if (!label.isNull())       e.label       = label;
            if (!description.isNull()) e.description = description;
            if (!paymentId.isNull())   e.paymentId   = paymentId;
            e.updatedAt = nowLocalTimestamp();
            emit entryUpdated(id);
            return true;
        }
    }
    return false;
}

bool AddressBook::removeEntry(qint64 id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            m_entries.removeAt(i);
            // Note: m_nextId is NOT decremented. IDs are monotonic and
            // never reused - matches V5.4.4 section 6 example which uses
            // hand-assigned ascending ids.
            emit entryRemoved(id);
            return true;
        }
    }
    return false;
}

void AddressBook::secureClear() noexcept
{
    using judecoin_wallet::core::util::secureClear;
    if (m_entries.isEmpty()) {
        return;
    }
    // Scrub every entry text fields while the entries are still alive in
    // m_entries, so the heap pages holding the sensitive strings are
    // physically zeroed before the vector releases them. We detach each
    // QString via secureClear explicit detach step which guarantees we own
    // the buffer exclusively.
    for (auto& e : m_entries) {
        e.secure_clear();
    }
    m_entries.clear();
    m_nextId = 1;
    emit cleared();
}

void AddressBook::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.clear();
    m_nextId = 1;
    emit cleared();
}

// -----------------------------------------------------------------------------
// JSON snapshot
//
// toJson() / fromJson() are the V1.0 file format documented in
// docs/UI_DETAILS_TODO.md section 6. The schema has a top-level "version"
// key (currently 1) and an "entries" array. Optional metadata
// (exported_at / exported_by) is only emitted by exportFile(), not by
// save() - the on-disk file should be the canonical working copy, while
// exports are clearly stamped.
// -----------------------------------------------------------------------------

QJsonObject AddressBook::toJson(bool withExportedMetadata) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);

    QJsonArray arr;
    for (const auto& e : m_entries) {
        QJsonObject o;
        o.insert(QStringLiteral("id"),          e.id);
        o.insert(QStringLiteral("address"),     e.address);
        o.insert(QStringLiteral("label"),       e.label);
        o.insert(QStringLiteral("description"), e.description);
        o.insert(QStringLiteral("payment_id"),  e.paymentId);
        o.insert(QStringLiteral("created_at"),  e.createdAt);
        o.insert(QStringLiteral("updated_at"),  e.updatedAt);
        arr.append(o);
    }
    root.insert(QStringLiteral("entries"), arr);

    if (withExportedMetadata) {
        root.insert(QStringLiteral("exported_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODate));
        root.insert(QStringLiteral("exported_by"),
                    QStringLiteral("JudecoinWallet v1.0.0"));
    }
    return root;
}

bool AddressBook::fromJson(const QJsonObject& obj, QString* error)
{
    // Forward-tolerate missing version: V1.1+ may introduce a
    // "schema_version" field. V1.0 readers only check version == 1.
    const QJsonValue v = obj.value(QStringLiteral("version"));
    if (!v.isUndefined() && v.toInt(-1) != kSchemaVersion) {
        if (error) {
            *error = QStringLiteral("unsupported address_book.json version: %1 (expected %2)")
                         .arg(v.toInt(-1))
                         .arg(kSchemaVersion);
        }
        return false;
    }

    m_entries.clear();
    m_nextId = 1;

    const QJsonArray arr = obj.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& val : arr) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject o = val.toObject();
        AddressBookEntry e;
        e.id          = o.value(QStringLiteral("id")).toInt(0);
        e.address     = o.value(QStringLiteral("address")).toString();
        e.label       = o.value(QStringLiteral("label")).toString();
        e.description = o.value(QStringLiteral("description")).toString();
        e.paymentId   = o.value(QStringLiteral("payment_id")).toString();
        e.createdAt   = o.value(QStringLiteral("created_at")).toString();
        e.updatedAt   = o.value(QStringLiteral("updated_at")).toString();
        if (e.address.isEmpty()) {
            continue; // skip malformed entries
        }
        m_entries.append(e);
    }
    recomputeNextId();
    emit cleared(); // bulk-replace notification
    return true;
}

// -----------------------------------------------------------------------------
// File I/O
//
// load() of a missing file is NOT an error - it just leaves the in-memory
// list empty. That matches the first-run UX: the user opens the app for
// the first time, there is no address_book.json yet, and the UI shows an
// empty list without any "failed to load" noise.
// -----------------------------------------------------------------------------

bool AddressBook::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.exists()) {
        return true; // first run, nothing to load
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

bool AddressBook::save(const QString& path, QString* error) const
{
    // Ensure parent directory exists. LocalDataManager usually calls us
    // with an already-existing dir, but we tolerate the "first save" case
    // where ~/.config/JudecoinWallet/ has not been created yet.
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

// -----------------------------------------------------------------------------
// Import / Export
//
// exportFile() adds the optional "exported_at" / "exported_by" metadata
// (matches docs/UI_DETAILS_TODO.md section 6 example). importFile() is
// identical to load() except it requires the file to exist (import is a
// user-initiated action; missing file IS an error there).
// -----------------------------------------------------------------------------

bool AddressBook::exportFile(const QString& path, QString* error) const
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

bool AddressBook::importFile(const QString& path, QString* error)
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
