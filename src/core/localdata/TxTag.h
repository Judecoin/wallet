// =============================================================================
// Judecoin Wallet - TxTag (st-112)
//
// Maps tx_hash -> a short free-form tag string ("March rent", "Salary",
// "Trading"). Pure UI metadata, lives in UI-local JSON next to
// tx_notes.json so the user can see the tag on the transaction detail
// page even before the wallet has fully synced.
//
// Per V5.4 §8 decision C (st-112 spec_notes), tag and note are TWO INDEPENDENT
// features on the same tx: the user might write "March rent" as the
// free-form note AND set "Salary" as the structured tag. The TxNote and
// TxTag stores are kept separate so we can evolve each without breaking
// the other (e.g. V1.1 might add per-tag colour, while notes stay
// plain-text).
//
// Schema (docs/UI_DETAILS_TODO.md section 8 + st-112 spec):
//   {
//     "version": 1,
//     "tags": {
//       "0x1234567890abcdef...": "Salary",
//       "0xfedcba0987654321...": "Trading"
//     }
//   }
//
// Keys are the wallet-rpc get_transfers `txid` hex string (64 chars).
// Empty-string tags are stored as empty strings (NOT deleted) so the
// user can edit a tag, save an empty value, and the next time the
// detail page is opened it won't show stale text. To actually forget a
// tag, call removeTag().
//
// V5.4.7 compliance: tag text is user-supplied free text, NOT a secret.
// tx_hash is public on-chain data. We never store keys/seeds/passwords
// here.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_LOCALDATA_TX_TAG_H
#define JUDECOIN_WALLET_CORE_LOCALDATA_TX_TAG_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace judecoin_wallet::core::localdata {

// -----------------------------------------------------------------------------
// TxTag
//
// One tx_tags.json file. Tags are stored as a flat QHash<QString, QString>
// for O(1) lookup by txid; iteration order is not guaranteed (QHash uses
// a sparse hash with internal ordering). For QML display we can hand
// the keys to a sorted QML model, but the on-disk format itself is
// order-agnostic.
//
// This class is the storage backend; the QML-facing surface lives on
// LocalDataManager (Q_INVOKABLE saveTxTag / getTxTag / removeTxTag),
// mirroring the TxNote bridge pattern from st-062.
// -----------------------------------------------------------------------------
class TxTag : public QObject
{
    Q_OBJECT

public:
    explicit TxTag(QObject* parent = nullptr);
    ~TxTag() override;

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

    /// Returns the tag string for txid, or "" if no tag is set.
    /// st-112 spec: Q_INVOKABLE bridge is `tagFor(hash)`.
    QString tagFor(const QString& txid) const;

    /// True iff this txid has a (possibly empty) tag entry.
    bool hasTag(const QString& txid) const;

    /// All txids with a tag, in arbitrary order (QHash order).
    QHash <QString, QString> tags() const;
    QStringList txids() const;

    // -- Mutation ----------------------------------------------------------
    /// setTag() with an empty value stores an empty string (the user
    /// explicitly cleared the tag text but kept the row). To remove the
    /// tag entry entirely, call removeTag().
    /// st-112 spec: Q_INVOKABLE bridge is `setTag(hash, tag)`.
    void setTag(const QString& txid, const QString& tag);

    /// Returns true iff the entry existed and was removed. Returns false
    /// for an unknown txid (caller can still treat this as success -
    /// the desired post-condition holds).
    /// st-112 spec: Q_INVOKABLE bridge is `removeTag(hash)`.
    bool removeTag(const QString& txid);

    // -- V5.4.5 / gf-004 memory scrubbing -------------------------------
    // Physically zero every tag text via util::secureClear() before
    // releasing the hash, so a heap dump after clearAll() does not
    // surface the tag text. Idempotent + no-throw.
    void secureClear() noexcept;

    void clear();

    static constexpr int kSchemaVersion = 1;

Q_SIGNALS:
    void tagSet(const QString& txid);
    void tagRemoved(const QString& txid);
    void cleared();

private:
    QHash <QString, QString> m_tags;
};

} // namespace judecoin_wallet::core::localdata

#endif // JUDECOIN_WALLET_CORE_LOCALDATA_TX_TAG_H