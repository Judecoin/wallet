// =============================================================================
// Judecoin Wallet - HttpRpcClient implementation (st-003)
//
// Two execution modes share a single dispatch path:
//
//   sync  call()     ->  QEventLoop + callbacks, blocks the calling thread
//   async callAsync  ->  lambdas captured in Pending, fires when the reply
//                        completes; signals rpcCompleted / rpcFailed emitted
//
// Retry policy: transport errors (connection refused, timeout, EOF, DNS)
// are retried up to config_.retryCount additional times with a fixed
// retryDelay between attempts. JSON-RPC server errors and protocol errors
// are returned to the caller without retry; they indicate a real problem
// with the request, not a flaky network.
//
// P0-04 (2026-07-23): hardening against UAF in timer callbacks and
// non-thread-safe pending access.  See audit/2026-07-21-gemma4-12b/P0-04-*.md
// for the full rationale.
// =============================================================================

#include "core/rpc/HttpRpcClient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRecursiveMutex>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include <utility>

namespace judecoin_wallet::core::rpc {

// =============================================================================
// RpcException
// =============================================================================

RpcException::RpcException(Category category,
                           int code,
                           QString message,
                           QJsonObject data)
    : category_(category)
    , code_(code)
    , message_(std::move(message))
    , data_(std::move(data))
{
}

QString RpcException::toString() const
{
    return QStringLiteral("%1 (code=%2): %3")
        .arg(categoryName(category_))
        .arg(code_)
        .arg(message_);
}

bool RpcException::isTransient() const noexcept
{
    if (category_ != Category::Transport) return false;
    // -32099 = caller cancel; everything else in the transport range is retryable.
    return code_ != -32099;
}

QString RpcException::categoryName(Category c)
{
    switch (c) {
    case Category::Transport: return QStringLiteral("transport");
    case Category::Protocol:  return QStringLiteral("protocol");
    case Category::Server:    return QStringLiteral("server");
    case Category::Cancel:    return QStringLiteral("cancel");
    case Category::Unknown:   return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

// =============================================================================
// HttpRpcClient - private state
// =============================================================================

struct HttpRpcClient::Private
{
    struct Pending {
        QString method;
        QJsonObject params;
        CompletionCallback onSuccess;
        ErrorCallback onError;
        // FIX-P0-04-Issue1: use a guarded pointer for the reply so that any
        // queued or cross-thread timer callback can safely test validity
        // before touching the QNetworkReply.
        QPointer<QNetworkReply> reply;
        QTimer* timer = nullptr;
        int attempt = 0;
        bool finished = false;
        bool sync = false;
        QEventLoop* syncLoop = nullptr;
    };

    QHash<QString, Pending> pending;
    // FIX-P0-04-Issue2: guard every read/write of `pending` with this mutex.
    // QRecursiveMutex is used because the sync path may re-enter while a
    // nested QEventLoop processes the QNetworkReply::finished signal.
    QRecursiveMutex mutex;
};

// =============================================================================
// HttpRpcClient - ctor / dtor
// =============================================================================

HttpRpcClient::HttpRpcClient(QObject* parent)
    : QObject(parent)
    , d_(std::make_unique<Private>())
{
    nam_ = new QNetworkAccessManager(this);
    // 60 s hard ceiling at the Qt layer; per-request timeout is layered on
    // top via QTimer so we can abort cleanly with a transport error code.
    nam_->setTransferTimeout(60'000);
}

HttpRpcClient::HttpRpcClient(const Config& config, QObject* parent)
    : QObject(parent)
    , d_(std::make_unique<Private>())
    , config_(config)
{
    nam_ = new QNetworkAccessManager(this);
    nam_->setTransferTimeout(60'000);
}

HttpRpcClient::~HttpRpcClient()
{
    cancelAll();
}

// =============================================================================
// HttpRpcClient - configuration
// =============================================================================

void HttpRpcClient::setEndpoint(const QUrl& endpoint)
{
    config_.endpoint = endpoint;
}

void HttpRpcClient::setTimeout(std::chrono::milliseconds t)
{
    config_.timeout = t;
}

void HttpRpcClient::setRetryCount(int retries)
{
    config_.retryCount = qMax(0, retries);
}

void HttpRpcClient::setRetryDelay(std::chrono::milliseconds d)
{
    config_.retryDelay = d;
}

void HttpRpcClient::setAuthHeader(const QString& header)
{
    config_.authHeader = header;
}

// =============================================================================
// Internal helpers
// =============================================================================

namespace {

QString newCorrelationId()
{
    quint64 a = QRandomGenerator::global()->generate64();
    quint64 b = QRandomGenerator::global()->generate64();
    return QString::number(a, 36) + QLatin1Char('-') + QString::number(b, 36);
}

QJsonObject buildRequest(const QString& id,
                         const QString& method,
                         const QJsonObject& params)
{
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), id);
    req.insert(QStringLiteral("method"), method);
    req.insert(QStringLiteral("params"), params);
    return req;
}

RpcException classifyTransportError(QNetworkReply::NetworkError err,
                                   const QString& detail)
{
    int code = -32006;
    switch (err) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
        code = -32002;
        break;
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::UnknownNetworkError:
        code = -32004;
        break;
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TimeoutError:
        code = -32001;
        break;
    case QNetworkReply::ContentAccessDenied:
    case QNetworkReply::AuthenticationRequiredError:
        code = -32005;
        break;
    default:
        code = -32006;
        break;
    }
    return RpcException(RpcException::Category::Transport,
                        code,
                        QStringLiteral("network error: %1").arg(detail));
}

} // namespace

// =============================================================================
// HttpRpcClient - dispatch (shared between sync and async)
// =============================================================================

void HttpRpcClient::startAttempt(const QString& correlationId)
{
    QMutexLocker<QRecursiveMutex> locker(&d_->mutex);

    auto it = d_->pending.find(correlationId);
    if (it == d_->pending.end()) return;
    auto& p = it.value();
    if (p.finished) return;

    ++p.attempt;

    const QJsonObject req = buildRequest(correlationId, p.method, p.params);
    const QByteArray body  = QJsonDocument(req).toJson(QJsonDocument::Compact);

    QNetworkRequest nreq{config_.endpoint};
    nreq.setHeader(QNetworkRequest::ContentTypeHeader,
                   QStringLiteral("application/json"));
    nreq.setRawHeader(QByteArrayLiteral("User-Agent"),
                      config_.userAgent.toUtf8());
    nreq.setRawHeader(QByteArrayLiteral("Accept"),
                      QByteArrayLiteral("application/json"));
    if (!config_.authHeader.isEmpty()) {
        nreq.setRawHeader(QByteArrayLiteral("Authorization"),
                          config_.authHeader.toUtf8());
    }

    p.reply = nam_->post(nreq, body);

    // Per-request timeout (overrides the 60 s ceiling from NAM).
    p.timer = new QTimer(p.reply.data());
    p.timer->setSingleShot(true);
    p.timer->setInterval(static_cast<int>(config_.timeout.count()));
    // FIX-P0-04-Issue1: capture the guarded pointer and test it before use.
    // The context is `this` (not the reply) so the slot can be safely queued
    // even if the reply is already scheduled for deletion.
    QObject::connect(p.timer, &QTimer::timeout, this, [reply = p.reply]() {
        if (reply && reply->isRunning()) {
            qWarning() << "HttpRpcClient: per-request timeout fired, aborting reply";
            reply->abort();
        }
    });
    p.timer->start();

    QObject::connect(p.reply, &QNetworkReply::finished, this, [this, correlationId]() {
        this->onReplyFinished(correlationId);
    });
}

void HttpRpcClient::onReplyFinished(const QString& correlationId)
{
    QMutexLocker<QRecursiveMutex> locker(&d_->mutex);

    auto it = d_->pending.find(correlationId);
    if (it == d_->pending.end()) return;
    auto& p = it.value();
    if (p.finished) return;
    p.finished = true;

    // The guarded pointer should be non-null here - the slot is connected to
    // this exact reply's finished() signal.  The check is a defensive belt.
    QNetworkReply* reply = p.reply;
    if (!reply) return;

    if (p.timer) {
        p.timer->stop();
        p.timer->deleteLater();
        p.timer = nullptr;
    }

    // ---------- transport error path (retryable) -----------------------------
    if (reply->error() != QNetworkReply::NoError) {
        RpcException e = classifyTransportError(reply->error(), reply->errorString());
        reply->deleteLater();
        reply = nullptr;
        p.reply.clear();

        const int maxAttempts = config_.retryCount + 1;  // 1 initial + retries
        if (e.isTransient() && p.attempt < maxAttempts) {
            qInfo().noquote()
                << QStringLiteral("HttpRpcClient: %1 transient error (%2), retry %3/%4")
                       .arg(p.method)
                       .arg(e.code())
                       .arg(p.attempt)
                       .arg(maxAttempts - 1);
            // Reset state and re-dispatch after retryDelay.
            p.finished = false;
            QTimer::singleShot(static_cast<int>(config_.retryDelay.count()),
                               this,
                               [this, correlationId]() { this->startAttempt(correlationId); });
            return;
        }

        // Final failure - emit and forget.
        if (p.onError) p.onError(std::move(e));
        emit rpcFailed(correlationId, p.method, e);
        if (p.sync && p.syncLoop) p.syncLoop->quit();
        d_->pending.erase(it);
        return;
    }

    // ---------- HTTP 200 path - parse JSON ------------------------------------
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    reply = nullptr;
    p.reply.clear();

    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        RpcException e{RpcException::Category::Protocol,
                       -32700,
                       QStringLiteral("invalid JSON in response: %1").arg(pe.errorString())};
        if (p.onError) p.onError(e);
        emit rpcFailed(correlationId, p.method, e);
        if (p.sync && p.syncLoop) p.syncLoop->quit();
        d_->pending.erase(it);
        return;
    }

    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        const QJsonObject errObj = obj.value(QStringLiteral("error")).toObject();
        RpcException e{RpcException::Category::Server,
                       errObj.value(QStringLiteral("code")).toInt(-1),
                       errObj.value(QStringLiteral("message")).toString(),
                       errObj.value(QStringLiteral("data")).toObject()};
        if (p.onError) p.onError(e);
        emit rpcFailed(correlationId, p.method, e);
        if (p.sync && p.syncLoop) p.syncLoop->quit();
        d_->pending.erase(it);
        return;
    }

    if (!obj.contains(QStringLiteral("result"))) {
        RpcException e{RpcException::Category::Protocol,
                       -32600,
                       QStringLiteral("response missing both result and error")};
        if (p.onError) p.onError(e);
        emit rpcFailed(correlationId, p.method, e);
        if (p.sync && p.syncLoop) p.syncLoop->quit();
        d_->pending.erase(it);
        return;
    }

    const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
    // FIX-G3c (2026-07-18): suppress verbose result body in logs.
    // The daemon heartbeat get_info poll outputs a ~1 KB JSON blob every
    // 5 seconds; logging the full result clutters the console and makes
    // real error messages hard to find. Keep method + CID for debugging;
    // the result body is available via the signal chain if needed.
    qInfo().noquote() << "st-118: HttpRpcClient RPC success, method=" << p.method
                       << "cid=" << correlationId;
    if (p.onSuccess) p.onSuccess(result);
    emit rpcCompleted(correlationId, p.method, result);
    if (p.sync && p.syncLoop) p.syncLoop->quit();
    d_->pending.erase(it);
}

// =============================================================================
// HttpRpcClient - synchronous public API
// =============================================================================

QJsonObject HttpRpcClient::call(const QString& method,
                                const QJsonObject& params,
                                const QString& correlationId)
{
    QMutexLocker<QRecursiveMutex> locker(&d_->mutex);

    if (!config_.endpoint.isValid() || config_.endpoint.isEmpty()) {
        throw RpcException(RpcException::Category::Protocol,
                           -32603,
                           QStringLiteral("HttpRpcClient endpoint not configured"));
    }

    const QString id = correlationId.isEmpty() ? newCorrelationId() : correlationId;

    QEventLoop loop;
    QJsonObject capturedResult;
    RpcException capturedError{RpcException::Category::Unknown,
                               -32603,
                               QStringLiteral("internal: never set")};
    bool gotResult = false;
    bool gotError  = false;

    Private::Pending p;
    p.method = method;
    p.params = params;
    p.sync = true;
    p.syncLoop = &loop;
    p.onSuccess = [&](QJsonObject r) {
        capturedResult = std::move(r);
        gotResult = true;
        loop.quit();
    };
    p.onError = [&](RpcException e) {
        capturedError = std::move(e);
        gotError = true;
        loop.quit();
    };

    d_->pending.insert(id, std::move(p));
    startAttempt(id);

    // Release the lock while the local event loop is running so that other
    // threads can still call callAsync() / cancel() on this client.  The
    // recursive mutex would also allow re-entrancy, but unlocking avoids
    // serialising all operations behind the sync call.
    locker.unlock();
    loop.exec();

    // Defensive cleanup: if the loop exited without erasing the entry (e.g.
    // an external caller called loop.quit()), remove it and stop any
    // outstanding network activity.
    locker.relock();
    if (auto it = d_->pending.find(id); it != d_->pending.end()) {
        auto& leftover = it.value();
        leftover.finished = true;
        if (leftover.timer) {
            leftover.timer->stop();
            leftover.timer->deleteLater();
            leftover.timer = nullptr;
        }
        if (leftover.reply) {
            leftover.reply->abort();
        }
        d_->pending.erase(it);
    }
    locker.unlock();

    if (gotError) {
        throw capturedError;
    }
    // The reply path always sets one of gotResult or gotError before
    // quitting the loop, so reaching here with gotResult==false is a
    // genuine internal error.
    if (!gotResult) {
        throw RpcException(RpcException::Category::Unknown,
                           -32603,
                           QStringLiteral("HttpRpcClient::call ended without resolution"));
    }
    return capturedResult;
}

// =============================================================================
// HttpRpcClient - async public API
// =============================================================================

QString HttpRpcClient::callAsync(const QString& method,
                                 const QJsonObject& params,
                                 CompletionCallback onSuccess,
                                 ErrorCallback onError,
                                 const QString& correlationId)
{
    QMutexLocker<QRecursiveMutex> locker(&d_->mutex);

    if (!config_.endpoint.isValid() || config_.endpoint.isEmpty()) {
        const QString id = correlationId.isEmpty() ? newCorrelationId() : correlationId;
        QTimer::singleShot(0, this, [onError]() {
            if (onError) {
                onError(RpcException(RpcException::Category::Protocol,
                                     -32603,
                                     QStringLiteral("HttpRpcClient endpoint not configured")));
            }
        });
        return id;
    }

    const QString id = correlationId.isEmpty() ? newCorrelationId() : correlationId;

    Private::Pending p;
    p.method    = method;
    p.params    = params;
    p.onSuccess = std::move(onSuccess);
    p.onError   = std::move(onError);
    p.sync      = false;
    p.syncLoop  = nullptr;

    d_->pending.insert(id, std::move(p));

    // Defer one event loop tick so the caller's stack unwinds before we
    // touch the network - matters for QML signal ordering.
    QTimer::singleShot(0, this, [this, id]() { this->startAttempt(id); });

    return id;
}

// =============================================================================
// HttpRpcClient - cancel / cancelAll
// =============================================================================

void HttpRpcClient::cancel(const QString& correlationId)
{
    QMutexLocker<QRecursiveMutex> locker(&d_->mutex);

    auto it = d_->pending.find(correlationId);
    if (it == d_->pending.end()) return;
    auto& p = it.value();
    p.finished = true;
    if (p.timer) {
        p.timer->stop();
        p.timer->deleteLater();
        p.timer = nullptr;
    }
    if (p.reply) {
        p.reply->abort();
    }
    RpcException e{RpcException::Category::Cancel,
                   -32099,
                   QStringLiteral("cancelled by caller")};
    if (p.onError) p.onError(e);
    emit rpcFailed(correlationId, p.method, e);
    if (p.sync && p.syncLoop) p.syncLoop->quit();
    d_->pending.erase(it);
}

void HttpRpcClient::cancelAll()
{
    QMutexLocker<QRecursiveMutex> locker(&d_->mutex);

    const auto keys = d_->pending.keys();
    for (const auto& k : keys) {
        cancel(k);
    }
}

} // namespace judecoin_wallet::core::rpc
