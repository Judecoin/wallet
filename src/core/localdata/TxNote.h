// =============================================================================
// Judecoin Wallet - TxNote (st-017)
//
// Maps tx_hash -> free-form note text. Pure UI metadata; we do NOT call
// wallet-rpc's get_tx_note / set_tx_note even though the binary exposes
// them. The wallet-rpc key file is the sole owner of secrets (V5.4.7);
// tx notes aren't secrets but they live in UI-local JSON so the user can
// see them in the transaction detail page even before the wallet has
// fully synced.
//
// Schema (docs/UI_DETAILS_TODO.md section 8):
//   {
//     "version": 1,
//     "notes": {
//       "0x1234567890abcdef...": "March rent",
//       "0xfedcba0987654321...": "Coffee payment"
//     }
//   }
//
// Keys are the wallet-rpc get_transfers `txid` hex string (64 chars).
// Empty-string notes are stored as empty strings (NOT deleted) so the
// user can edit a note, save an empty value, and the next time the
// detail page is opened it won't show stale text. To actually forget a
// note, call removeNote().
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_LOCALDATA_TX_NOTE_H
#define JUDECOIN_WALLET_CORE_LOCALDATA_TX_NOTE_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace judecoin_wallet::core::localdata {

// -----------------------------------------------------------------------------
// TxNote
//
// One tx_notes.json file. Notes are stored as a flat QHash<QString, QString>
// for O(1) lookup by txid; iteration order is not guaranteed (QHash uses
// a sparse hash with internal ordering). For QML display we can hand
// the keys to a sorted QML model, but the on-disk format itself is
// order-agnostic.
// -----------------------------------------------------------------------------
class TxNote : public QObject
{
    Q_OBJECT

public:
    explicit TxNote(QObject* parent = nullptr);
    ~TxNote() override;

    static QString fileName();

    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path, QString* error = nullptr) const;

    QJsonObject toJson(bool withExportedMetadata = false) const;
    bool        fromJson(const QJsonObject& obj, QString* error = nullptr);

    bool exportFile(const QString& path, QString* error = nullptr) const;
    bool importFile(const QString& path, QString* error = nullptr);

    // -- Queries -----------------------------------------------------------
    bool isEmpty() const;
    int  size() const;
    QString noteFor(const QString& txid) const;
    bool    hasNote(const QString& txid) const;
    QHash <QString, QString> notes() const;
    QStringList txids() const;

    // -- Mutation ----------------------------------------------------------
    // setNote() with an empty value stores an empty string (the user
    // explicitly cleared the note text but kept the row). To remove the
    // note entry entirely, call removeNote().
    void setNote(const QString& txid, const QString& text);
    bool removeNote(const QString& txid);
    // -- V5.4.5 / gf-004 memory scrubbing -------------------------------
    // Physically zero every note text via util::secureClear() before
    // releasing the hash, so a heap dump after clearAll() does not
    // surface the note text. Idempotent + no-throw.
    void secureClear() noexcept;

    void clear();

    static constexpr int kSchemaVersion = 1;

Q_SIGNALS:
    void noteSet(const QString& txid);
    void noteRemoved(const QString& txid);
    void cleared();

private:
    QHash <QString, QString> m_notes;
};

} // namespace judecoin_wallet::core::localdata

#endif // JUDECOIN_WALLET_CORE_LOCALDATA_TX_NOTE_H
