// =============================================================================
// Judecoin Wallet - AddressBook (st-017)
//
// Stores named addresses the user wants to recognise in the Send flow.
// Pure UI-side metadata (V5.4.5); we do NOT call wallet-rpc's
// get_address_book / add_address_book / delete_address_book even though the
// binary exposes them. V5.4.7 reaffirms: wallet-rpc is the sole encryptor
// of secrets, but address_book entries aren't secrets - they're labels -
// so they live in UI-local JSON.
//
// Schema (docs/UI_DETAILS_TODO.md section 6):
//   {
//     "version": 1,
//     "entries": [
//       {
//         "id":         1,
//         "address":    "4BGpF12RsLYbTboNedF1ES8idarKGh...Cn96vsrv",
//         "label":      "Binance",
//         "description":"Crypto exchange deposit",
//         "payment_id": "",
//         "created_at": "2026-07-03 12:00",
//         "updated_at": "2026-07-03 12:00"
//       }
//     ]
//   }
//
// V1.0 file is plaintext on disk (OS file permissions only); V1.1+ plans to
// wrap it in the QSettings master password. The reader is forward-tolerant:
// unknown fields in the JSON are kept on disk untouched (we round-trip via
// QJsonObject, not a stripped DTO) so V1.1+ can add fields without breaking
// V1.0 readers.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_LOCALDATA_ADDRESS_BOOK_H
#define JUDECOIN_WALLET_CORE_LOCALDATA_ADDRESS_BOOK_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace judecoin_wallet::core::localdata {

// -----------------------------------------------------------------------------
// AddressBookEntry
//
// Plain DTO; no QObject (these are value types, embedded in QML model lists).
//
// Field semantics match docs/UI_DETAILS_TODO.md section 6:
//   id          - monotonic, never reused after remove. Assigned by addEntry().
//   address     - the underlying Judecoin address (string copy; no validation
//                 here - validation is the Send flow's job, see st-020 URI
//                 handler).
//   label       - short user-visible name, e.g. "Binance".
//   description - longer free-form notes, optional.
//   payment_id  - legacy 16-hex payment id; V1.0 hides it from the UI
//                 (V5.4.4 #6 decision A) but we still serialise it for
//                 forward-compat with users who imported an old export.
//   created_at  - "YYYY-MM-DD HH:MM" timestamp string (local time, human
//                 readable). Not an ISO-8601 because the spec example uses
//                 this exact format.
//   updated_at  - same format; bumped on every successful updateEntry().
// -----------------------------------------------------------------------------
struct AddressBookEntry
{
    qint64   id          = 0;
    QString  address;
    QString  label;
    QString  description;
    QString  paymentId;
    QString  createdAt;
    QString  updatedAt;

    // -- V5.4.5 / gf-004 memory scrubbing -------------------------------
    // Physically zero every QString field via util::secureClear() so a
    // heap dump after clearAll() does not surface the entry's address /
    // payment_id / label. Idempotent + no-throw. Safe to call multiple
    // times; safe on a default-constructed entry.
    void secure_clear() noexcept;
};

// -----------------------------------------------------------------------------
// AddressBook
//
// Owns one address_book.json file. Exposes CRUD + import/export + signals.
// Construction is cheap; data is held in memory until save() persists it.
// -----------------------------------------------------------------------------
class AddressBook : public QObject
{
    Q_OBJECT

public:
    explicit AddressBook(QObject* parent = nullptr);
    ~AddressBook() override;

    // -- File name (used by LocalDataManager to resolve absolute path) ------
    static QString fileName();

    // -- I/O ---------------------------------------------------------------
    // load() / save() use the absolute `path` argument; LocalDataManager
    // resolves it from baseDir(). Both return true on success. On failure
    // they return false AND set *error (if non-null) to a human-readable
    // message. load() of a missing file is NOT an error - it returns true
    // and leaves the in-memory list empty (first run UX).
    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path, QString* error = nullptr) const;

    // -- JSON snapshot for import/export -----------------------------------
    // toJson() / fromJson() are the wire format. exportFile() / importFile()
    // wrap them with file I/O for the UI's [Import] / [Export] buttons.
    QJsonObject toJson(bool withExportedMetadata = false) const;
    bool        fromJson(const QJsonObject& obj, QString* error = nullptr);

    bool exportFile(const QString& path, QString* error = nullptr) const;
    bool importFile(const QString& path, QString* error = nullptr);

    // -- Queries -----------------------------------------------------------
    bool isEmpty() const;
    int  size() const;
    QVector <AddressBookEntry> entries() const;

    // Lookup by id; returns a default-constructed entry with id=0 if not
    // found. (Returning a struct, not a pointer, keeps QML bindings simple.)
    AddressBookEntry entryById(qint64 id) const;

    // Lookup by address; returns -1 if not found. Used by the Send flow to
    // auto-fill the label when the user pastes a known address.
    qint64 idForAddress(const QString& address) const;

    // -- Mutation ----------------------------------------------------------
    // addEntry() assigns a fresh id (max(existing) + 1) and stamps
    // created_at / updated_at. Returns the new id, or -1 on validation
    // failure (e.g. empty address).
    qint64 addEntry(const QString& address,
                    const QString& label,
                    const QString& description = QString(),
                    const QString& paymentId   = QString());

    // updateEntry() patches fields. Empty string arguments mean "leave
    // unchanged" for label/description/paymentId. id must match an existing
    // entry; returns false otherwise.
    bool updateEntry(qint64 id,
                     const QString& label,
                     const QString& description,
                     const QString& paymentId);

    bool removeEntry(qint64 id);

    // -- V5.4.5 / gf-004 memory scrubbing --------------------------------
    // Same as clear() but every entry's QString fields are physically
    // zeroed via util::secureClear() before the vector is released.
    // Called by LocalDataManager::clearAll() so a heap dump after a
    // reset does not surface the address_book's addresses / payment_ids
    // / labels. Idempotent + no-throw.
    void secureClear() noexcept;

    void clear();

    // -- Schema constants --------------------------------------------------
    static constexpr int kSchemaVersion = 1;

Q_SIGNALS:
    // Fires after addEntry / updateEntry / removeEntry / clear / importFile
    // mutate the in-memory list. (No signal for save() - persistence is not
    // a model change.) The signal argument is the affected id (0 for
    // clear/importFile bulk operations, since multiple ids may be affected).
    void entryAdded(qint64 id);
    void entryUpdated(qint64 id);
    void entryRemoved(qint64 id);
    void cleared();

private:
    QVector <AddressBookEntry> m_entries;
    qint64 m_nextId = 1; // bumps monotonically; never reset on remove

    void recomputeNextId();
    QString nowLocalTimestamp() const;
};

} // namespace judecoin_wallet::core::localdata

#endif // JUDECOIN_WALLET_CORE_LOCALDATA_ADDRESS_BOOK_H
