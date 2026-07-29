// =============================================================================
// Judecoin Wallet - MultisigApi.h (st-005, multisig module)
//
// Strongly-typed request/response structs for the 2 wallet-rpc methods
// WalletRpcClient exposes as MultisigApi:
//
//   make_multisig  -> { address: <string>, multisig_info: <string> }
//   is_multisig     -> { multisig: bool, ready: bool, threshold: uint, total: uint }
//
// V5.4.x V1.0 cut: prepare_multisig / finalize_multisig / sign_multisig /
// submit_multisig are documented in CLI_COMMANDS.md 8.2 but not exposed
// here. They share the same envelope and can be added in V1.1 by extending
// this header. The two methods exposed here cover the wallet-setup
// primary use case ("enable N-of-M signing on this wallet").
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_MULTISIG_API_H
#define JUDECOIN_WALLET_CORE_RPC_MULTISIG_API_H

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstdint>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// MultisigInfo
//
// Result of make_multisig. Returns the newly generated multisig wallet address
// plus the multisig_info string that must be exchanged with other cosigners.
// `multisig_info` is hex-encoded; it contains each cosigner's spend public key.
// -----------------------------------------------------------------------------
struct MultisigInfo
{
    QString address;        // the N-of-M wallet address
    QString multisig_info;  // hex; exchange with other cosigners

    static bool parse(const QJsonObject& obj, MultisigInfo& out);
    QJsonObject toJson() const;
};

// -----------------------------------------------------------------------------
// MultisigStatus
//
// Result of is_multisig. `ready` distinguishes "in-progress multisig setup"
// from "fully prepared multisig wallet". `threshold` and `total` are only
// meaningful when multisig=true.
// -----------------------------------------------------------------------------
struct MultisigStatus
{
    bool    multisig   = false;
    bool    ready      = false;
    quint64 threshold  = 0;
    quint64 total      = 0;

    static bool parse(const QJsonObject& obj, MultisigStatus& out);
    QJsonObject toJson() const;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_MULTISIG_API_H
