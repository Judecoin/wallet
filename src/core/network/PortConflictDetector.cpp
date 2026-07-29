// =============================================================================
// Judecoin Wallet - PortConflictDetector (st-007) implementation
//
// All methods run on the caller thread (synchronous utility, no signals,
// no timers). QTcpServer listen/close cycles are blocking but quick on
// Linux loopback (~1ms per port).
// =============================================================================

#include "core/network/PortConflictDetector.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>

namespace judecoin_wallet::core::network {

// -----------------------------------------------------------------------------
// Static constants
// -----------------------------------------------------------------------------

const char* const PortConflictDetector::kSettingsKeyLastDaemonPort =
    "network/last_successful_daemon_port";

const char* const PortConflictDetector::kSettingsKeyLastWalletRpcPort =
    "network/last_successful_wallet_rpc_port";

const PortRange PortConflictDetector::kDefaultDaemonRange = {
    PortConflictDetector::kDefaultDaemonPort,
    PortConflictDetector::kDaemonRangeStart,
    PortConflictDetector::kDaemonRangeEnd,
};

const PortRange PortConflictDetector::kDefaultWalletRpcRange = {
    PortConflictDetector::kDefaultWalletRpcPort,
    PortConflictDetector::kWalletRpcRangeStart,
    PortConflictDetector::kWalletRpcRangeEnd,
};

// -----------------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------------

PortConflictDetector::PortConflictDetector()
    : ownedSettings_(std::make_unique<QSettings>())
    , settings_(ownedSettings_.get())
    , daemonRange_(kDefaultDaemonRange)
    , walletRpcRange_(kDefaultWalletRpcRange)
{
}

PortConflictDetector::~PortConflictDetector() = default;

// -----------------------------------------------------------------------------
// Settings seam
// -----------------------------------------------------------------------------

void PortConflictDetector::setSettings(QSettings* settings)
{
    // nullptr reverts to the owned default; any non-null value replaces it.
    settings_ = settings ? settings : ownedSettings_.get();
}

QSettings* PortConflictDetector::settings() const
{
    return settings_;
}

// -----------------------------------------------------------------------------
// Test seam: port range override
// -----------------------------------------------------------------------------

void PortConflictDetector::setPortRangeForTesting(ServiceKind kind,
                                                  const PortRange& range)
{
    // Validate: rangeStart <= defaultPort <= rangeEnd, and the inclusive
    // range must span exactly 10 ports (V5.4.4 #2 C).
    if (range.defaultPort < range.rangeStart ||
        range.defaultPort > range.rangeEnd) {
        return;  // default out of range - silently ignore
    }
    const quint16 span = static_cast<quint16>(range.rangeEnd - range.rangeStart + 1);
    if (span != 10) {
        return;  // V5.4.4 #2 C requires 10 backup ports
    }
    if (range.rangeStart == 0 || range.rangeEnd == 0) {
        return;  // would overlap with kOsAssigned sentinel
    }
    if (kind == ServiceKind::Daemon) {
        daemonRange_ = range;
    } else {
        walletRpcRange_ = range;
    }
}

// -----------------------------------------------------------------------------
// Per-service constant lookup
// -----------------------------------------------------------------------------

quint16 PortConflictDetector::defaultPortOf(ServiceKind kind) const
{
    return (kind == ServiceKind::Daemon)
               ? daemonRange_.defaultPort
               : walletRpcRange_.defaultPort;
}

quint16 PortConflictDetector::rangeStartOf(ServiceKind kind) const
{
    return (kind == ServiceKind::Daemon)
               ? daemonRange_.rangeStart
               : walletRpcRange_.rangeStart;
}

quint16 PortConflictDetector::rangeEndOf(ServiceKind kind) const
{
    return (kind == ServiceKind::Daemon)
               ? daemonRange_.rangeEnd
               : walletRpcRange_.rangeEnd;
}

const char* PortConflictDetector::settingsKeyOf(ServiceKind kind) const
{
    return (kind == ServiceKind::Daemon)
               ? kSettingsKeyLastDaemonPort
               : kSettingsKeyLastWalletRpcPort;
}

// -----------------------------------------------------------------------------
// Port probing primitives
//
// All three helpers share the same listen+close approach but answer
// different questions:
//
//   isPortAvailable(port)         : can WE bind here? (used by detectAvailablePort)
//   isPortBound(host, port)       : is SOMETHING already listening?
//                                    (used by ProcessManager st-113 probe step 1)
//   findPidByPort(host, port)     : who is listening? (st-113 probe step 2)
//
// The first two use QTcpServer (synchronous, ~1ms/port). The third uses
// /proc + an optional lsof fallback so it works in minimal containers.
// ---------------------------------------------------------------------------

namespace {

// Translate host -> QHostAddress::SpecialAddress for the well-known
// names Qt accepts. Anything else is parsed by QHostAddress directly.
// Empty host defaults to LocalHost (matches isPortAvailable()).
QHostAddress resolveHostForProbe(const QString& host)
{
    if (host.isEmpty()) return QHostAddress::LocalHost;
    return QHostAddress(host);
}

} // namespace

bool PortConflictDetector::isPortAvailable(quint16 port) const
{
    // Port 0 has a special meaning in our protocol ("OS-assigned"), not
    // "free" - reject it so callers don't accidentally treat listen(0)
    // as "step X succeeded with port 0".
    if (port == kOsAssigned) {
        return false;
    }

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, port)) {
        return false;
    }
    // We don't need the listener to persist; close immediately so the
    // port is free again before we return it to the caller.
    server.close();
    return true;
}

bool PortConflictDetector::isPortBound(const QString& host, quint16 port) const
{
    // Port 0 has the same special meaning here: never "bound" to anything
    // we can talk to.
    if (port == kOsAssigned) {
        return false;
    }

    const QHostAddress addr = resolveHostForProbe(host);
    if (addr.isNull()) {
        // Unknown host name - treat as "not bound" so the caller falls
        // through to spawn (better than blocking on a never-going-to-
        // resolve hostname).
        return false;
    }

    QTcpServer server;
    if (!server.listen(addr, port)) {
        // Couldn't bind => something else is already there (or
        // permissions refused - rare on loopback).
        return true;
    }
    server.close();
    return false;
}

qint64 PortConflictDetector::findPidByPort(const QString& /*host*/,
                                           quint16 port) const
{
    if (port == kOsAssigned) {
        return 0;
    }

    // Strategy 1: lsof (preferred when present). Output format with
    // `-F p` is one record per line, lowercase 'p' prefix + numeric PID.
    // We filter to TCP LISTEN state implicitly with `-sTCP:LISTEN`.
    //
    //   $ lsof -nP -iTCP:19061 -sTCP:LISTEN -F p
    //   p1234\n
    //
    // The -nP flags suppress DNS / port-name lookups (we already know the
    // port number) so the run stays fast (~5ms) even on busy hosts.
    {
        const QStringList lsofCandidates{
            QStringLiteral("/usr/bin/lsof"),
            QStringLiteral("/usr/sbin/lsof"),
            QStringLiteral("/bin/lsof"),
        };
        QString lsofPath;
        for (const QString& c : lsofCandidates) {
            if (QFileInfo::exists(c) && QFileInfo(c).isExecutable()) {
                lsofPath = c;
                break;
            }
        }
        if (!lsofPath.isEmpty()) {
            const QStringList args{
                QStringLiteral("-nP"),
                QStringLiteral("-iTCP:") + QString::number(port),
                QStringLiteral("-sTCP:LISTEN"),
                QStringLiteral("-F"),
                QStringLiteral("p"),
            };
            QProcess lsof;
            lsof.start(lsofPath, args);
            if (lsof.waitForStarted(200) && lsof.waitForFinished(1000)) {
                const QByteArray out = lsof.readAllStandardOutput();
                const QList<QByteArray> lines = out.split('\n');
                for (const QByteArray& line : lines) {
                    if (line.isEmpty()) continue;
                    if (line.startsWith('p')) {
                        bool ok = false;
                        const qint64 pid = line.mid(1).toLongLong(&ok);
                        if (ok && pid > 0) {
                            return pid;
                        }
                    }
                }
            }
            // If lsof returned 1 (nothing listening) or failed entirely,
            // fall through to /proc/net/tcp so we get a second chance.
        }
    }

    // Strategy 2: /proc/net/tcp + fd-symlink walk. We parse the
    // listener inode, then enumerate /proc/<pid>/fd/* looking for
    // `socket:[<inode>]` symlinks. First hit wins.
    //
    // The local address field in /proc/net/tcp is hex IP:port (port in
    // hex at the suffix; IP bytes preceding the colon, both in big-
    // endian network order). We match by port only - the daemon's local
    // bind is what matters, not the caller's "host" hint.
    QFile procTcp(QStringLiteral("/proc/net/tcp"));
    if (!procTcp.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;  // not Linux with /proc - give up gracefully.
    }

    const QString hexPort = QString::number(port, 16).toUpper()
                                .rightJustified(4, QLatin1Char('0'));
    quint64 listenInode = 0;
    const QByteArray contents = procTcp.readAll();
    procTcp.close();

    constexpr int kMaxProcLines = 4096;
    int procLines = 0;
    for (const QByteArray& rawLine : contents.split('\n')) {
        if (rawLine.isEmpty()) continue;
        if (++procLines > kMaxProcLines) break;
        // Columns are whitespace-separated; "sl st local rem ... ino".
        // Per `man 5 proc`, index 9 (10th field, post split) is the
        // socket inode.
        // QByteArray::split in Qt 6.6 only supports the single-char
        // overload; we filter empties via the size check below.
        const QList<QByteArray> cols = rawLine.split(' ');
        if (cols.size() < 10) continue;
        const QByteArray& local = cols[1];
        const QByteArray& st    = cols[3];
        const QByteArray& ino   = cols[9];
        if (st != "0A") continue;  // 0A = TCP_LISTEN
        const int colon = local.lastIndexOf(':');
        if (colon < 0) continue;
        const QByteArray portHex = local.mid(colon + 1);
        if (portHex.compare(hexPort.toLatin1(), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool ok = false;
        listenInode = ino.toULongLong(&ok);
        if (ok && listenInode > 0) break;
    }
    if (listenInode == 0) {
        return 0;
    }

    // Walk /proc/<pid>/fd/* and look for a symlink named
    // "socket:[<listenInode>]". First match wins.
    const QString targetFmt = QStringLiteral("socket:[%1]")
                                  .arg(listenInode);
    QDir procDir(QStringLiteral("/proc"));
    if (!procDir.exists()) return 0;
    const QStringList entries = procDir.entryList(
        QStringList{QStringLiteral("[0-9]*")},
        QDir::Dirs | QDir::NoDotAndDotDot);
    constexpr int kMaxPids = 2048;
    int scanned = 0;
    for (const QString& pidDir : entries) {
        if (++scanned > kMaxPids) break;
        bool ok = false;
        const qint64 pid = pidDir.toLongLong(&ok);
        if (!ok || pid <= 0) continue;
        const QString fdPath = QStringLiteral("/proc/") + pidDir
                                   + QStringLiteral("/fd");
        QDir fdDir(fdPath);
        if (!fdDir.exists()) continue;
        const QFileInfoList fdEntries = fdDir.entryInfoList(
            QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : fdEntries) {
            const QString tgt = fi.symLinkTarget();
            if (tgt == targetFmt) {
                return pid;
            }
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// QSettings read / write
// -----------------------------------------------------------------------------

quint16 PortConflictDetector::rememberedPort(ServiceKind kind) const
{
    if (settings_ == nullptr) {
        return 0;
    }
    const QVariant v = settings_->value(QLatin1String(settingsKeyOf(kind)));
    if (!v.isValid()) {
        return 0;
    }
    bool ok = false;
    const quint16 port = v.toUInt(&ok);
    if (!ok || port == kOsAssigned) {
        return 0;
    }
    // Range guard: ignore out-of-range ports (e.g. stale data from a
    // prior fork binary that used a different port band - V5.4.2 made
    // mainnet-only so anything outside the service range is bogus).
    const quint16 start = rangeStartOf(kind);
    const quint16 end   = rangeEndOf(kind);
    if (port < start || port > end) {
        return 0;
    }
    return port;
}

void PortConflictDetector::rememberSuccessfulPort(ServiceKind kind, quint16 port)
{
    if (settings_ == nullptr) {
        return;
    }
    if (port == kOsAssigned) {
        // Don't persist kernel-assigned ports - they are not stable
        // across launches and would only confuse the next run's step 1.
        settings_->remove(QLatin1String(settingsKeyOf(kind)));
        return;
    }
    settings_->setValue(QLatin1String(settingsKeyOf(kind)), port);
    settings_->sync();
}

// -----------------------------------------------------------------------------
// Algorithm steps
// -----------------------------------------------------------------------------

quint16 PortConflictDetector::tryRemembered(ServiceKind kind)
{
    const quint16 port = rememberedPort(kind);
    if (port == 0) {
        return 0;
    }
    if (isPortAvailable(port)) {
        return port;
    }
    return 0;
}

quint16 PortConflictDetector::tryDefault(ServiceKind kind)
{
    const quint16 port = defaultPortOf(kind);
    if (isPortAvailable(port)) {
        return port;
    }
    return 0;
}

quint16 PortConflictDetector::tryScanRange(ServiceKind kind)
{
    // Try every port in [rangeStart, rangeEnd] inclusive, skipping any
    // port already attempted in steps 1-2 (default + remembered). The
    // first available one wins.
    const quint16 start = rangeStartOf(kind);
    const quint16 end   = rangeEndOf(kind);
    const quint16 def   = defaultPortOf(kind);
    const quint16 rem   = rememberedPort(kind);

    for (quint16 p = start; p <= end; ++p) {
        if (p == def || p == rem) {
            continue;  // already tried in steps 1-2
        }
        if (isPortAvailable(p)) {
            return p;
        }
    }
    return 0;
}

quint16 PortConflictDetector::tryOsAssigned()
{
    // Port 0 in QTcpServer::listen() means "let the kernel pick a free
    // ephemeral port". serverPort() then returns the actual chosen port.
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, kOsAssigned)) {
        lastError_ = QStringLiteral(
            "OS-assigned port allocation failed (no free ephemeral port?)");
        return 0;
    }
    const quint16 port = server.serverPort();
    server.close();
    if (port == kOsAssigned) {
        // Defensive: shouldn't happen, but if QTcpServer gives us 0 back
        // something is very wrong.
        lastError_ = QStringLiteral(
            "OS-assigned port allocation returned 0 (kernel bug?)");
        return 0;
    }
    return port;
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------

quint16 PortConflictDetector::detectAvailablePort(ServiceKind kind)
{
    lastError_.clear();

    // Step 1: remembered default (V5.4.4 #2 C).
    if (quint16 p = tryRemembered(kind); p != kOsAssigned) {
        return p;
    }

    // Step 2: hardcoded default port.
    if (quint16 p = tryDefault(kind); p != kOsAssigned) {
        return p;
    }

    // Step 3: scan the 10-port range, skipping ports already tried.
    if (quint16 p = tryScanRange(kind); p != kOsAssigned) {
        return p;
    }

    // Step 4: OS-assigned (last resort).
    return tryOsAssigned();
}

} // namespace judecoin_wallet::core::network
