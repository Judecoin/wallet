// =============================================================================
// Judecoin Wallet - FreezeApi.h (st-005, freeze module)
//
// Strongly-typed request/response struct for the 1 wallet-rpc method
// WalletRpcClient exposes as FreezeApi in V1.0:
//
//   freeze -> FreezeResult
//
// V5.4.x V1.0 cut: thaw / frozen are documented in CLI_COMMANDS.md 8.2 but
// not exposed here. The single freeze method is the primary use case
// ("lock specific outputs from being spent until a block height"). thaw
// shares the same envelope and can be added in V1.1 by extending this
// header; frozen is a read-only listing that can be served by get_transfers
// + a UI-side filter.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_FREEZE_API_H
#define JUDECOIN_WALLET_CORE_RPC_FREEZE_API_H

#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// FreezeResult
//
// wallet-rpc returns { tx_hash, tx_key } for freeze so the UI can show the
// spending tx that locked the output. fields may be empty if the daemon
// optimistically locked without creating a tx (rare path; falls through to
// the regular error path if it actually failed).
// -----------------------------------------------------------------------------
struct FreezeResult
{
    QString tx_hash;
    QString tx_key;

    static bool parse(const QJsonObject& obj, FreezeResult& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_FREEZE_API_H
