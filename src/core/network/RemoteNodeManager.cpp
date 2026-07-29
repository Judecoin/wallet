// =============================================================================
// RemoteNodeManager - implementation
// =============================================================================

#include "core/network/RemoteNodeManager.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace judecoin_wallet::core::network {

RemoteNodeManager::RemoteNodeManager(judecoin_wallet::core::settings::SettingsManager* settings,
                                     QObject* parent)
    : QObject(parent)
    , settings_(settings)
{
    // Initial resolution happens in the constructor so the first
    // StartupManager binding sees a sane value (never "" / 0 with the
    // Source still unknown).
    refresh();
}

void RemoteNodeManager::refresh()
{
    // Local is the default. We only flip to Preset/Custom when the
    // user explicitly enabled a remote node AND the values are usable.
    QString newHost;
    int     newPort = 0;
    Source  newSource = Source::Local;

    if (settings_ != nullptr) {
        if (settings_->isCustomRemote()) {
            const QString h = settings_->remoteDaemonHost();
            const int p = settings_->remoteDaemonPort();
            // Custom mode requires both host AND a valid port. Empty
            // host means "Custom, but I haven't typed anything yet" -
            // fall back to Local so the wallet still starts.
            if (!h.isEmpty() && p > 0 && p <= 65535) {
                newHost = h;
                newPort = p;
                newSource = Source::Custom;
            }
        } else {
            // Preset mode: look up preset id in the table. Empty id ==
            // "no preset selected yet" == Local.
            const QString id = settings_->presetNode();
            if (!id.isEmpty()) {
                if (auto preset =
                        judecoin_wallet::core::settings::findRemotePreset(id)) {
                    // std::optional<RemotePresetNode> owns the value, no
                    // dangling pointer into the temp QList produced by
                    // remotePresetNodes() (gf-005 fix).
                    newHost = preset->host;
                    newPort = preset->port;
                    newSource = Source::Preset;
                }
            }
        }
    }

    // Only emit + cache when something actually changed. This matches
    // SettingsManager's "set same value -> no signal" rule and keeps
    // QML from re-rendering needlessly.
    if (newHost == activeHost_ &&
        newPort == activePort_ &&
        newSource == activeSource_) {
        return;
    }
    activeHost_   = newHost;
    activePort_   = newPort;
    activeSource_ = newSource;
    emit nodeChanged(activeHost_, activePort_, activeSourceString());
}

QString RemoteNodeManager::activeSourceString() const
{
    switch (activeSource_) {
        case Source::Local:  return QStringLiteral("local");
        case Source::Preset: return QStringLiteral("preset");
        case Source::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("local");
}

QString RemoteNodeManager::activeLabel() const
{
    switch (activeSource_) {
        case Source::Local:
            return QStringLiteral("Local Daemon (127.0.0.1)");
        case Source::Preset: {
            if (settings_ == nullptr) {
                return QStringLiteral("Preset");
            }
            const QString id = settings_->presetNode();
            if (auto p =
                    judecoin_wallet::core::settings::findRemotePreset(id)) {
                return p->label;
            }
            return QStringLiteral("Preset");
        }
        case Source::Custom:
            return QStringLiteral("Custom: %1:%2")
                .arg(activeHost_.isEmpty() ? QStringLiteral("?") : activeHost_)
                .arg(activePort_);
    }
    return QStringLiteral("Local Daemon (127.0.0.1)");
}

QUrl RemoteNodeManager::endpointUrl() const
{
    QUrl u;
    u.setScheme(QStringLiteral("http"));
    u.setHost(activeHost_.isEmpty()
                  ? QStringLiteral("127.0.0.1")
                  : activeHost_);
    u.setPort(activePort_ > 0 ? activePort_ : 19061);
    u.setPath(QStringLiteral("/json_rpc"));
    return u;
}

// ---------------------------------------------------------------------------
// st-111: testConnection - one-shot probe categorised into the 4-way
// ConnectionTestResult enum. Implementation strategy:
//
//   (1) Use QTcpSocket to attempt a synchronous TCP connect to
//       host:port with a short (kTestConnectionTimeoutMs) deadline.
//       If connect() fails with "Connection refused" -> ConnectionRefused.
//       If the connect times out (no SYN/ACK)                 -> Timeout.
//   (2) On TCP success, fire a minimal /json_rpc POST
//       {"jsonrpc":"2.0","id":"0","method":"get_info","params":{}}
//       via QNetworkAccessManager with a matching overall deadline.
//   (3) Parse the response body as JSON, pull
//       result.version_string, feed it through VersionChecker::isSupported.
//       Empty / unparseable                  -> ConnectionRefused
//       Recognised but unsupported version   -> VersionMismatch
//       Recognised and supported             -> Ok
//
// The QML side calls this from a button slot and switches on the
// returned enum to render one of four banners ("Reachable" /
// "Refused - no daemon listening" / "Timeout - daemon did not
// reply" / "Unsupported version - please upgrade"). The 1500 ms
// total ceiling keeps the UI thread responsive on slow remotes.
//
// Note: this function is intentionally synchronous. The QML call
// site is the "Test Connection" button - a one-shot diagnostic
// action, not a streaming health check. A one-shot synchronous
// HTTP roundtrip is the simplest correct shape; the alternative
// (callAsync + signal wiring) would force every caller to deal
// with the async lifetime. We trade a ~1s UI freeze for one less
// async lifecycle to manage.
// ---------------------------------------------------------------------------
RemoteNodeManager::ConnectionTestResult
RemoteNodeManager::testConnection(const QString& host, int port) const
{
    // Coerce host/port to a usable shape. The QML layer passes
    // user-typed strings so we have to defend against empty host /
    // out-of-range port just like RemoteNodeManager::refresh() does
    // for the persisted values.
    if (host.isEmpty() || port <= 0 || port > 65535) {
        // Bad input -> classify as refused so the user sees a clear
        // "the address you typed is wrong" banner rather than a
        // hung UI waiting on a never-going-to-arrive TCP connect.
        return ConnectionTestResult::ConnectionRefused;
    }

    // (1) TCP probe with a short deadline. We split the deadline
    // across the TCP phase and the HTTP phase so neither gets the
    // full 1500ms - a refused TCP connect returns fast so the
    // user gets immediate feedback, while a slow-but-eventually-
    // working HTTP response gets a fair share.
    constexpr int kTcpTimeoutMs = 500;

    QTcpSocket socket;
    socket.connectToHost(host, static_cast<quint16>(port));
    if (!socket.waitForConnected(kTcpTimeoutMs)) {
        // Distinguish refused (host reached, no listener) from
        // timeout (host unreachable, slow link). waitForConnected's
        // error() returns QAbstractSocket::ConnectionRefusedError
        // for the former; everything else (timeout, host not found,
        // network unreachable) we classify as Timeout so the QML
        // banner reads "daemon did not respond" (covers both no
        // route and no answer).
        const QAbstractSocket::SocketError se = socket.error();
        if (se == QAbstractSocket::ConnectionRefusedError) {
            return ConnectionTestResult::ConnectionRefused;
        }
        return ConnectionTestResult::Timeout;
    }
    // TCP is up - close the probe socket so the HTTP stack can
    // re-open the port cleanly (QNetworkAccessManager will issue
    // its own connect). We never reuse the QTcpSocket for the HTTP
    // exchange; NAM has its own connection management.
    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.waitForDisconnected(50);
    }

    // (2) HTTP POST /json_rpc {"method":"get_info"}. NAM + reply
    // pair is owned on the stack with a matching deadline; we
    // block in a nested event loop so QNetworkAccessManager can
    // deliver its signals. The remaining slice of the 1500ms
    // budget after the TCP probe is what the HTTP phase gets.
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(host);
    endpoint.setPort(port);
    endpoint.setPath(QStringLiteral("/json_rpc"));

    QNetworkAccessManager nam;
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setRawHeader(QStringLiteral("User-Agent").toUtf8(),
                     QStringLiteral("JudecoinWallet/1.0 (Qt; RemoteNodeManager)").toUtf8());
    req.setTransferTimeout(static_cast<int>(kTestConnectionTimeoutMs) - kTcpTimeoutMs);

    const QByteArray body =
        QStringLiteral("{\"jsonrpc\":\"2.0\",\"id\":\"0\","
                       "\"method\":\"get_info\",\"params\":{}}").toUtf8();
    QNetworkReply* reply = nam.post(req, body);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // Outer safety timer guards against a stuck NAM (e.g. proxy
    // DNS hang) that never delivers finished(). Without it the
    // QML button would freeze the UI thread for the full
    // QNetworkRequest::transferTimeout + whatever the OS queues.
    QTimer safety;
    safety.setSingleShot(true);
    QObject::connect(&safety, &QTimer::timeout, &loop, &QEventLoop::quit);
    safety.setInterval(kTestConnectionTimeoutMs);
    safety.start();
    loop.exec();

    // If the reply never fired and the safety timer killed the
    // loop, we report Timeout - we have no body to parse so any
    // other classification would be a guess.
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return ConnectionTestResult::Timeout;
    }

    // (3) Body parse + version-string classification.
    const QByteArray respBody = reply->readAll();
    const QNetworkReply::NetworkError ne = reply->error();
    reply->deleteLater();
    if (ne != QNetworkReply::NoError) {
        // Transport-level error from NAM (host reset, malformed
        // response, 5xx body). Treat as refused - the user sees
        // "the daemon at this address is not responding as a
        // judecoind". Timeout is reserved for "we sent nothing
        // came back"; a NAM-level error means we DID hear back,
        // just not sensibly.
        return ConnectionTestResult::ConnectionRefused;
    }

    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(respBody, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return ConnectionTestResult::ConnectionRefused;
    }
    const QJsonObject root = doc.object();
    const QJsonObject result = root.value(QStringLiteral("result")).toObject();
    const QString versionString = result.value(QStringLiteral("version_string")).toString();
    if (versionString.isEmpty()) {
        // Daemon replied JSON without the version_string field -
        // this is the shape a wrong-port service (e.g. an HTTP
        // proxy or our own wallet-rpc on the wrong port) returns.
        // Surface as refused so the user can tell apart "daemon is
        // up but old" (VersionMismatch) from "service at this
        // address isn't a judecoind at all" (ConnectionRefused).
        return ConnectionTestResult::ConnectionRefused;
    }
    if (!judecoin_wallet::core::version::VersionChecker::isSupported(versionString)) {
        return ConnectionTestResult::VersionMismatch;
    }
    return ConnectionTestResult::Ok;
}

} // namespace judecoin_wallet::core::network
