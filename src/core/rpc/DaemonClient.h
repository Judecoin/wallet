// =============================================================================
// Judecoin Wallet - DaemonClient (st-004)
//
// Thin wrapper around HttpRpcClient that talks JSON-RPC to the mainnet
// daemon (V5.4.2 mainnet-only). In V1.0 the daemon is the *single source of
// truth* for the chain / node state - the UI only ever calls one method:
//
//     daemon.get_info   ->  { version, nettype, synchronized, height,
//                             tx_pool_size, ... }
//
// Two layers of supervision keep the UI honest about daemon liveness:
//
//   1. A 5 s poll (heartbeat) of daemon.get_info - every successful call
//      refreshes the cached DaemonInfo and resets the failure counter.
//   2. A 3 s per-request timeout + a "3 consecutive failures == dead" rule
//      (V5.4.4 #2 spirit: health must degrade on real network problems,
//      not on a single hiccup). The combined logic emits the
//      `daemonHealthChanged(healthy)` signal exactly on state transitions
//      so the QML layer doesn't have to debounce.
//
// Signal contract:
//   daemonHealthChanged(bool healthy)  - emitted on the Qt event loop thread
//                                        that owns this object. Transitions
//                                        only (no spam on every tick).
//   infoUpdated(DaemonInfo info)        - emitted every successful get_info.
//
// Lifetime:
//   QObject parent / heap-allocated. startHeartbeat() schedules the first
//   poll immediately; stopHeartbeat() (or destruction) cancels any
//   in-flight call via the HttpRpcClient base.
//
// V5.4.2 (mainnet-only): the only supported nettype is "mainnet" (port
// 19061). The struct still surfaces whatever the daemon returned so that
// future releases can compare against the expected value.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_DAEMON_CLIENT_H
#define JUDECOIN_WALLET_CORE_RPC_DAEMON_CLIENT_H

#include "core/rpc/HttpRpcClient.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtCore/QTimer>

#include <chrono>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// DaemonInfo
//
// Strongly-typed view of daemon.get_info. Only the fields the UI cares about
// are surfaced explicitly; everything else is preserved in `extras` so
// future versions of the wallet can add consumers without breaking ABI.
//
// Field reference (per Loki/Oxen v3.2.0 binary `judecoind get_info`):
//   version          - packed integer version. For legacy builds with int
//                      version (e.g. 65558 for v3.2.0) the value is
//                      carried through as-is. For v3.2.0+ binaries that
//                      emit a string version ("3.2.0-release") the parser
//                      packs it as (major<<16)|(minor<<8)|patch = 0x030200.
//                      (Both representations end up non-zero; test code
//                      can assert `info.version > 0` to verify the call
//                      succeeded.)
//   version_full     - raw string value the daemon returned for `version`.
//                      Useful for diagnostic / logging: carries
//                      "3.2.0-release", "65558", "v3.2.0-rc1" etc.
//   version_major    - 3
//   version_minor    - 2
//   version_patch    - 0
//   nettype          - "mainnet" | "testnet" | "stagenet" | "regtest" (V1.0 = mainnet)
//   synchronized     - bool, daemon caught up with the network tip
//   height           - quint64, top block height the daemon knows about
//   tx_pool_size     - quint64, size of the mempool (pending tx count)
//   version_string   - human-readable e.g. "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)".
//                      In v3.2.0 the daemon does NOT emit `version_string`;
//                      the parser falls back to the `status_line` field
//                      (e.g. "v3.2.0; Height: 829271, SN: no").
//   top_block_hash   - hex string of the top block hash
//   cumulative_difficulty - hex string, total chain work
// -----------------------------------------------------------------------------
// FIX-G9 (2026-07-17 21:24): DaemonInfo converted from `struct`
// to a Q_GADGET with explicit Q_PROPERTY entries so QML's meta-object
// system can access `info.height` / `info.synchronized` over the
// `infoUpdated(DaemonInfo)` signal connection. Pre-fix the struct had
// only Q_DECLARE_METATYPE (which handles queued-connection marshaling
// across threads) but no Q_GADGET, so QML saw `info.height` as
// undefined and every heartbeat tick printed "Cannot assign
// [undefined] to int" at Dashboard.qml:504. Only the two fields
// Dashboard.qml binds to are promoted to Q_PROPERTY; the rest stay as
// plain members for C++ consumers.
//
// FIX-G10 (2026-07-18 01:13): added `target_height` Q_PROPERTY so
// the Dashboard pill can render real sync progress against the network
// tip instead of the fake h/(h+10000) approximation. Daemon emits this
// field alongside `height`; when the daemon is fully synced it is
// usually 0 (or equal to height), and when it is still catching up it
// carries the network tip height. QML reads this through the
// DaemonClient::targetHeight Q_PROPERTY (see FIX-G9b block below) and
// binds it to Dashboard.qml's `syncTargetHeight`.
class DaemonInfo
{
    Q_GADGET
    Q_PROPERTY(quint64 height         MEMBER height)
    Q_PROPERTY(bool    synchronized   MEMBER synchronized)
    // FIX-G10: target_height from daemon.get_info. 0 when daemon is
    // already caught up (or when the daemon build does not surface it);
    // > height when the daemon is still syncing behind the network tip.
    Q_PROPERTY(quint64 target_height  MEMBER target_height)

public:
    quint64 version           = 0;
    quint32 version_major     = 0;
    quint32 version_minor     = 0;
    quint32 version_patch     = 0;
    QString version_full;     // raw `version` field value, unchanged
    QString nettype;          // "mainnet" | "testnet" | "stagenet" | "regtest" | ""
    bool    synchronized      = false;
    quint64 height            = 0;
    // FIX-G10: target_height from daemon.get_info. 0 when the daemon is
    // already caught up with the network tip (some daemon builds omit
    // the field entirely in that case). > height when the daemon is
    // still syncing. Drives Dashboard.qml's real sync-progress pill and
    // ProgressBar; QML falls back to the old h/(h+10000) approximation
    // when this stays 0.
    quint64 target_height     = 0;
    quint64 tx_pool_size      = 0;
    QString version_string;   // free-form, may include codename + release tag;
                              // falls back to `status_line` if absent
    QString top_block_hash;
    QString cumulative_difficulty;
    QJsonObject extras;       // any additional fields returned by the binary

    bool isValid() const noexcept { return version != 0 || !nettype.isEmpty(); }
};

} // namespace judecoin_wallet::core::rpc

Q_DECLARE_METATYPE(judecoin_wallet::core::rpc::DaemonInfo)

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// DaemonClient
// -----------------------------------------------------------------------------
class DaemonClient : public HttpRpcClient
{
    Q_OBJECT

public:
    /// Polling / timeout defaults driven by V5.4.4 #2 (heartbeat 5 s) and
    /// the implicit "fail fast on dead daemon" requirement (3 s timeout,
    /// 3 consecutive failures = dead).
    static constexpr std::chrono::milliseconds kHeartbeatInterval{5'000};
    static constexpr std::chrono::milliseconds kPerRequestTimeout{3'000};
    static constexpr int kDeadAfterConsecutiveFailures = 3;

    explicit DaemonClient(QObject* parent = nullptr);
    DaemonClient(const QUrl& endpoint, QObject* parent = nullptr);
    ~DaemonClient() override;

    // -- heartbeat controls ----------------------------------------------------

    /// Start the 5 s heartbeat loop. The first poll fires immediately on
    /// the next event loop tick. Calling start() twice is a no-op while
    /// the loop is already running.
    void startHeartbeat();

    /// Stop the heartbeat loop. In-flight async calls (if any) are cancelled
    /// via HttpRpcClient::cancelAll(); they will still emit rpcFailed but
    /// no signal is forwarded to listeners past this point.
    void stopHeartbeat();

    /// True between start() and stop() (or destruction).
    bool isHeartbeatRunning() const noexcept { return heartbeatTimer_.isActive(); }

    // -- one-shot RPCs (sync wrappers; useful for startup probes) --------------

    /// Synchronous get_info - blocks the calling thread until the daemon
    /// replies or the per-request timeout fires. Throws RpcException on
    /// failure. Intended for startup probes / CLI; production UI uses the
    /// heartbeat path.
    DaemonInfo fetchInfo();

    /// Returns the most recently observed DaemonInfo. May be default-
    /// constructed (isValid()==false) if no successful poll has happened
    /// yet on this instance.
    const DaemonInfo& lastInfo() const noexcept { return lastInfo_; }

    /// True iff the daemon is currently considered alive (the most recent
    /// poll succeeded and we have not yet hit kDeadAfterConsecutiveFailures
    /// in a row). Mirrors the argument of the last daemonHealthChanged
    /// signal.
    bool isHealthy() const noexcept { return healthy_; }

    /// Number of consecutive get_info failures observed so far. Resets to 0
    /// on the next successful call. Exposed for tests + diagnostics.
    int consecutiveFailures() const noexcept { return consecutiveFailures_; }

    // -------------------------------------------------------------------------
    // FIX-G9b (2026-07-17 21:51): Q_PROPERTY helpers for QML bindings.
    //
    // Qt 6.6.3 QML cannot reliably access Q_GADGET struct members passed
    // through signal parameters (DaemonInfo via infoUpdated) without
    // explicit qmlRegisterUncreatableType, which is fragile across Qt
    // versions. Instead we expose simple-typed Q_PROPERTY entries on
    // DaemonClient itself so Dashboard.qml can use reactive property
    // bindings (the QML-idiomatic approach) instead of a Connections block
    // with a Q_GADGET signal parameter.
    //
    // Companion changes:
    //   - DaemonClient.cpp: emit NOTIFY signals in onRpcCompleted / onRpcFailed
    //   - Dashboard.qml: replace Connections block with property bindings
    // -------------------------------------------------------------------------
    Q_PROPERTY(quint64 lastHeight READ lastHeight NOTIFY lastHeightChanged)
    Q_PROPERTY(bool isSynchronized READ isSynchronized NOTIFY isSynchronizedChanged)
    Q_PROPERTY(bool isHealthy READ isHealthy NOTIFY daemonHealthChanged)
    // FIX-G10 (2026-07-18 01:13): mirror DaemonInfo::target_height
    // as a top-level Q_PROPERTY so Dashboard.qml's reactive bindings can
    // read it without round-tripping the Q_GADGET signal parameter.
    // Mirrors lastHeight() / isSynchronized() shape so the QML contract
    // is identical to FIX-G9b.
    Q_PROPERTY(quint64 targetHeight READ targetHeight NOTIFY targetHeightChanged)

    quint64 lastHeight() const noexcept { return lastInfo_.height; }
    // FIX-G11d (2026-07-18): check synchronized field first, then
    // fall back to target_height==0 as a sync-complete signal. Some daemon
    // builds (judecoind) omit "synchronized" from get_info but still set
    // target_height to 0 when caught up. Without this fallback the
    // Dashboard sync dot stays amber forever.
    // st-163 (2026-07-18): additional fallback — when target_height
    // is non-zero but height has caught up (height >= target), treat as
    // synced. Some daemon builds return target_height == current height
    // even when caught up, and the synchronized flag may still be false
    // on the first few get_info ticks. This catches that edge case.
    bool isSynchronized() const noexcept {
        if (lastInfo_.synchronized) return true;
        if (lastInfo_.target_height == 0 && lastInfo_.height > 0) return true;
        if (lastInfo_.target_height > 0 && lastInfo_.height >= lastInfo_.target_height) return true;
        return false;
    }
    // FIX-G10: returns the network tip height the daemon is catching up
    // to, or 0 when the daemon is already synced (or did not surface the
    // field). DaemonInfo::target_height carries the same value.
    quint64 targetHeight() const noexcept { return lastInfo_.target_height; }

signals:
    /// Emitted when daemon liveness transitions. Transitions only - we do
    /// not spam the signal on every successful poll. `healthy=false` means
    /// we have hit kDeadAfterConsecutiveFailures transport failures in a
    /// row without an intervening success.
    void daemonHealthChanged(bool healthy);

    /// Emitted on every successful get_info poll (the heartbeat tick).
    /// The QML layer can bind this to drive "synchronized: yes/no",
    /// "block height: N", "tx pool size: M" labels without polling itself.
    void infoUpdated(judecoin_wallet::core::rpc::DaemonInfo info);

    // FIX-G9b: simple-property NOTIFY signals for QML bindings.
    // Emitted alongside infoUpdated/daemonHealthChanged so QML can
    // reactively bind daemonClientObj.lastHeight / .isSynchronized
    // without Q_GADGET signal-parameter round-tripping.
    void lastHeightChanged();
    void isSynchronizedChanged();
    // FIX-G10: simple-property NOTIFY signal for the new targetHeight
    // Q_PROPERTY. Emitted in onRpcCompleted alongside lastHeightChanged()
    // / isSynchronizedChanged() so QML sees one coherent heartbeat tick.
    void targetHeightChanged();

private slots:
    /// Fired by heartbeatTimer_. Issues one async get_info and updates
    /// internal state when the response arrives.
    void onHeartbeatTick();

    /// Bridge from HttpRpcClient::rpcCompleted/rpcFailed to DaemonClient's
    /// own signals. We filter so only the heartbeat-driven call path
    /// advances `healthy_` and `consecutiveFailures_`; one-shot
    /// fetchInfo() callers see RpcException via their synchronous path.
    void onRpcCompleted(const QString& correlationId,
                        const QString& method,
                        const QJsonObject& result);
    void onRpcFailed(const QString& correlationId,
                     const QString& method,
                     const judecoin_wallet::core::rpc::RpcException error);

private:
    /// Build a DaemonInfo from a get_info result object. Returns false if
    /// the payload is missing mandatory fields.
    static bool parseInfo(const QJsonObject& result, DaemonInfo& out);

    QTimer heartbeatTimer_;
    QString pendingCorrelationId_;   // correlation id of the in-flight heartbeat call
    DaemonInfo lastInfo_;
    bool healthy_ = false;
    int  consecutiveFailures_ = 0;
    // True after we have emitted daemonHealthChanged(false) for the
    // current failure streak. Reset on the first successful get_info so
    // a future dead-transition re-emits. Lets the very first dead-edge
    // (from "we don't know yet") fire even when no healthy edge preceded
    // it, while still suppressing duplicate dead-edges on later ticks.
    bool deadEmittedForCurrentStreak_ = false;
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_DAEMON_CLIENT_H