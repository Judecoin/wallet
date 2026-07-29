// =============================================================================
// Judecoin Wallet - LocalDataManager (st-017)
//
// Facade for the four UI-local .json files defined by V5.4.5:
//   ~/.config/JudecoinWallet/address_book.json
//   ~/.config/JudecoinWallet/tx_notes.json
//   ~/.config/JudecoinWallet/account_tags.json
//   ~/.config/JudecoinWallet/subaddress_labels.json
//
// st-112 adds a fifth file:
//   ~/.config/JudecoinWallet/tx_tags.json
// which holds the "structured tag" half of V5.4 §8 decision C
// (note + tag are independent features on the same tx).
//
// Why all five together: they're written together (any UI write to any
// of them comes through this manager), they share a single storage
// directory, and the UI never edits one without the others also being
// valid JSON. Owning all five in one place keeps load/save atomic-ish
// (each file is still independently committed via QSaveFile; we don't
// have a cross-file transaction, but the five writes happen back-to-back
// on the UI thread so the worst case is one file written, crash, four
// files missing - recoverable on next launch by loadAll returning the
// missing ones as empty).
//
// Storage path resolution:
//   - Default: QStandardPaths::AppConfigLocation (Linux: ~/.config/JudecoinWallet).
//   - Tests: pass an explicit baseDir; LocalDataManager does NOT cache
//     QStandardPaths at construction time, so it picks up changes if
//     the test harness swaps org/app names (matches SettingsManager's
//     ScopedSettingsEnv pattern from st-014).
//
// Threading: synchronous. The five files together are < 200 KB even for
// heavy users; QJsonDocument round-trip is sub-millisecond on V1.0
// hardware. Matching st-014 SettingsManager's synchronous-only rule.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_LOCALDATA_LOCAL_DATA_MANAGER_H
#define JUDECOIN_WALLET_CORE_LOCALDATA_LOCAL_DATA_MANAGER_H

#include "core/localdata/AccountTag.h"
#include "core/localdata/AddressBook.h"
#include "core/localdata/SubaddressLabel.h"
#include "core/localdata/TxNote.h"
#include "core/localdata/TxTag.h"

#include <QObject>
#include <QVariantList>
#include <QString>
#include <memory>

namespace judecoin_wallet::core::localdata {

// -----------------------------------------------------------------------------
// LocalDataManager
//
// One per UI instance. Construction is cheap; the five child stores are
// owned via unique_ptr so callers always go through the manager's
// getter methods (which return references) rather than holding pointers
// that could dangle if the manager is destroyed.
// -----------------------------------------------------------------------------
class LocalDataManager : public QObject
{
    Q_OBJECT

public:
    // `baseDir` overrides QStandardPaths::AppConfigLocation. The tests
    // pass a QTemporaryDir path so the production config never gets
    // touched. If `baseDir` is empty, the manager resolves it lazily
    // via the standard path at the time of the first load/save call.
    explicit LocalDataManager(const QString& baseDir = QString(),
                              QObject* parent   = nullptr);
    ~LocalDataManager() override;

    // -- Path helpers ------------------------------------------------------
    QString baseDir() const;
    QString filePath(const QString& fileName) const;

    // -- Bulk I/O ----------------------------------------------------------
    // loadAll() walks the five stores in a fixed order (addressBook,
    // txNotes, accountTags, subaddressLabels, txTags). If any single
    // file is missing, that's NOT an error (first run). If any file is
    // present but malformed, loadAll() returns false and sets *error to
    // the first failure encountered; subsequent stores are NOT loaded.
    //
    // saveAll() writes the five stores in the same order. A failure on
    // any one stops the bulk save and returns false; earlier writes
    // stay on disk (QSaveFile is atomic per file, but there's no
    // cross-file atomicity).
    bool loadAll(QString* error = nullptr);
    bool saveAll(QString* error = nullptr);

    // -- Per-store accessors (returned by reference; lifetime tied to the
    //    manager).
    AddressBook&      addressBook();
    TxNote&           txNotes();
    AccountTag&       accountTags();
    SubaddressLabel&  subaddressLabels();
    TxTag&            txTags();
    const AddressBook&     addressBook() const;
    const TxNote&          txNotes() const;
    const AccountTag&      accountTags() const;
    const SubaddressLabel& subaddressLabels() const;
    const TxTag&           txTags() const;

    // -- QML bridge (st-062) -------------------------------------------
    // HistoryPage / TxDetailDialog need to save and read the per-tx note
    // straight from QML. We expose the underlying TxNote facade as a
    // thin Q_INVOKABLE wrapper around the localdata::TxNote store so
    // the QML layer never has to touch the file I/O directly. Save
    // failures bubble up as a false return; the caller (TxDetailDialog)
    // shows a red qsTr("Failed to save note") hint under the input
    // when the round-trip does not succeed.
    //
    // The wrapper pair (get / set) is symmetric: setNote saves to
    // tx_notes.json on disk; getTxNote reads the current in-memory
    // value (which is loaded from tx_notes.json at app start via
    // loadAll() or loadTxNotes()). Both are no-throw and tolerate an
    // empty / non-existent baseDir (the manager resolves the standard
    // path on the first load).
    Q_INVOKABLE bool    saveTxNote(const QString& tx_hash, const QString& note);
    Q_INVOKABLE QString getTxNote(const QString& tx_hash) const;
    Q_INVOKABLE bool    removeTxNote(const QString& tx_hash);
    // -- QML bridge (st-112) ---------------------------------------------
    // TxDetailDialog.qml needs to save and read the per-tx structured tag
    // alongside the per-tx note. The two features are independent (V5.4
    // §8 decision C): the user might write "March rent" as the free-form note
    // AND set "Salary" as the structured tag. Bridge mirrors the TxNote
    // bridge exactly - same save-on-write contract, same auto-persist to
    // tx_tags.json on disk.
    //
    // Method names track the V5.4 §8 decision C contract (st-112 spec):
    //   setTag(hash, tag) - persist a tag string for tx_hash (overwrite ok)
    //   getTag(hash)      - return the current tag string ("" if absent)
    //   removeTag(hash)   - delete the entry (idempotent on missing tx)
    //
    // QML convention uses `accountTagObj` (set in main.cpp as an alias of
    // localDataManagerObj) so all callers reach the same bridge.
    Q_INVOKABLE bool    setTag(const QString& tx_hash, const QString& tag);
    Q_INVOKABLE QString getTag(const QString& tx_hash) const;
    Q_INVOKABLE bool    removeTag(const QString& tx_hash);
    // -- QML bridge (st-063) ---------------------------------------------
    // AddressBookPage.qml + EditAddressDialog.qml need CRUD + import/export
    // of named addresses straight from QML. We expose the underlying
    // AddressBook facade as a thin Q_INVOKABLE wrapper around the
    // localdata::AddressBook store so the QML layer never has to touch the
    // file I/O directly. Mutations auto-persist to address_book.json on disk
    // via saveAddressBook() so a restart shows the same list.
    // addressBookEntries() returns a QVariantList of QVariantMap (one map
    // per entry: id/address/label/description/payment_id/created_at/
    // updated_at) so the QML side can bind it as a ListView model without
    // writing a custom QAbstractListModel. The list is a *snapshot*:
    // callers should re-read on addressBookChanged() to refresh.
    //
    // addAddressEntry / updateAddressEntry return the assigned id (>=1) on
    // success, -1 on validation failure (empty address). Duplicate-address
    // dedupe is auto-handled in AddressBook and returns the existing id.
    // importAddressBook / exportAddressBook return true on success and
    // false on failure (e.g. file not found / malformed JSON).
    // importAddressBook also fires addressBookChanged() so QML lists
    // refresh.
    //
    // V5.4.7 compliance: the address field is plain text (a public Judecoin
    // address). No key/seed/password/privateKey crosses this surface;
    // payment_id is the legacy 16-hex id (hidden in V1.0 UI per V5.4.4 #6
    // decision A but preserved for forward-compat with imported data).
    Q_INVOKABLE QVariantList addressBookEntries() const;
    Q_INVOKABLE int          addressBookSize() const;
    Q_INVOKABLE qint64       addAddressEntry(const QString& address,
                                             const QString& label,
                                             const QString& description = QString(),
                                             const QString& paymentId   = QString());
    Q_INVOKABLE bool         updateAddressEntry(qint64 id,
                                                 const QString& label,
                                                 const QString& description,
                                                 const QString& paymentId);
    Q_INVOKABLE bool         removeAddressEntry(qint64 id);
    Q_INVOKABLE bool         importAddressBook(const QString& path);
    Q_INVOKABLE bool         exportAddressBook(const QString& path);


    // -- Per-store load / save (delegates to the child store's
    //    load()/save() with the resolved file path).
    bool loadAddressBook(QString* error = nullptr);
    bool loadTxNotes(QString* error = nullptr);
    bool loadAccountTags(QString* error = nullptr);
    bool loadSubaddressLabels(QString* error = nullptr);
    bool loadTxTags(QString* error = nullptr);

    bool saveAddressBook(QString* error = nullptr);
    bool saveTxNotes(QString* error = nullptr);
    bool saveAccountTags(QString* error = nullptr);
    bool saveSubaddressLabels(QString* error = nullptr);
    bool saveTxTags(QString* error = nullptr);

    // -- Bulk clear (used by the Settings -> Reset flow) -------------------
    // Resets the five in-memory stores to empty and DELETES the
    // underlying files from disk. Returns true if all five deletes
    // succeeded (or files were already absent).
    bool clearAll(QString* error = nullptr);

Q_SIGNALS:
    // Fired after a successful loadAll() / saveAll().
    void loaded();
    void saved();

    // Per-store change notifications - forwarded from the child stores
    // (LocalDataManager acts as a Qt signal relay so QML can listen to
    // a single object for any of the five data sources).
    void addressBookChanged();
    void txNotesChanged();
    void accountTagsChanged();
    void subaddressLabelsChanged();
    void txTagsChanged();

private:
    QString m_baseDirOverride;   // empty => resolve lazily
    mutable QString m_cachedBaseDir;
    std::unique_ptr <AccountTag>       m_accountTags;
    std::unique_ptr <AddressBook>      m_addressBook;
    std::unique_ptr <SubaddressLabel>  m_subaddressLabels;
    std::unique_ptr <TxNote>           m_txNotes;
    std::unique_ptr <TxTag>            m_txTags;

    QString resolveBaseDir() const;
};

} // namespace judecoin_wallet::core::localdata

#endif // JUDECOIN_WALLET_CORE_LOCALDATA_LOCAL_DATA_MANAGER_H