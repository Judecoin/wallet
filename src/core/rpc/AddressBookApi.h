// =============================================================================
// Judecoin Wallet - AddressBookApi.h (st-005, address_book module)
//
// Strongly-typed request/response structs for the 3 wallet-rpc methods
// WalletRpcClient exposes as AddressBookApi:
//
//   get_address_book    -> AddressBookEntry list
//   add_address_book    -> AddressBookEntry index
//   delete_address_book -> { }      (wallet-rpc returns empty on success)
//
// V5.4.5 (storage_policy): per docs/DECISIONS.md, address_book entries are
// *also* mirrored to UI-local .json files (LocalDataManager, st-017). The
// wallet-rpc copy remains the source of truth on the daemon side; the UI
// local copy is the fast-path cache for the QML layer.
//
// V5.4.7: UI never reads .keys files; the address book here stores only
// public addresses + descriptive labels. No private keys ever touch this
// layer.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_ADDRESS_BOOK_API_H
#define JUDECOIN_WALLET_CORE_RPC_ADDRESS_BOOK_API_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// AddressBookEntry
//
// One row of the wallet's address book. The wallet-rpc returns objects with
// fields: { index, address, payment_id, description } where:
//   - index        - unique row id (use as the deletion key)
//   - address      - public Loki/Oxen address (95 chars mainnet)
//   - payment_id   - optional short payment id (legacy; usually empty)
//   - description  - free-form label ("Alice's savings", etc.)
//
// V5.4.5: the UI may also tag entries locally via LocalDataManager. The
// local tags are *not* sent back to wallet-rpc in V1.0.
// -----------------------------------------------------------------------------
struct AddressBookEntry
{
    quint64 index       = 0;
    QString address;
    QString payment_id;     // empty if none
    QString description;    // free-form

    static bool parse(const QJsonObject& obj, AddressBookEntry& out);
    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// AddressBookEntryList
//
// get_address_book returns { entries: [...] } per the wallet-rpc contract.
// -----------------------------------------------------------------------------
struct AddressBookEntryList
{
    QVector<AddressBookEntry> entries;

    static bool parse(const QJsonObject& obj, AddressBookEntryList& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_ADDRESS_BOOK_API_H
