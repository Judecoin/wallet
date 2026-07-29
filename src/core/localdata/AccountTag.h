// =============================================================================
// Judecoin Wallet - AccountTag (st-017)
//
// Named colour-coded tags that the user attaches to a wallet. V5.4.4 #8
// decided account_tags.json lives in UI-local JSON; we do NOT call
// wallet-rpc's tag_accounts / get_account_tags / set_account_tag even
// though the binary exposes them.
//
// Schema (docs/UI_DETAILS_TODO.md section 8):
//   {
//     "version": 1,
//     "tags": [
//       { "id": 1, "tag": "Long-term", "color": "#4CAF50" },
//       { "id": 2, "tag": "Spending",  "color": "#2196F3" },
//       { "id": 3, "tag": "Trading",   "color": "#FF9800" }
//     ]
//   }
//
// V1.0 only stores the tag definitions; tag <-> wallet-account mapping
// is owned by wallet-rpc's account_tags (not used in V1.0 per V5.4.5).
// V1.1+ may add a mapping table here too.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_LOCALDATA_ACCOUNT_TAG_H
#define JUDECOIN_WALLET_CORE_LOCALDATA_ACCOUNT_TAG_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace judecoin_wallet::core::localdata {

struct AccountTagEntry
{
    qint64  id   = 0;
    QString tag;    // user-visible name
    QString color;  // CSS hex color, e.g. "#4CAF50"

    // -- V5.4.5 / gf-004 memory scrubbing -------------------------------
    // Physically zero both QString fields via util::secureClear() so the
    // tag name does not survive in freed heap pages after clearAll().
    // Idempotent + no-throw. Safe on a default-constructed entry.
    void secure_clear() noexcept;
};

class AccountTag : public QObject
{
    Q_OBJECT

public:
    explicit AccountTag(QObject* parent = nullptr);
    ~AccountTag() override;

    static QString fileName();

    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path, QString* error = nullptr) const;

    QJsonObject toJson(bool withExportedMetadata = false) const;
    bool        fromJson(const QJsonObject& obj, QString* error = nullptr);

    bool exportFile(const QString& path, QString* error = nullptr) const;
    bool importFile(const QString& path, QString* error = nullptr);

    bool isEmpty() const;
    int  size() const;
    QVector <AccountTagEntry> tags() const;
    AccountTagEntry tagById(qint64 id) const;
    qint64 idForName(const QString& name) const;

    // Returns the new id, or -1 if `name` is empty.
    qint64 addTag(const QString& name, const QString& color);
    bool updateTag(qint64 id, const QString& name, const QString& color);
    bool removeTag(qint64 id);
    // -- V5.4.5 / gf-004 memory scrubbing --------------------------------
    // Same as clear() but every entry's QString fields are physically
    // zeroed via util::secureClear() before the vector is released.
    // Called by LocalDataManager::clearAll() so a heap dump after a
    // reset does not surface the tag list. Idempotent + no-throw.
    void secureClear() noexcept;

    void clear();

    static constexpr int kSchemaVersion = 1;

Q_SIGNALS:
    void tagAdded(qint64 id);
    void tagUpdated(qint64 id);
    void tagRemoved(qint64 id);
    void cleared();

private:
    QVector <AccountTagEntry> m_tags;
    qint64 m_nextId = 1;

    void recomputeNextId();
};

} // namespace judecoin_wallet::core::localdata

#endif // JUDECOIN_WALLET_CORE_LOCALDATA_ACCOUNT_TAG_H
