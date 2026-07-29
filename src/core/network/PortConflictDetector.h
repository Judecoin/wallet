// =============================================================================
// Judecoin Wallet - PortConflictDetector (st-007, st-113)
//
// Detects an available TCP port for judecoind / judecoin-wallet-rpc using
// a 4-step algorithm. Called once per service at startup; the result is
// passed to ProcessManager (st-006) as the --rpc-bind-port argument for the
// respective binary.
//
// 4-step algorithm (V5.4.1 + V5.4.4 #2 C decision)
// --------------------------------------------
//   1. remembered default - read QSettings key
//                            (network/last_successful_daemon_port or
//                            network/last_successful_wallet_rpc_port) and
//                            try that port first. This is the hot path:
//                            the port that worked last time almost always
//                            still works.
//   2. default port       - fall back to 19061 (daemon) or 19071 (wallet-rpc).
//   3. scan 10 backup     - try the 10-port range 19061..19070 (daemon) or
//                            19071..19080 (wallet-rpc) in order, skipping
//                            any port already tried in steps 1-2.
//   4. OS-assigned        - last resort: let the kernel pick a free
//                            ephemeral port by listening on port 0.
//
// The probing primitive is QTcpServer::listen(QHostAddress::LocalHost, port).
// If listen() returns true the port is available; we immediately close()
// and return it. If listen() returns false the port is taken.
//
// mainnet-only (V5.4.2): the 10-port range is the only allowed band; we
// never look outside it before falling back to OS-assigned.
//
// Persistence (V5.4.4 #2 C)
// --------------------------
// rememberSuccessfulPort(kind, port) writes the chosen port to QSettings
// AFTER the caller has confirmed the binary actually started listening.
// The next launch reads it via the algorithm's step 1. A mid-startup
// crash therefore never persists an unusable port.
//
// st-113 additions: local-daemon detection
// ----------------------------------------
// To avoid silent port conflicts when an external daemon is already
// running on the canonical port, ProcessManager calls
// `isPortBound(host, port)` before spawning. If the port is taken we
// also resolve the owning PID via `findPidByPort(host, port)` (lsof on
// Linux; /proc/net/tcp fallback). The probe is synchronous and
// host/port are explicit so the caller is not tied to the configured
// service range.
//
// V1.0 scope
// ----------
// This module only probes ports. It does NOT spawn anything (that is
// ProcessManager's job, st-006). It does NOT hold any QProcess. It is
// safe to call from the UI thread.
//
// Synchronous only. Probing takes ~1ms per port on Linux loopback.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_NETWORK_PORT_CONFLICT_DETECTOR_H
#define JUDECOIN_WALLET_CORE_NETWORK_PORT_CONFLICT_DETECTOR_H

#include <QSettings>
#include <QString>

#include <memory>

namespace judecoin_wallet::core::network {

// -----------------------------------------------------------------------------
// ServiceKind - which backend service we are picking a port for.
//
// Different services have different default ports and ranges (V5.4.2
// mainnet-only):
//
//   Daemon     -> default 19061, range [19061, 19070] (10 ports)
//   WalletRpc  -> default 19071, range [19071, 19080] (10 ports)
//
// V5.4.2 explicitly removed regtest/testnet/stagenet, so no other
// ServiceKind values are needed for V1.0.
// -----------------------------------------------------------------------------
enum class ServiceKind {
    Daemon,       // judecoind              -> default 19061, range 19061-19070
    WalletRpc,    // judecoin-wallet-rpc    -> default 19071, range 19071-19080
};

// -----------------------------------------------------------------------------
// PortRange - declarative description of a service's port band.
//
// Production code never constructs one of these directly; the defaults
// baked into PortConflictDetector are the V5.4.2 mainnet values.
// Tests may construct arbitrary ranges to exercise the algorithm in
// environments where the production ports are already bound.
// -----------------------------------------------------------------------------
struct PortRange {
    quint16 defaultPort = 0;   // canonical default (step 2)
    quint16 rangeStart  = 0;   // inclusive lower bound of the 10-port scan
    quint16 rangeEnd    = 0;   // inclusive upper bound of the 10-port scan
};

// -----------------------------------------------------------------------------
// PortConflictDetector
//
// Stateless utility (other than the QSettings cache and the per-instance
// port range). Designed to be created once at startup, called once per
// service, then discarded.
// -----------------------------------------------------------------------------
class PortConflictDetector
{
public:
    // V5.4.2 mainnet-only port constants.
    static constexpr quint16 kDefaultDaemonPort     = 19061;
    static constexpr quint16 kDefaultWalletRpcPort  = 19071;

    // V5.4.4 #2 C: 10 backup ports per service (inclusive ranges).
    static constexpr quint16 kDaemonRangeStart      = 19061;
    static constexpr quint16 kDaemonRangeEnd        = 19070;
    static constexpr quint16 kWalletRpcRangeStart   = 19071;
    static constexpr quint16 kWalletRpcRangeEnd     = 19080;

    // Sentinel: port 0 means "OS-assigned" (kernel picks).
    static constexpr quint16 kOsAssigned            = 0;

    // QSettings keys (V5.4.4 #2 C). These live under the JudecoinWallet
    // organisation / application (set in main.cpp).
    //
    //   ~/.config/JudecoinWallet/JudecoinWallet.conf
    //     [network]
    //     last_successful_daemon_port=19063
    //     last_successful_wallet_rpc_port=19073
    static const char* const kSettingsKeyLastDaemonPort;
    static const char* const kSettingsKeyLastWalletRpcPort;

    // The default ranges for V5.4.2 mainnet-only.
    static const PortRange kDefaultDaemonRange;
    static const PortRange kDefaultWalletRpcRange;

    // Constructs a detector with V5.4.2 mainnet ranges and its own
    // QSettings instance (using QCoreApplication::organizationName /
    // applicationName set by main.cpp).
    PortConflictDetector();
    virtual ~PortConflictDetector();

    PortConflictDetector(const PortConflictDetector&)            = delete;
    PortConflictDetector& operator=(const PortConflictDetector&) = delete;

    // -------------------------------------------------------------------------
    // Core algorithm
    // -------------------------------------------------------------------------

    // Run the 4-step algorithm for the given service.
    //
    // Returns:
    //   - the discovered port (> 0) if any of steps 1-3 succeeded;
    //   - the OS-assigned port if step 4 succeeded (also > 0);
    //   - 0 only on catastrophic failure (kernel refused every port,
    //     which on Linux basically means the ephemeral range is full -
    //     ~16384 ports - and is essentially never reached in practice).
    //
    // In every realistic case this returns > 0. lastError() is empty on
    // success and populated on the rare failure.
    virtual quint16 detectAvailablePort(ServiceKind kind);

    // Persist a successful port. Callers MUST invoke this AFTER the
    // binary has actually started listening and is reachable. A failure
    // to start should NOT pollute QSettings with an unusable port.
    virtual void rememberSuccessfulPort(ServiceKind kind, quint16 port);

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------

    // Last error description (empty if last call succeeded).
    QString lastError() const { return lastError_; }
    void    clearLastError()  { lastError_.clear(); }

    // -------------------------------------------------------------------------
    // Lower-level helpers (exposed for tests + future direct callers)
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // Local-daemon detection (st-113)
    //
    // Both helpers are synchronous and stay on the caller thread. They are
    // deliberately separated from `isPortAvailable()` so the spawn path
    // (detectAvailablePort) and the detect-existing path can be tested
    // independently.
    // -------------------------------------------------------------------------

    /// True if QTcpServer CANNOT bind to (host, port). The inverse of
    /// `isPortAvailable()`: it is the "something is already listening
    /// here" check that ProcessManager.startDaemon() uses as step 1 of
    /// the 2-step external-daemon probe (st-113).
    ///
    /// Implementation: same QTcpServer-listen-then-close trick as
    /// `isPortAvailable()`. A non-empty `host` triggers a concrete
    /// QHostAddress lookup; an empty host defaults to LocalHost (matching
    /// the rest of the API).
    ///
    /// Returns false on port 0 (matches `isPortAvailable()` - port 0
    /// means "OS-assigned" in our protocol, not "bound").
    bool isPortBound(const QString& host, quint16 port) const;

    /// Best-effort lookup of the PID currently bound to (host, port).
    /// Returns 0 when no PID could be resolved.
    ///
    /// Strategy:
    ///   1. `lsof -nP -iTCP:<port> -sTCP:LISTEN -F p` (Linux standard,
    ///      skipped if `lsof` is missing).
    ///   2. `/proc/net/tcp` parse - same hex-encoded `(local_ip:port)`
    ///      tuple, then walk `/proc/<pid>/fd/*` for an `socket:[inode]`
    ///      symlink whose inode matches the listening socket.
    ///
    /// Both paths are read-only, ~10ms on Linux.
    qint64 findPidByPort(const QString& host, quint16 port) const;

    // True if QTcpServer can bind to (LocalHost, port). Port 0 is
    // rejected here (it means "OS-assigned" in our protocol, not "free").
    bool isPortAvailable(quint16 port) const;

    // Read the currently-remembered port without consuming it. Returns
    // 0 if QSettings has no value, the value is malformed, or the value
    // is outside the service's range (e.g. stale data from a prior fork
    // binary that used a different port band).
    quint16 rememberedPort(ServiceKind kind) const;

    // -------------------------------------------------------------------------
    // Test seams
    // -------------------------------------------------------------------------

    // Replace the QSettings instance. Pass nullptr to revert to the
    // default (the one constructed at construction time).
    void setSettings(QSettings* settings);
    QSettings* settings() const;

    // Override the port range for a service. **TEST-ONLY.** Production
    // code MUST NOT call this; it exists so unit tests can exercise the
    // algorithm in environments where the V5.4.2 mainnet ports (19061,
    // 19071, ...) are already bound by other processes (e.g. a live
    // daemon / wallet-rpc on the same machine).
    //
    // `range` must satisfy range.rangeStart <= range.defaultPort
    //                            <= range.rangeEnd.
    // The full inclusive range [rangeStart, rangeEnd] must span exactly
    // 10 ports (V5.4.4 #2 C). Invalid inputs are silently ignored.
    void setPortRangeForTesting(ServiceKind kind, const PortRange& range);

private:
    // Static helpers - per-service constants in one place. Read from
    // per-instance members so test overrides take effect.
    quint16      defaultPortOf(ServiceKind kind) const;
    quint16      rangeStartOf(ServiceKind kind)  const;
    quint16      rangeEndOf(ServiceKind kind)    const;
    const char*  settingsKeyOf(ServiceKind kind) const;

    // Each step of the 4-step algorithm; returns chosen port or 0 on miss.
    quint16 tryRemembered(ServiceKind kind);
    quint16 tryDefault(ServiceKind kind);
    quint16 tryScanRange(ServiceKind kind);
    quint16 tryOsAssigned();

    // Owned default QSettings (created on first access in ctor). Always
    // alive for the lifetime of the detector - if the user provides an
    // override via setSettings() this stays alive but is no longer used.
    std::unique_ptr<QSettings> ownedSettings_;

    // Either ownedSettings_.get() (default) or an externally-supplied
    // pointer (tests).
    QSettings* settings_ = nullptr;

    // Per-service port range. Initialised to V5.4.2 mainnet; tests may
    // override via setPortRangeForTesting().
    PortRange daemonRange_;
    PortRange walletRpcRange_;

    // Last error message (empty on success).
    QString lastError_;
};

} // namespace judecoin_wallet::core::network

#endif // JUDECOIN_WALLET_CORE_NETWORK_PORT_CONFLICT_DETECTOR_H
