// =============================================================================
// Judecoin Wallet - HttpRpcClient (st-003)
//
// Generic JSON-RPC 2.0 client used by DaemonClient (st-004) and
// WalletRpcClient (st-005). Talks plain HTTP to localhost JSON-RPC endpoints:
//
//   daemon     -> http://127.0.0.1:19061/json_rpc
//   wallet-rpc -> http://127.0.0.1:19071/json_rpc
//
// This is V5.1 Option E (the UI never links wallet2; it talks RPC only) and
// V5.4.7 (the UI never reads key files; wallet-rpc is the sole encryptor).
//
// Design goals (per devplan st-003):
//   - Q_OBJECT + signals/slots so QML / controllers can react to completion.
//   - Synchronous .call(...) throws RpcException; async overload takes a
//     std::function callback and emits rpcCompleted / rpcFailed signals.
//   - 30 s default timeout, 3 retries on transport failures.
//   - Per-call correlation id so concurrent RPC calls do not cross.
//   - Strict JSON-RPC 2.0 envelope so /json_rpc endpoints accept the body.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_RPC_HTTP_RPC_CLIENT_H
#define JUDECOIN_WALLET_CORE_RPC_HTTP_RPC_CLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtNetwork/QNetworkAccessManager>

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <string>

namespace judecoin_wallet::core::rpc {

// -----------------------------------------------------------------------------
// RpcException
//
// Thrown by HttpRpcClient::call() and emitted through rpcFailed when a call
// fails for any reason. Categorised so callers (DaemonClient / WalletRpcClient)
// can map exceptions to user-friendly messages without parsing strings.
//
// JSON-RPC 2.0 standard codes (per spec):
//   -32700 parse error     : malformed JSON body
//   -32600 invalid request : bad envelope (missing jsonrpc, method, id)
//   -32601 method not found
//   -32602 invalid params
//   -32603 internal error
//
// Loki/Oxen / Monero fork daemon & wallet-rpc codes (verified from
// audit/cli_commands/v3.2.0.json + monero-project docs):
//   -1  wrong number of arguments
//   -2  generic / I/O / file-not-found
//   -3  daemon not running / not connected
//   -4  invalid payment id (wallet)
//   -5  no wallet (wallet not open yet)
//   -6  wallet already open
//   -7  invalid address
//   -8  invalid amount
//   -9  not enough money
//   -10 too many outputs for requested ringct mixin
//   -11 wallet is being closed
//   -12 wrong wallet password
//   -13 no daemon (wallet-rpc could not reach daemon)
//   -14 wallet not found on disk
//   -15 wallet still loading / refreshing blocks
//
// Transport-layer categories use codes >= -32000 (per JSON-RPC 2.0 server-error
// range). They are unique to this client; the wallet-rpc never returns them.
// -----------------------------------------------------------------------------
class RpcException : public std::exception
{
public:
    enum class Category {
        Transport,   // connection refused, TLS, DNS, timeout, malformed response
        Protocol,    // JSON-RPC 2.0 envelope error (server spoke garbage)
        Server,      // JSON-RPC 2.0 error.code from the remote endpoint
        Cancel,      // caller cancelled the request before completion
        Unknown,     // unclassified
    };

    RpcException(Category category,
                 int code,
                 QString message,
                 QJsonObject data = {});

    Category category() const noexcept { return category_; }
    int code() const noexcept { return code_; }
    const QString& message() const noexcept { return message_; }
    const QJsonObject& data() const noexcept { return data_; }

    /// std::exception interface — lets callers write
    /// `catch (const std::exception& e)` and still get the full error text
    /// via `e.what()`. The returned C-string is stored in `whatBuffer_` so
    /// it remains valid for the exception's lifetime (avoids the
    /// dangling-pointer bug from `toString().toStdString().c_str()` where
    /// the std::string temporary dies at the end of the full-expression).
    /// `mutable` is required because `what()` is `const` per the
    /// std::exception contract.
    const char* what() const noexcept override
    {
        try {
            whatBuffer_ = toString().toStdString();
            return whatBuffer_.c_str();
        } catch (...) {
            return "RpcException::what() formatting failed";
        }
    }

    /// Human-readable string suitable for qWarning() / UI banners.
    QString toString() const;

    /// True for transport errors that callers may want to retry (connection
    /// refused, timeout, EOF mid-response). False for protocol / server
    /// errors that almost always indicate a programmer mistake.
    bool isTransient() const noexcept;

    static QString categoryName(Category c);

private:
    Category category_;
    int code_;
    QString message_;
    QJsonObject data_;
    /// Backing store for `what()`. Mutable because `what()` is `const`;
    /// populated lazily on each call so the returned `c_str()` pointer is
    /// valid until the next `what()` call (or until the exception is
    /// destroyed, whichever comes first).
    mutable std::string whatBuffer_;
};

// -----------------------------------------------------------------------------
// HttpRpcClient
// -----------------------------------------------------------------------------
class HttpRpcClient : public QObject
{
    Q_OBJECT

public:
    using CompletionCallback = std::function<void(QJsonObject result)>;
    using ErrorCallback       = std::function<void(RpcException error)>;

    struct Config {
        QUrl endpoint;                       // e.g. http://127.0.0.1:19071/json_rpc
        std::chrono::milliseconds timeout{30'000};  // 30 s default
        int retryCount{3};                   // 3 retries on transport errors
        std::chrono::milliseconds retryDelay{200};  // 200 ms backoff between attempts
        QString authHeader;                  // optional Basic auth header value
        QString userAgent{QStringLiteral("JudecoinWallet/1.0 (Qt; HttpRpcClient)")};
    };

    explicit HttpRpcClient(QObject* parent = nullptr);
    explicit HttpRpcClient(const Config& config, QObject* parent = nullptr);
    ~HttpRpcClient() override;

    // -- configuration setters / getters ---------------------------------------

    void setEndpoint(const QUrl& endpoint);
    QUrl endpoint() const { return config_.endpoint; }

    void setTimeout(std::chrono::milliseconds t);
    std::chrono::milliseconds timeout() const { return config_.timeout; }

    void setRetryCount(int retries);
    int retryCount() const { return config_.retryCount; }

    void setRetryDelay(std::chrono::milliseconds d);
    std::chrono::milliseconds retryDelay() const { return config_.retryDelay; }

    void setAuthHeader(const QString& header);
    QString authHeader() const { return config_.authHeader; }

    /// Issue a synchronous RPC call. Throws RpcException on any failure.
    /// Blocks the calling thread until completion, timeout, or transport error.
    /// Intended for one-shot CLI / test paths; production UI should use the
    /// async overload below to keep the Qt event loop responsive.
    QJsonObject call(const QString& method,
                     const QJsonObject& params = {},
                     const QString& correlationId = {});

    /// Issue an asynchronous RPC call. Returns the correlation id assigned
    /// to this call (auto-generated if the caller did not provide one). The
    /// callback fires on the Qt event loop thread that owns this object, so
    /// UI code may update widgets directly from the callback.
    QString callAsync(const QString& method,
                      const QJsonObject& params,
                      CompletionCallback onSuccess,
                      ErrorCallback onError,
                      const QString& correlationId = {});

    /// Cancel an in-flight async call. No-op if the call already finished.
    void cancel(const QString& correlationId);

    /// Cancel every in-flight call. Used during shutdown.
    void cancelAll();

signals:
    /// Emitted when an async call succeeds. `correlationId` matches the
    /// value returned by callAsync().
    void rpcCompleted(const QString& correlationId,
                      const QString& method,
                      const QJsonObject& result);

    /// Emitted when an async call fails for any reason. `error` contains the
    /// categorised exception; see RpcException::toString().
    void rpcFailed(const QString& correlationId,
                   const QString& method,
                   const judecoin_wallet::core::rpc::RpcException error);

private:
    // Private implementation (PIMPL-ish; keeps the public header free of
    // QtNetwork includes for downstream consumers).
    struct Private;
    std::unique_ptr<Private> d_;

    Config config_;
    QNetworkAccessManager* nam_ = nullptr;

    /// Kick off one HTTP attempt for the given correlation id. Used by both
    /// sync (call) and async (callAsync) public APIs - the dispatch is the
    /// same, only the wait/notify strategy differs.
    void startAttempt(const QString& correlationId);

    /// QNetworkReply::finished handler. Parses the response, retries on
    /// transient transport errors, dispatches to the success/error path.
    void onReplyFinished(const QString& correlationId);

    Q_DISABLE_COPY(HttpRpcClient)
};

} // namespace judecoin_wallet::core::rpc

#endif // JUDECOIN_WALLET_CORE_RPC_HTTP_RPC_CLIENT_H
