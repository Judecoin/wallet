// =============================================================================
// Judecoin Wallet - BalanceProofApi.h (st-005, balance_proofs module)
//
// Strongly-typed request/response structs for the 2 wallet-rpc methods
// WalletRpcClient exposes as BalanceProofApi:
//
//   get_reserve_proof  -> { signature: <hex> }     (prove total balance >= amount)
//   check_reserve_proof -> CheckReserveProofResult  (verify against amount)
//
// V5.4.x V1.0 cut: get_spend_proof / check_spend_proof are documented in
// CLI_COMMANDS.md 8.2 under "balance proof" but not implemented here. They share
// the same shape and can be added in V1.1 by extending this header. The
// spec proof count for st-005 is 30 cases; reserve_proof covers the
// primary use case ("audit wallet balance to a counterparty").
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_BALANCE_PROOF_API_H
#define JUDECOIN_WALLET_CORE_RPC_BALANCE_PROOF_API_H

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// CheckReserveProofResult
//
// wallet-rpc returns:
//   { total, spent, good } where:
//   total  - confirmed total balance at the time the proof was made
//   spent  - amount already spent (atomic units)
//   good   - the proof is mathematically valid AND current balance >= claimed
//
// The UI displays total - spent as the available balance; "good" determines
// whether to show a green checkmark in the proof viewer.
// -----------------------------------------------------------------------------
struct CheckReserveProofResult
{
    quint64 total  = 0;
    quint64 spent  = 0;
    bool    good   = false;

    static bool parse(const QJsonObject& obj, CheckReserveProofResult& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_BALANCE_PROOF_API_H
