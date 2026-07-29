// =============================================================================
// Judecoin Wallet - SubaddressLabel (st-017)
//
// Per-subaddress labels for account 0 (the only account in V1.0; V1.1+
// will add a per-account layer). Subaddresses are numbered inside their
// account, starting at 0 (the primary address).
//
// V5.4.5 decision: labels live in UI-local .json because wallet-rpc
// does not expose set_subaddress_label / get_subaddress_label as RPC
// methods. The on-binary versions of those functions are internal.
//
// Schema:
//   {
//     "version": 1,
//     "labels": {
//       "0:0": "Primary",
//       "0:1": "Binance deposit",
//       "0:2": "Cold storage"
//     }
//   }
//
// Keys are "account_index:subaddress_index". We do not constrain to a
// single account index in V1.0 - the file format supports it, but the
// UI surface only ever edits account 0. When V1.1+ exposes multi-account
// flows, the same schema is used and the UI just lets the user pick
// which key prefix to write.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_LOCALDATA_SUBADDRESS_LABEL_H
#define JUDECOIN_WALLET_CORE_LOCALDATA_SUBADDRESS_LABEL_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace judecoin_wallet::core::localdata {

class SubaddressLabel : public QObject
{
    Q_OBJECT

public:
    explicit SubaddressLabel(QObject* parent = nullptr);
    ~SubaddressLabel() override;

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
    QString labelFor(quint32 accountIndex, quint32 subaddressIndex) const;
    bool    hasLabel(quint32 accountIndex, quint32 subaddressIndex) const;

    QHash <QString, QString> labels() const;

    // -- Mutation ----------------------------------------------------------
    // Empty text removes the label (so the UI's "clear" action is a
    // single setLabel("", ...)).
    void setLabel(quint32 accountIndex, quint32 subaddressIndex, const QString& text);
    bool clearLabel(quint32 accountIndex, quint32 subaddressIndex);
    // -- V5.4.5 / gf-004 memory scrubbing -------------------------------
    // Physically zero every label value via util::secureClear() before
    // releasing the hash, so a heap dump after clearAll() does not
    // surface the user-typed label text. Idempotent + no-throw.
    void secureClear() noexcept;

    void clear();

    // -- Helpers (key encoding) -------------------------------------------
    static QString makeKey(quint32 accountIndex, quint32 subaddressIndex);
    static bool    parseKey(const QString& key, quint32* accountIndex, quint32* subaddressIndex);

    static constexpr int kSchemaVersion = 1;

Q_SIGNALS:
    void labelSet(quint32 accountIndex, quint32 subaddressIndex);
    void labelCleared(quint32 accountIndex, quint32 subaddressIndex);
    void cleared();

    // ---- address cache (FIX-G22, 2026-07-18) ------------------
    // Because wallet-rpc get_address(0, N) returns the PRIMARY address
    // for N>0 in some forks, we cannot rely on getAddress() to rebuild
    // subaddress entries during refresh().  Instead, we cache the
    // actual base58 address returned by create_address so refresh()
    // can read it back directly.
    //
    // This lives alongside m_labels in the same JSON file under an
    // "addresses" key, keeping labels and addresses decoupled.
public:
    void setAddress(quint32 accountIndex, quint32 subaddressIndex,
                    const QString& address);
    QString addressFor(quint32 accountIndex, quint32 subaddressIndex) const;
    bool hasAddress(quint32 accountIndex, quint32 subaddressIndex) const;
    void clearAddress(quint32 accountIndex, quint32 subaddressIndex);
    QHash<QString, QString> addresses() const { return m_addresses; }

private:
    QHash <QString, QString> m_labels;
    QHash <QString, QString> m_addresses;   // "acct:idx" → base58 address
};

} // namespace judecoin_wallet::core::localdata

#endif // JUDECOIN_WALLET_CORE_LOCALDATA_SUBADDRESS_LABEL_H
