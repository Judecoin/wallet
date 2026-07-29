// =============================================================================
// Judecoin Wallet - TransferApi.h (st-005, transfer module)
//
// Strongly-typed request/response structs for the 3 wallet-rpc methods
// WalletRpcClient exposes as TransferApi:
//
//   transfer_split  -> TransferResult     (send to N destinations, one tx)
//   sweep_all       -> TransferResult     (send all unlocked to one address)
//   sweep_single    -> TransferResult     (send a specific unlocked output)
//
// V5.4.3 B-plan (per docs/CLI_COMMANDS.md 8.5 transaction_tracking):
//   - UI is a dumb display; wallet-rpc handles all sync confirmation logic.
//   - transfer_split blocks internally until daemon is synced, then returns
//     the fully constructed tx_hash. UI just shows a spinner and waits.
//   - ring_size is hardcoded to 16 per devplan st-005 spec_notes (current
//     hard-fork enforces this; no future-proof knob in V1.0).
//   - priority is 0..3 (default/priority/unimportant/blank).
//
// All amounts are in atomic units. The UI multiplies by 1e-12 for display.
//
// V5.4.5 memory-safety update (gemma4-p0-fixes, gf-002):
//   TransferResult carries potentially sensitive material (tx_key is the
//   transaction private key per Monero's wallet_rpc; tx_blob is the raw
//   signed tx; tx_metadata can include payment-ids and decrypted notes).
//   QString does NOT auto-zero its heap buffer on destruction, so once
//   these strings are loaded into the struct the bytes linger in freed
//   memory until the allocator recycles the page. To meet the V5.4.5
//   "key clearing principle" requirement, callers MUST invoke secureClear() on the
//   TransferResult once they are done extracting the data they need.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_TRANSFER_API_H
#define JUDECOIN_WALLET_CORE_RPC_TRANSFER_API_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "../util/SecureBuffer.h"

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// TransferDestination
//
// One row of the destinations array. amount is atomic units (uint64). Both
// fields are mandatory; the wallet-rpc rejects empty addresses.
// -----------------------------------------------------------------------------
struct TransferDestination
{
    QString address;
    quint64 amount = 0;

    QJsonObject toJson() const;
    static bool parse(const QJsonObject& obj, TransferDestination& out);
};

// -----------------------------------------------------------------------------
// TransferRequest
//
// Common input shape for transfer_split / sweep_all / sweep_single. The UI
// constructs this once and passes it to the relevant WalletRpcClient method.
//
// Fields:
//
//   destinations    - list of (address, amount) pairs. For sweep_* this list
//                     must contain exactly one element (the sweep target).
//   account_index   - source sub-account (0 = primary).
//   subaddr_indices - subset of sub-addresses to draw from; empty = all.
//   priority        - 0..3 (see CLI_COMMANDS.md 8.5). Default 0.
//   ring_size       - hardcoded to 16 per devplan st-005; ignored if zero.
//   get_tx_key      - request tx private key in response (default false).
//   get_tx_hex      - request full tx blob in response (default false).
//   get_tx_metadata - request tx metadata blob (default false).
// -----------------------------------------------------------------------------
struct TransferRequest
{
    QVector<TransferDestination> destinations;
    quint64 account_index   = 0;
    QVector<quint64>        subaddr_indices;
    int     priority        = 0;
    quint64 ring_size       = 16;   // hardcoded per devplan
    bool    get_tx_key      = false;
    bool    get_tx_hex      = false;
    bool    get_tx_metadata = false;

    /// Serialise to the JSON-RPC params envelope. Exposed so tests can
    /// assert what we actually sent on the wire.
    QJsonObject toJson() const;

    /// Parse back from a JSON envelope (used by tests).
    static bool parse(const QJsonObject& obj, TransferRequest& out);
};

// -----------------------------------------------------------------------------
// TransferResult
//
// Common output for transfer_split / sweep_all / sweep_single. Wallet-rpc
// returns a flat object; amount_list and multisig_txset are optional.
//
// Per devplan st-005 spec_notes, the documented fields are:
//   amount, fee, tx_hash, tx_key, tx_blob, tx_metadata,
//   amount_list, multisig_txset.
//
// V5.4.5 memory-safety (gf-002, gemma4-p0-fixes):
//   The three fields below are marked SENSITIVE:
//     tx_key      - request tx private key (Monero wallet-rpc semantics)
//     tx_blob     - full signed transaction hex (contains key images)
//     tx_metadata - payment-id + decrypted notes (can leak recipient/amount)
//   QString does NOT zero its internal storage on destruction. Callers MUST
//   invoke secureClear() once they have copied any value they need to keep,
//   before the TransferResult object goes out of scope. The default-dtor
//   path (object destruction without secureClear()) leaves the bytes
//   resident in heap memory and is NOT acceptable for V5.4.5.
//
//   See src/core/util/SecureBuffer.h for the underlying volatile-write
//   scrubbing primitives.
//
//   IMPORTANT: do NOT change tx_key / tx_blob / tx_metadata from QString
//   to a non-Qt container. The wallet-rpc JSON parser, the test harness,
//   and the QML layer all consume QString here. Per the gf-002 spec we
//   honour Option A (helper + comments) and leave the type system alone.
// -----------------------------------------------------------------------------
struct TransferResult
{
    quint64 amount = 0;
    quint64 fee    = 0;
    QString tx_hash;
    QString tx_key;          // SENSITIVE (gf-002): tx private key. Clear via secureClear().
    QString tx_blob;         // SENSITIVE (gf-002): signed tx hex. Clear via secureClear().
    QString tx_metadata;     // SENSITIVE (gf-002): payment-id / decrypted notes. Clear via secureClear().
    QVector<quint64> amount_list;
    QString multisig_txset;  // empty for non-multisig transfers

    static bool parse(const QJsonObject& obj, TransferResult& out);
    QJsonObject toJson() const;

    /// V5.4.5 memory-safety helper (gf-002).
    ///
    /// Physically zero the three sensitive QString buffers (tx_key,
    /// tx_blob, tx_metadata) and then release them. Safe to call on a
    /// default-constructed or already-cleared result (no-op). Safe to
    /// call multiple times.
    ///
    /// Callers MUST invoke this once they are done with the result, e.g.
    ///
    ///     TransferResult r = client_->transferSplit(req);
    ///     // ... copy out whatever is needed ...
    ///     r.secureClear();      // <-- gf-002: scrub sensitive fields
    ///
    /// Leaving the default-dtor path to handle the strings is NOT
    /// sufficient; that would leak the bytes into freed heap memory.
    void secureClear() noexcept
    {
        core::util::secureClear(tx_key);
        core::util::secureClear(tx_blob);
        core::util::secureClear(tx_metadata);
        // multisig_txset can carry an encrypted txset blob on multisig
        // wallets; scrub it too to keep the invariant "every String field
        // that may hold key material is cleared".
        core::util::secureClear(multisig_txset);
    }
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_TRANSFER_API_H
