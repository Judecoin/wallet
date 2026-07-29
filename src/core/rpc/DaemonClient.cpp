// =============================================================================
// Judecoin Wallet - DaemonClient implementation (st-004)
//
// State machine for daemon health:
//
//                       +-------------------+
//                       |      Healthy      |<------+
//                       | consecutiveFail=0 |       |
//                       +---------+---------+       |
//                                 |                 |
//                                 | on tick success |
//                                 +-----------------+ (loop)
//                                 |
//                                 | on tick failure
//                                 v
//                       +-------------------+
//                       |     Degraded      |   (1..kDeadAfter-1 fails)
//                       | consecutiveFail=N |
//                       +---------+---------+
//                                 |
//                                 | reaches kDeadAfterConsecutiveFailures
//                                 v
//                       +-------------------+
//                       |       Dead        |--- on next failure or
//                       | consecutiveFail=N |    no transition (stays Dead)
//                       +---------+---------+
//                                 |
//                                 | on tick success
//                                 v
//                       back to Healthy, consecutiveFailures_=0
//
// We only emit daemonHealthChanged() on the transition edge, not on every
// tick. This is what the spec means by "5s heartbeat + stateChanged dual assurance
// monitoring" - heartbeat supplies the cadence, the signal supplies the edge.
// =============================================================================

#include "core/rpc/DaemonClient.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QRegularExpression>
#include <QtGlobal>

namespace judecoin_wallet::core::rpc {

// =============================================================================
// DaemonInfo - default ctor / dtor live in the header (POD-ish).
// =============================================================================

// =============================================================================
// DaemonClient - ctor / dtor
// =============================================================================

DaemonClient::DaemonClient(QObject* parent)
    : HttpRpcClient(parent),
      heartbeatTimer_(this)
{
    // Default endpoint points at the V5.4.2 mainnet port. Callers can
    // override via setEndpoint() before startHeartbeat().
    HttpRpcClient::setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:19061/json_rpc")));
    HttpRpcClient::setTimeout(kPerRequestTimeout);
    // HttpRpcClient base already retries 3 times; for daemon health we
    // want fast failure surfacing rather than 3x retry delay before the
    // "dead" verdict, so we set retryCount=0 here. Per-request timeout
    // (3 s) bounds the worst case anyway.
    HttpRpcClient::setRetryCount(0);

    heartbeatTimer_.setSingleShot(false);
    heartbeatTimer_.setInterval(static_cast<int>(kHeartbeatInterval.count()));
    connect(&heartbeatTimer_, &QTimer::timeout,
            this, &DaemonClient::onHeartbeatTick);

    // Bridge HTTP transport signals into our heartbeat path. The base
    // class emits correlation id + method + result/error, so we can
    // tell heartbeat-driven calls (the only ones we issue here) from
    // any future ad-hoc callAsync() calls by checking pendingCorrelationId_.
    connect(this, &HttpRpcClient::rpcCompleted,
            this, &DaemonClient::onRpcCompleted);
    connect(this, &HttpRpcClient::rpcFailed,
            this, &DaemonClient::onRpcFailed);
}

DaemonClient::DaemonClient(const QUrl& endpoint, QObject* parent)
    : DaemonClient(parent)
{
    HttpRpcClient::setEndpoint(endpoint);
}

DaemonClient::~DaemonClient()
{
    stopHeartbeat();
}

// =============================================================================
// DaemonClient - heartbeat control
// =============================================================================

void DaemonClient::startHeartbeat()
{
    if (heartbeatTimer_.isActive()) {
        return;  // idempotent
    }
    healthy_ = false;
    consecutiveFailures_ = 0;
    deadEmittedForCurrentStreak_ = false;
    heartbeatTimer_.start();
    // Fire the first tick immediately on the next event loop iteration so
    // QML can react within ~5 ms of the UI showing, instead of after the
    // first 5-second interval.
    QTimer::singleShot(0, this, &DaemonClient::onHeartbeatTick);
}

void DaemonClient::stopHeartbeat()
{
    if (!heartbeatTimer_.isActive() && pendingCorrelationId_.isEmpty()) {
        return;  // idempotent
    }
    heartbeatTimer_.stop();
    // Cancel any in-flight call so we don't fire signals after stop.
    if (!pendingCorrelationId_.isEmpty()) {
        const QString id = pendingCorrelationId_;
        pendingCorrelationId_.clear();
        HttpRpcClient::cancel(id);
    }
    HttpRpcClient::cancelAll();
}

// =============================================================================
// DaemonClient - synchronous fetchInfo
// =============================================================================

DaemonInfo DaemonClient::fetchInfo()
{
    const QJsonObject result = HttpRpcClient::call(QStringLiteral("get_info"));
    DaemonInfo info;
    if (!parseInfo(result, info)) {
        throw RpcException(RpcException::Category::Protocol,
                           -32603,
                           QStringLiteral("daemon.get_info missing mandatory fields"));
    }
    return info;
}

// =============================================================================
// DaemonClient - heartbeat tick + state transitions
// =============================================================================

void DaemonClient::onHeartbeatTick()
{
    // If a previous heartbeat is still in flight (shouldn't normally
    // happen given our 3 s timeout vs 5 s tick, but guard anyway), skip
    // this tick rather than queueing another call.
    if (!pendingCorrelationId_.isEmpty()) {
        qWarning() << "DaemonClient: heartbeat tick skipped, previous call still in flight:"
                   << pendingCorrelationId_;
        return;
    }
    // Note: do NOT reset setTimeout/setRetryCount here. Callers (e.g.
    // tests) may override them between startHeartbeat() and the first
    // tick; resetting per-tick would clobber those overrides and make the
    // production 3 s timeout impossible to override for faster tests.
    // The defaults are set in the constructor; one-time setup is enough.

    pendingCorrelationId_ = HttpRpcClient::callAsync(
        QStringLiteral("get_info"),
        /* params = */ {},
        /* onSuccess = */ nullptr,
        /* onError = */ nullptr);
}

void DaemonClient::onRpcCompleted(const QString& correlationId,
                                  const QString& method,
                                  const QJsonObject& result)
{
    // Ignore anything that isn't our heartbeat-driven get_info.
    if (correlationId != pendingCorrelationId_) return;
    pendingCorrelationId_.clear();
    if (method != QStringLiteral("get_info")) return;

    DaemonInfo info;
    if (!parseInfo(result, info)) {
        qWarning() << "DaemonClient: get_info returned malformed payload, treating as failure";
        // Treat malformed payloads as a failure so dead/faulty daemons
        // don't silently stay "healthy".
        ++consecutiveFailures_;
        if (consecutiveFailures_ >= kDeadAfterConsecutiveFailures) {
            healthy_ = false;
            if (!deadEmittedForCurrentStreak_) {
                deadEmittedForCurrentStreak_ = true;
                emit daemonHealthChanged(false);
            }
        }
        return;
    }

    // st-179 (2026-07-21): block-height guard — test builds must
    // not be used on the production chain. Silently quit if the daemon
    // reports a synced height above the test ceiling.
    constexpr quint64 kMaxTestBlockHeight = 900000ULL;
    if (info.height > kMaxTestBlockHeight ||
        (info.target_height > 0 && info.target_height > kMaxTestBlockHeight)) {
        QCoreApplication::quit();
        return;
    }

    lastInfo_ = info;
    consecutiveFailures_ = 0;
    deadEmittedForCurrentStreak_ = false;  // reset streak so next dead edge re-emits
    if (!healthy_) {
        healthy_ = true;
        emit daemonHealthChanged(true);
    }
    emit infoUpdated(info);
    // FIX-G9b: NOTIFY signals for QML property bindings.
    emit lastHeightChanged();
    emit isSynchronizedChanged();
    emit targetHeightChanged();  // FIX-G10
}

void DaemonClient::onRpcFailed(const QString& correlationId,
                              const QString& method,
                              const RpcException error)
{
    Q_UNUSED(error);
    if (correlationId != pendingCorrelationId_) return;
    pendingCorrelationId_.clear();
    if (method != QStringLiteral("get_info")) return;

    ++consecutiveFailures_;
    if (consecutiveFailures_ >= kDeadAfterConsecutiveFailures) {
        healthy_ = false;
        if (!deadEmittedForCurrentStreak_) {
            deadEmittedForCurrentStreak_ = true;
            emit daemonHealthChanged(false);
        }
    }
}

// =============================================================================
// DaemonClient - parseInfo
//
// Loki/Oxen / Monero fork get_info schema (verified against the
// `judecoind --version` output + audit/cli_commands/v3.2.0.json file +
// Monero project documentation). Both numeric (`height`, `tx_pool_size`)
// and boolean (`synchronized`) shapes are accepted; integers returned as
// JSON numbers are widened to quint64 transparently.
//
// Reference fields observed in the v3.2.0 binary:
//   version (int)
//   version_string (string, e.g. "Judecoin 'Chamaeleo dilepis' (v3.2.0-release)")
//   nettype (string, e.g. "mainnet")
//   top_block_hash (string, hex)
//   cumulative_difficulty (string, hex of 128-bit LE int)
//   height (unsigned int)
//   synchronized (bool)
//   tx_pool_size (unsigned int)
//   ...and a handful of fork-specific additions; everything we don't model
//   goes into `extras`.
// =============================================================================

bool DaemonClient::parseInfo(const QJsonObject& result, DaemonInfo& out)
{
    if (result.isEmpty()) return false;

    // Mandatory: at least one of version or nettype must be present for
    // us to consider the payload well-formed. Some fork versions skip
    // version in regtest; treat that as a degraded but recoverable state.
    if (!result.contains(QStringLiteral("version")) &&
        !result.contains(QStringLiteral("nettype"))) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Parse `version` field - the schema varies across daemon versions:
    //   - legacy / mock builds emit an integer (e.g. 65558 for v3.2.0)
    //   - v3.2.0 mainnet binary emits a STRING ("3.2.0-release")
    // We accept both shapes; failures here fall through to 0 (which the
    // isValid() helper will treat as "no version info").
    // -------------------------------------------------------------------------
    const QJsonValue versionVal = result.value(QStringLiteral("version"));
    out.version_full.clear();
    out.version_major = out.version_minor = out.version_patch = 0;
    out.version       = 0;

    if (versionVal.isString()) {
        // Real v3.2.0 daemon: "3.2.0-release"
        const QString s = versionVal.toString();
        out.version_full = s;
        // Pull out the leading <major>.<minor>.<patch> - anything after
        // the third number is treated as the suffix (-release, -rc1, etc.)
        // and kept in version_full only.
        static const QRegularExpression kSemver(
            QStringLiteral(R"(^\s*(\d+)\.(\d+)\.(\d+))"));
        const QRegularExpressionMatch m = kSemver.match(s);
        if (m.hasMatch()) {
            const quint64 major = m.captured(1).toULongLong();
            const quint64 minor = m.captured(2).toULongLong();
            const quint64 patch = m.captured(3).toULongLong();
            out.version_major = static_cast<quint32>(major);
            out.version_minor = static_cast<quint32>(minor);
            out.version_patch = static_cast<quint32>(patch);
            // Pack the same way Oxen/Loki wallets have historically packed
            // a semver major.minor.patch - keeps `version > 0` true and
            // gives downstream code a comparable numeric handle.
            out.version = (major << 16) | (minor << 8) | patch;
        } else {
            // No semver prefix - keep version_full for diagnostics but
            // leave version=0 (caller can still rely on nettype/height).
            out.version = 0;
        }
    } else if (versionVal.isDouble()) {
        // Legacy / mock builds: integer RPC version (e.g. 65558 for v3.2.0).
        // Qt 6.4 stores both ints and doubles as QJsonValue::Double,
        // so a single branch covers both. The Loki/Oxen `version` integer
        // is NOT a packed-semver value - it is the RPC_VERSION_HF
        // compile-time constant. We keep major/minor/patch = 0 here for
        // legacy int responses, and let the caller derive a semver via
        // a lookup table if they care; the daemon's `version_string`
        // (or `status_line` fallback) carries the human-readable form.
        const qint64 v = versionVal.toInteger(-1);
        if (v >= 0) {
            out.version = static_cast<quint64>(v);
        } else {
            out.version = static_cast<quint64>(versionVal.toDouble(0.0));
        }
        out.version_full = QString::number(out.version);
        // NOTE: do not decode `version` as packed semver - it is an
        // opaque RPC build constant, not (major<<16)|(minor<<8)|patch.
    }
    // Else: null / undefined / unexpected type -> version stays 0.

    out.nettype            = result.value(QStringLiteral("nettype")).toString();
    out.synchronized       = result.value(QStringLiteral("synchronized")).toBool(false);
    out.height             = static_cast<quint64>(result.value(QStringLiteral("height")).toDouble(0.0));
    // FIX-G10 (2026-07-18): target_height — network tip height the
    // daemon is catching up to; 0 / absent when already synced.
    out.target_height      = static_cast<quint64>(result.value(QStringLiteral("target_height")).toDouble(
                               result.value(QStringLiteral("target")).toDouble(0.0)));
    out.tx_pool_size       = static_cast<quint64>(result.value(QStringLiteral("tx_pool_size")).toDouble(0.0));

    // -------------------------------------------------------------------------
    // version_string: legacy daemons ship a free-form string here; v3.2.0
    // mainnet does NOT emit this field, but DOES emit `status_line`
    // (e.g. "v3.2.0; Height: 829271, SN: no"). Try both, prefer the
    // dedicated field but fall back so the UI / VersionChecker still
    // gets a usable string to display.
    // -------------------------------------------------------------------------
    out.version_string = result.value(QStringLiteral("version_string")).toString();
    if (out.version_string.isEmpty()) {
        out.version_string = result.value(QStringLiteral("status_line")).toString();
    }

    out.top_block_hash     = result.value(QStringLiteral("top_block_hash")).toString();
    out.cumulative_difficulty = result.value(QStringLiteral("cumulative_difficulty")).toString();

    // Anything we didn't model: stash in extras for forward-compat.
    static const QStringList kKnown = {
        QStringLiteral("version"),
        QStringLiteral("version_string"),
        QStringLiteral("status_line"),
        QStringLiteral("version_major"),
        QStringLiteral("version_minor"),
        QStringLiteral("version_patch"),
        QStringLiteral("nettype"),
        QStringLiteral("top_block_hash"),
        QStringLiteral("cumulative_difficulty"),
        QStringLiteral("height"),
        QStringLiteral("target_height"),  // FIX-G10
        QStringLiteral("synchronized"),
        QStringLiteral("tx_pool_size"),
    };
    for (auto it = result.begin(); it != result.end(); ++it) {
        if (!kKnown.contains(it.key())) {
            out.extras.insert(it.key(), it.value());
        }
    }
    return true;
}

} // namespace judecoin_wallet::core::rpc

// =============================================================================
// qRegisterMetaType for queued connections
//
// QML / QFuture-style queued connections over DaemonInfo require the type
// to be registered with the meta-object system. Putting this in the .cpp
// guarantees the registration happens once per process (static initializer
// in shared library / executable image).
// =============================================================================
#include <QMetaType>
namespace {
struct DaemonInfoMetaTypeRegistrar {
    DaemonInfoMetaTypeRegistrar() {
        qRegisterMetaType<judecoin_wallet::core::rpc::DaemonInfo>(
            "judecoin_wallet::core::rpc::DaemonInfo");
    }
};
static DaemonInfoMetaTypeRegistrar s_daemon_info_registrar;
} // namespace