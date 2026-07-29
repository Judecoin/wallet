// =============================================================================
// Judecoin Wallet - TxProofApi.h (st-005, tx_proofs module)
//
// Strongly-typed request/response structs for the 4 wallet-rpc methods
// WalletRpcClient exposes as TxProofApi:
//
//   get_tx_key    -> { tx_key: <hex> }       (private key for a tx we sent)
//   check_tx_key  -> CheckTxKeyResult        (verify tx_key + amount + dest)
//   get_tx_proof   -> { signature: <hex> }    (MCTX proof)
//   check_tx_proof -> CheckTxProofResult      (verify signature + dest)
//
// V5.4.5 (storage_policy): get_tx_note / set_tx_note are NOT exposed here.
// Per docs/DECISIONS.md, tx notes are stored in UI-local .json (LocalData-
// Manager, st-017), not via wallet-rpc. The 4 proof methods in this header
// cover the cryptographic proof surface area only.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_TX_PROOF_API_H
#define JUDECOIN_WALLET_CORE_RPC_TX_PROOF_API_H

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// CheckTxKeyResult
//
// Result of check_tx_key: did the recipient receive the amount we claim we
// sent? Returns the amount confirmed in `received`, plus the number of
// confirmations. `in_pool` means the tx is still pending (not in a block yet).
// -----------------------------------------------------------------------------
struct CheckTxKeyResult
{
    quint64 received = 0;       // confirmed amount (atomic units)
    bool    in_pool  = false;   // true if tx still in mempool
    quint64 confirmations = 0;  // 0 if in_pool

    static bool parse(const QJsonObject& obj, CheckTxKeyResult& out);
    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// CheckTxProofResult
//
// Result of check_tx_proof. wallet-rpc returns:
//   { confirmations, in_pool, good } - "good" indicates the signature
// matched the claimed destination address.
// -----------------------------------------------------------------------------
struct CheckTxProofResult
{
    quint64 confirmations = 0;
    bool    in_pool  = false;
    bool    good     = false;   // signature matches claimed destination

    static bool parse(const QJsonObject& obj, CheckTxProofResult& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_TX_PROOF_API_H
