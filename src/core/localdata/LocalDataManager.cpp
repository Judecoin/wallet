// =============================================================================
// Judecoin Wallet - LocalDataManager implementation (st-017)
// =============================================================================

#include "core/localdata/LocalDataManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantMap>

namespace judecoin_wallet::core::localdata {

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

LocalDataManager::LocalDataManager(const QString& baseDir, QObject* parent)
    : QObject(parent)
    , m_baseDirOverride(baseDir)
    , m_accountTags(std::make_unique <AccountTag>())
    , m_addressBook(std::make_unique <AddressBook>())
    , m_subaddressLabels(std::make_unique <SubaddressLabel>())
    , m_txNotes(std::make_unique <TxNote>())
    , m_txTags(std::make_unique <TxTag>())
{
    // Wire child signals so QML listening on LocalDataManager hears
    // everything. We do NOT propagate entryAdded/entryUpdated/etc.
    // individually; the catch-all *Changed signals suffice for the
    // QML side (which re-reads the relevant lists on each pulse).
    connect(m_addressBook.get(), &AddressBook::entryAdded,
            this, &LocalDataManager::addressBookChanged);
    connect(m_addressBook.get(), &AddressBook::entryUpdated,
            this, &LocalDataManager::addressBookChanged);
    connect(m_addressBook.get(), &AddressBook::entryRemoved,
            this, &LocalDataManager::addressBookChanged);
    connect(m_addressBook.get(), &AddressBook::cleared,
            this, &LocalDataManager::addressBookChanged);

    connect(m_txNotes.get(), &TxNote::noteSet,
            this, &LocalDataManager::txNotesChanged);
    connect(m_txNotes.get(), &TxNote::noteRemoved,
            this, &LocalDataManager::txNotesChanged);
    connect(m_txNotes.get(), &TxNote::cleared,
            this, &LocalDataManager::txNotesChanged);

    connect(m_accountTags.get(), &AccountTag::tagAdded,
            this, &LocalDataManager::accountTagsChanged);
    connect(m_accountTags.get(), &AccountTag::tagUpdated,
            this, &LocalDataManager::accountTagsChanged);
    connect(m_accountTags.get(), &AccountTag::tagRemoved,
            this, &LocalDataManager::accountTagsChanged);
    connect(m_accountTags.get(), &AccountTag::cleared,
            this, &LocalDataManager::accountTagsChanged);

    // Signal relay: forward child-store signals for QML listeners.
    connect(m_subaddressLabels.get(), &SubaddressLabel::labelSet,
            this, &LocalDataManager::subaddressLabelsChanged);
    connect(m_subaddressLabels.get(), &SubaddressLabel::labelCleared,
            this, &LocalDataManager::subaddressLabelsChanged);
    connect(m_subaddressLabels.get(), &SubaddressLabel::cleared,
            this, &LocalDataManager::subaddressLabelsChanged);
    // FIX-G26 (2026-07-18): auto-save subaddress labels on every change.
    // SubaddressLabel::setLabel() only updates in-memory m_labels;
    // without this, labels (and therefore subaddress indices) are lost
    // after app restart, causing refresh() to find zero subaddresses.
    connect(m_subaddressLabels.get(), &SubaddressLabel::labelSet,
            this, [this]() { saveSubaddressLabels(); });
    connect(m_subaddressLabels.get(), &SubaddressLabel::labelCleared,
            this, [this]() { saveSubaddressLabels(); });
    connect(m_subaddressLabels.get(), &SubaddressLabel::cleared,
            this, [this]() { saveSubaddressLabels(); });

    // st-112: wire TxTag's per-tx set/remove signals into the manager's
    // txTagsChanged() relay so QML can listen to a single object for any
    // of the five data sources. cleared() (from secureClear / clear)
    // also forwards so a Settings -> Reset shows up as a single change.
    connect(m_txTags.get(), &TxTag::tagSet,
            this, &LocalDataManager::txTagsChanged);
    connect(m_txTags.get(), &TxTag::tagRemoved,
            this, &LocalDataManager::txTagsChanged);
    connect(m_txTags.get(), &TxTag::cleared,
            this, &LocalDataManager::txTagsChanged);
}

LocalDataManager::~LocalDataManager() = default;

QString LocalDataManager::resolveBaseDir() const
{
    if (!m_baseDirOverride.isEmpty()) {
        return m_baseDirOverride;
    }
    if (!m_cachedBaseDir.isEmpty()) {
        return m_cachedBaseDir;
    }
    m_cachedBaseDir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    if (m_cachedBaseDir.isEmpty()) {
        // Fallback: AppConfigLocation is empty in some sandboxes; fall
        // back to the home directory. This shouldn't happen on a real
        // Linux desktop.
        m_cachedBaseDir = QDir::homePath() + QStringLiteral("/.judecoin-wallet");
    }
    return m_cachedBaseDir;
}

QString LocalDataManager::baseDir() const
{
    return resolveBaseDir();
}

QString LocalDataManager::filePath(const QString& fileName) const
{
    return resolveBaseDir() + QLatin1Char('/') + fileName;
}

// -----------------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------------

AddressBook& LocalDataManager::addressBook() { return *m_addressBook; }
TxNote& LocalDataManager::txNotes() { return *m_txNotes; }
AccountTag& LocalDataManager::accountTags() { return *m_accountTags; }
SubaddressLabel& LocalDataManager::subaddressLabels() { return *m_subaddressLabels; }
TxTag& LocalDataManager::txTags() { return *m_txTags; }

const AddressBook& LocalDataManager::addressBook() const { return *m_addressBook; }
const TxNote& LocalDataManager::txNotes() const { return *m_txNotes; }
const AccountTag& LocalDataManager::accountTags() const { return *m_accountTags; }
const SubaddressLabel& LocalDataManager::subaddressLabels() const { return *m_subaddressLabels; }
const TxTag& LocalDataManager::txTags() const { return *m_txTags; }

// -----------------------------------------------------------------------------
// Bulk I/O
// -----------------------------------------------------------------------------

bool LocalDataManager::loadAddressBook(QString* error)
{
    return m_addressBook->load(filePath(AddressBook::fileName()), error);
}

bool LocalDataManager::loadTxNotes(QString* error)
{
    return m_txNotes->load(filePath(TxNote::fileName()), error);
}

bool LocalDataManager::loadAccountTags(QString* error)
{
    return m_accountTags->load(filePath(AccountTag::fileName()), error);
}

bool LocalDataManager::loadSubaddressLabels(QString* error)
{
    return m_subaddressLabels->load(filePath(SubaddressLabel::fileName()), error);
}

bool LocalDataManager::loadTxTags(QString* error)
{
    return m_txTags->load(filePath(TxTag::fileName()), error);
}

bool LocalDataManager::saveAddressBook(QString* error)
{
    return m_addressBook->save(filePath(AddressBook::fileName()), error);
}

bool LocalDataManager::saveTxNotes(QString* error)
{
    return m_txNotes->save(filePath(TxNote::fileName()), error);
}

bool LocalDataManager::saveAccountTags(QString* error)
{
    return m_accountTags->save(filePath(AccountTag::fileName()), error);
}

bool LocalDataManager::saveSubaddressLabels(QString* error)
{
    return m_subaddressLabels->save(filePath(SubaddressLabel::fileName()), error);
}

bool LocalDataManager::saveTxTags(QString* error)
{
    return m_txTags->save(filePath(TxTag::fileName()), error);
}

bool LocalDataManager::loadAll(QString* error)
{
    if (!loadAddressBook(error))      return false;
    if (!loadTxNotes(error))          return false;
    if (!loadAccountTags(error))      return false;
    if (!loadSubaddressLabels(error)) return false;
    if (!loadTxTags(error))           return false;
    emit loaded();
    return true;
}

bool LocalDataManager::saveAll(QString* error)
{
    if (!saveAddressBook(error))      return false;
    if (!saveTxNotes(error))          return false;
    if (!saveAccountTags(error))      return false;
    if (!saveSubaddressLabels(error)) return false;
    if (!saveTxTags(error))           return false;
    emit saved();
    return true;
}

bool LocalDataManager::clearAll(QString* error)
{
    // V5.4.5 / gf-004 memory scrubbing:
    // Use secureClear() on each child store so every sensitive QString
    // field (addresses, payment_ids, labels, note text, subaddress
    // labels, tag names, tx tag text) is physically zeroed in heap pages
    // BEFORE the vector / hash releases those buffers. The legacy
    // clear() would leave the plaintext content in freed heap pages that
    // a forensic dump could surface.
    //
    // In-memory clear first (so signals fire while the QObject tree
    // is still coherent), then delete files on disk.
    m_addressBook->secureClear();
    m_txNotes->secureClear();
    m_accountTags->secureClear();
    m_subaddressLabels->secureClear();
    m_txTags->secureClear();

    const QStringList fileNames = {
        AddressBook::fileName(),
        TxNote::fileName(),
        AccountTag::fileName(),
        SubaddressLabel::fileName(),
        TxTag::fileName(),
    };
    for (const QString& name : fileNames) {
        const QString path = filePath(name);
        QFile f(path);
        if (f.exists() && !f.remove()) {
            if (error) {
                *error = QStringLiteral("failed to remove %1: %2")
                             .arg(path, f.errorString());
            }
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// QML bridge (st-062)
// -----------------------------------------------------------------------------
// Thin wrappers around the TxNote facade so TxDetailDialog.qml can call
// localDataManagerObj.saveTxNote(hash, note) / .getTxNote(hash) /
// .removeTxNote(hash) without ever seeing the file I/O. Each save also
// writes the JSON file so the change survives an app restart.
// -----------------------------------------------------------------------------

bool LocalDataManager::saveTxNote(const QString& tx_hash, const QString& note)
{
    if (tx_hash.isEmpty()) {
        return false;
    }
    m_txNotes->setNote(tx_hash, note);
    QString error;
    const bool ok = saveTxNotes(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-062: LocalDataManager::saveTxNote failed to persist"
            << "tx_hash=" << tx_hash
            << "error=" << error;
    } else {
        qInfo().noquote()
            << "st-062: LocalDataManager::saveTxNote OK"
            << "tx_hash=" << tx_hash
            << "len=" << note.size();
    }
    return ok;
}

QString LocalDataManager::getTxNote(const QString& tx_hash) const
{
    if (tx_hash.isEmpty()) {
        return QString();
    }
    return m_txNotes->noteFor(tx_hash);
}

bool LocalDataManager::removeTxNote(const QString& tx_hash)
{
    if (tx_hash.isEmpty()) {
        return false;
    }
    const bool removed = m_txNotes->removeNote(tx_hash);
    if (!removed) {
        // No-op removal (the hash wasn't tracked). Still save the
        // (unchanged) file so any concurrent edits are durable.
        QString error;
        saveTxNotes(&error);
        return true;
    }
    QString error;
    const bool ok = saveTxNotes(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-062: LocalDataManager::removeTxNote failed to persist"
            << "tx_hash=" << tx_hash
            << "error=" << error;
    }
    return ok;
}


// -----------------------------------------------------------------------------
// QML bridge (st-112) - per-tx structured tag
// -----------------------------------------------------------------------------
// Thin wrappers around the TxTag facade so TxDetailDialog.qml can call
// accountTagObj.setTag(hash, tag) / .getTag(hash) / .removeTag(hash)
// without ever seeing the file I/O. Each save also writes tx_tags.json
// so the change survives an app restart.
//
// Spec mapping (V5.4 §8 decision C, st-112 modify contract):
//   - accountTagObj.setTag(hash, tag)   -> LocalDataManager::setTag()
//   - accountTagObj.getTag(hash)        -> LocalDataManager::getTag()
//   - accountTagObj.removeTag(hash)     -> LocalDataManager::removeTag()
//
// `accountTagObj` is set up in main.cpp as an alias for
// &localDataManager so the existing `localDataManagerObj` consumers and
// the new st-112 consumers share a single source of truth on the C++
// side. (Both context properties point to the same LocalDataManager
// instance; that is allowed because setContextProperty just stores a
// QVariant copy of the QObject* and the QObject itself is shared.)
//
// Mirror of the st-062 TxNote bridge above - same save-on-write
// contract, same removeIdempotent-on-missing pattern, same qInfo()/
// qWarning() structured logging so a real-test harness can grep for
// the st-112 marker.
//
// V5.4.7 #7B compliance: tx_hash is public on-chain data; the tag
// string is user-supplied free text (might contain wallet addresses,
// amounts, or other context). Neither is a secret - we never store
// keys/seeds/passwords in this surface.
// -----------------------------------------------------------------------------

bool LocalDataManager::setTag(const QString& tx_hash, const QString& tag)
{
    if (tx_hash.isEmpty()) {
        return false;
    }
    m_txTags->setTag(tx_hash, tag);
    QString error;
    const bool ok = saveTxTags(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-112: LocalDataManager::setTag failed to persist"
            << "tx_hash=" << tx_hash
            << "error=" << error;
    } else {
        qInfo().noquote()
            << "st-112: LocalDataManager::setTag OK"
            << "tx_hash=" << tx_hash
            << "len=" << tag.size();
    }
    return ok;
}

QString LocalDataManager::getTag(const QString& tx_hash) const
{
    if (tx_hash.isEmpty()) {
        return QString();
    }
    return m_txTags->tagFor(tx_hash);
}

bool LocalDataManager::removeTag(const QString& tx_hash)
{
    if (tx_hash.isEmpty()) {
        return false;
    }
    const bool removed = m_txTags->removeTag(tx_hash);
    if (!removed) {
        // No-op removal (the hash wasn't tracked). Still save the
        // (unchanged) file so any concurrent edits are durable.
        QString error;
        saveTxTags(&error);
        return true;
    }
    QString error;
    const bool ok = saveTxTags(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-112: LocalDataManager::removeTag failed to persist"
            << "tx_hash=" << tx_hash
            << "error=" << error;
    }
    return ok;
}


// -----------------------------------------------------------------------------
// AddressBook Q_INVOKABLE bridge (st-063)
// -----------------------------------------------------------------------------
// These 7 methods are the QML-facing surface for the address book. They
// are thin wrappers around the underlying localdata::AddressBook store
// (st-017) so that:
//   * the QML side never has to know about persistence paths,
//   * all reads return QVariantMap/QVariantList so ListView delegates
//     can `modelData.field` straight off the row without a C++ cast,
//   * every write auto-saves the JSON file (so a kill -9 of the UI
//     process right after a successful add/update/remove/import is
//     still safe; on next launch the in-memory list reloads from
//     disk).
//
// V5.4.7 #7B compliance: the address book holds public-chain
// addresses + user-supplied labels / descriptions / payment_ids.
// Nothing in this bridge touches keys, seeds, or passwords.
// -----------------------------------------------------------------------------

QVariantList LocalDataManager::addressBookEntries() const
{
    QVariantList out;
    const auto entries = m_addressBook->entries();
    out.reserve(entries.size());
    for (const auto& e : entries) {
        QVariantMap m;
        m.insert("id",          QVariant::fromValue <qint64>(e.id));
        m.insert("address",     e.address);
        m.insert("label",       e.label);
        m.insert("description", e.description);
        m.insert("paymentId",   e.paymentId);
        m.insert("createdAt",   e.createdAt);
        m.insert("updatedAt",   e.updatedAt);
        out.append(m);
    }
    return out;
}

int LocalDataManager::addressBookSize() const
{
    return m_addressBook->size();
}

qint64 LocalDataManager::addAddressEntry(const QString& address,
                                         const QString& label,
                                         const QString& description,
                                         const QString& paymentId)
{
    if (address.trimmed().isEmpty()) {
        qWarning().noquote()
            << "st-063: LocalDataManager::addAddressEntry rejected: empty address";
        return -1;
    }
    const qint64 id = m_addressBook->addEntry(address, label, description, paymentId);
    if (id < 0) {
        return -1;
    }
    QString error;
    const bool ok = saveAddressBook(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-063: LocalDataManager::addAddressEntry failed to persist"
            << "id=" << id << "error=" << error;
    } else {
        qInfo().noquote()
            << "st-063: LocalDataManager::addAddressEntry OK"
            << "id=" << id << "label=" << label;
    }
    return id;
}

bool LocalDataManager::updateAddressEntry(qint64 id,
                                          const QString& label,
                                          const QString& description,
                                          const QString& paymentId)
{
    if (id <= 0) {
        qWarning().noquote()
            << "st-063: LocalDataManager::updateAddressEntry rejected: id=" << id;
        return false;
    }
    const bool updated = m_addressBook->updateEntry(id, label, description, paymentId);
    if (!updated) {
        qWarning().noquote()
            << "st-063: LocalDataManager::updateAddressEntry no-op: id=" << id;
        return false;
    }
    QString error;
    const bool ok = saveAddressBook(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-063: LocalDataManager::updateAddressEntry failed to persist"
            << "id=" << id << "error=" << error;
    } else {
        qInfo().noquote()
            << "st-063: LocalDataManager::updateAddressEntry OK id=" << id;
    }
    return ok;
}

bool LocalDataManager::removeAddressEntry(qint64 id)
{
    if (id <= 0) {
        return false;
    }
    const bool removed = m_addressBook->removeEntry(id);
    if (!removed) {
        qWarning().noquote()
            << "st-063: LocalDataManager::removeAddressEntry no-op: id=" << id;
        return false;
    }
    QString error;
    const bool ok = saveAddressBook(&error);
    if (!ok) {
        qWarning().noquote()
            << "st-063: LocalDataManager::removeAddressEntry failed to persist"
            << "id=" << id << "error=" << error;
    } else {
        qInfo().noquote()
            << "st-063: LocalDataManager::removeAddressEntry OK id=" << id;
    }
    return ok;
}

bool LocalDataManager::importAddressBook(const QString& path)
{
    if (path.isEmpty()) {
        qWarning().noquote()
            << "st-063: LocalDataManager::importAddressBook rejected: empty path";
        return false;
    }
    QString error;
    const bool ok = m_addressBook->importFile(path, &error);
    if (!ok) {
        qWarning().noquote()
            << "st-063: LocalDataManager::importAddressBook failed"
            << "path=" << path << "error=" << error;
        return false;
    }
    // importFile() replaces the in-memory list; persist immediately.
    QString saveError;
    if (!saveAddressBook(&saveError)) {
        qWarning().noquote()
            << "st-063: LocalDataManager::importAddressBook persist failed"
            << "path=" << path << "error=" << saveError;
        return false;
    }
    qInfo().noquote()
        << "st-063: LocalDataManager::importAddressBook OK"
        << "path=" << path
        << "count=" << m_addressBook->size();
    return true;
}

bool LocalDataManager::exportAddressBook(const QString& path)
{
    if (path.isEmpty()) {
        qWarning().noquote()
            << "st-063: LocalDataManager::exportAddressBook rejected: empty path";
        return false;
    }
    QString error;
    const bool ok = m_addressBook->exportFile(path, &error);
    if (!ok) {
        qWarning().noquote()
            << "st-063: LocalDataManager::exportAddressBook failed"
            << "path=" << path << "error=" << error;
        return false;
    }
    qInfo().noquote()
        << "st-063: LocalDataManager::exportAddressBook OK"
        << "path=" << path
        << "count=" << m_addressBook->size();
    return true;
}

} // namespace judecoin_wallet::core::localdata
