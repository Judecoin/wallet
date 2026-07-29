// =============================================================================
// Judecoin Wallet - HistoryApi.h (st-005, history module)
//
// Strongly-typed request/response structs for the 3 wallet-rpc methods
// WalletRpcClient exposes as HistoryApi:
//
//   get_transfer   -> TransferEntry       (single TX by tx_hash)
//   get_transfers  -> TransferList        (filtered list, paged by account)
//   get_payments   -> PaymentList         (filter by payment_id, receive)
//
// V5.4.3 B-plan: the UI does NOT compute confirmations or unlock state. All
// those fields come pre-filled from the wallet-rpc response. The TransferEntry
// struct surfaces them verbatim for the UI to render (pending/confirming/
// confirmed state machine is in UI_DETAILS_TODO.md #8).
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_HISTORY_API_H
#define JUDECOIN_WALLET_CORE_RPC_HISTORY_API_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// TransferEntry
//
// One row of wallet history. Fields surfaced by the UI per docs/CLI_COMMANDS.md
// 8.5 transaction_tracking.get_transfers_response_fields_used_by_ui:
//
//   tx_hash         - hex string, displayed as 0x1234...5678
//   amount          - atomic units (signed: positive = incoming, negative = outgoing)
//   block_height    - 0 = pending in mempool; >0 = confirmed in chain
//   confirmations   - pre-filled by wallet-rpc; UI just displays
//   unlocked        - pre-filled by wallet-rpc; UI just displays
//   fee             - atomic units (outgoing only; 0 for incoming)
//   timestamp       - unix epoch seconds, when the tx was first seen
//   payment_id      - optional encrypted payment ID (legacy)
//   destinations    - for outgoing TXs, list of recipient addresses
//   type            - "in" | "out" | "pending" | "failed" | "pool"
//
// `destinations` is a vector of AccountAddress because sub-addresses are
// also valid destinations. `block_height == 0` means pending.
// -----------------------------------------------------------------------------
struct TransferEntry
{
    QString tx_hash;
    qint64  amount = 0;
    quint64 block_height = 0;
    quint64 confirmations = 0;
    bool    unlocked = false;
    quint64 fee = 0;
    quint64 timestamp = 0;
    QString payment_id;
    QVector<QString> destinations;   // address strings
    QString type;

    static bool parse(const QJsonObject& obj, TransferEntry& out);
    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// TransferList
//
// Result envelope of get_transfers. The wallet-rpc returns a single object
// containing per-list-type arrays; we collapse them into one flat vector
// for the UI. `outgoing`, `incoming`, `pending`, `failed`, `pool` are all
// lists of TransferEntry.
// -----------------------------------------------------------------------------
struct TransferList
{
    QVector<TransferEntry> entries;

    static bool parse(const QJsonObject& obj, TransferList& out);
    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// PaymentEntry
//
// One row of get_payments result. Same shape as TransferEntry but constrained
// to incoming transfers only (filtered by payment_id on the wallet-rpc side).
// -----------------------------------------------------------------------------
struct PaymentEntry
{
    QString payment_id;
    QString tx_hash;
    quint64 amount = 0;
    quint64 block_height = 0;
    quint64 unlock_time = 0;
    bool    locked = false;

    static bool parse(const QJsonObject& obj, PaymentEntry& out);
    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// PaymentList
//
// Result envelope of get_payments. Wallet-rpc returns { payments: [...] }.
// -----------------------------------------------------------------------------
struct PaymentList
{
    QVector<PaymentEntry> payments;

    static bool parse(const QJsonObject& obj, PaymentList& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_HISTORY_API_H
