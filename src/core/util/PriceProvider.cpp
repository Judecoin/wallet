// =============================================================================
// Judecoin Wallet - PriceProvider implementation (st-042)
//
// See PriceProvider.h for the contract. This file is straight-line Qt
// code: one QNetworkAccessManager::get() per refresh(), one QSettings
// read/write per cache hit/miss. There is no worker thread, no JSON
// streaming, no retry loop - V1.0 keeps the failure mode simple so the
// UI can confidently render "N/A" on any non-success path.
// =============================================================================

#include "core/util/PriceProvider.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>
#include <QDebug>

namespace judecoin_wallet::core::util {

// -----------------------------------------------------------------------------
// Static constants
// -----------------------------------------------------------------------------

const char* const PriceProvider::kCoinId     = "judecoin";
const char* const PriceProvider::kVsCurrency = "usd";

// CoinGecko production base URL. Hard-coded V1.0 (no Settings toggle).
// We use the v=success query param so future stages can opt into
// the paid tier without re-architecting.
const QUrl kDefaultApiBase(QStringLiteral(
    "https://api.coingecko.com/api/v3/simple/price"));

// User-Agent header value. V1.0 reveals app name + version only;
// no wallet address / device id / install id (V5.4.7).
const QByteArray kUserAgent = QByteArrayLiteral(
    "judecoin-wallet/1.0 (V1.0; +https://judecoin.local)");

// QSettings keys. Namespaced under "price/" so they never collide with
// SettingsManager's owned keys (which use bare names like "language").
constexpr const char* kCachePriceKey = "price/usd";
constexpr const char* kCacheFetchedKey = "price/fetched_ms";

// -----------------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------------

PriceProvider::PriceProvider(QObject* parent)
    : PriceProvider(kDefaultApiBase, parent)
{
    // Single-arg ctor delegates to the two-arg one with the production
    // base URL. The two-arg ctor does all the real work.
}

PriceProvider::PriceProvider(const QUrl& apiBase, QObject* parent)
    : QObject(parent)
    , manager_(new QNetworkAccessManager(this))
    , refreshTimer_(new QTimer(this))
    , settings_(new QSettings(
        // Explicit IniFormat + UserScope + JudecoinWallet identity so
        // the cache file lives next to SettingsManager's .conf. Tests
        // override org/app name via setOrganizationName / setApplicationName
        // BEFORE constructing the PriceProvider to redirect this.
        QSettings::IniFormat,
        QSettings::UserScope,
        QCoreApplication::organizationName().isEmpty()
            ? QStringLiteral("JudecoinWallet")
            : QCoreApplication::organizationName(),
        QCoreApplication::applicationName().isEmpty()
            ? QStringLiteral("JudecoinWallet")
            : QCoreApplication::applicationName(),
        this))
    , apiBase_(apiBase)
{
    // Auto-refresh timer is single-shot=false, repeats at kDefaultAutoRefreshMs.
    // We do NOT call start() here so the production binary only refreshes
    // when explicitly told to (e.g. from QML on page mount, or via
    // setAutoRefreshInterval). That matches the spec: "PriceProvider on
    // construction: read cache only; no auto-refresh until first refresh()".
    refreshTimer_->setSingleShot(false);
    refreshTimer_->setInterval(kDefaultAutoRefreshMs);
    connect(refreshTimer_, &QTimer::timeout,
            this, &PriceProvider::onAutoRefreshTick);

    // Read whatever the cache already has so QML sees a non-zero
    // priceUsd / non-empty lastFetchMs on first bind (e.g. on app
    // restart). We do NOT emit priceChanged() here because the binding
    // will pick up the values from the Q_PROPERTY reads; an extra
    // signal at ctor time would race with the QML scene graph
    // instantiation order.
    double cachedPrice = 0.0;
    qint64 cachedFetchedMs = 0;
    if (readCache(&cachedPrice, &cachedFetchedMs) && cachedPrice > 0.0) {
        // Populate the fields silently. We deliberately do NOT emit
        // priceChanged here because:
        //   1. The Q_PROPERTY NOTIFY signal is for CHANGE notifications,
        //      not for initial-state notifications. QML bindings read
        //      the value via READ priceUsd() and pick up the cached
        //      value on first bind.
        //   2. A ctor emit would race with QML Connections / spies
        //      that attach AFTER the ctor returns (this is the
        //      test_constructor_loads_existing_cache failure mode).
        //   3. The spec says "PriceProvider on construction: read
        //      cache only; no auto-refresh until first refresh()".
        //      QML triggers the first refresh() from Component.onCompleted.
        priceUsd_    = cachedPrice;
        lastFetchMs_ = cachedFetchedMs;
        hasPrice_    = true;
    }
}

PriceProvider::~PriceProvider()
{
    // Stop the timer first so a pending timeout doesn't fire into a
    // half-destructed object (Qt guards via QObject parentage, but
    // being explicit is cheap).
    if (refreshTimer_) {
        refreshTimer_->stop();
    }

    // QNetworkReply cleanup: any in-flight reply is parented to the
    // manager (via Qt internals) so deleting the manager cancels them
    // and prevents late callbacks into the destructor.
    if (manager_) {
        manager_->deleteLater();
        manager_ = nullptr;
    }

    delete settings_;
    settings_ = nullptr;
}

// -----------------------------------------------------------------------------
// Public QML API
// -----------------------------------------------------------------------------

void PriceProvider::refresh()
{
    // Cache short-circuit: if we have a fresh cached value, emit it
    // synchronously and return WITHOUT a network round-trip.
    if (hasPrice_) {
        double cachedPrice = 0.0;
        qint64 cachedFetchedMs = 0;
        if (readCache(&cachedPrice, &cachedFetchedMs) && cachedPrice > 0.0) {
            const qint64 ageMs = QDateTime::currentMSecsSinceEpoch()
                                 - cachedFetchedMs;
            if (ageMs >= 0 && ageMs < kDefaultCacheMs) {
                // Cache is fresh. Re-emit so QML sees a notification
                // (Bindings would update from the property read anyway,
                // but emitting lets Connections-based code react).
                lastError_.clear();
                emit priceChanged(priceUsd_);
                emit fetchFinished(true, priceUsd_);
                return;
            }
        }
    }

    // Re-entry guard: if a fetch is already in flight, drop this call.
    // The pending reply will emit priceChanged when it lands, which
    // satisfies the caller without an extra network request.
    if (fetchInFlight_) {
        return;
    }
    fetchInFlight_ = true;

    const QUrl url = buildApiUrl();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setRawHeader(QByteArrayLiteral("Accept"),
                     QByteArrayLiteral("application/json"));
    // Qt 6 QNetworkAccessManager defaults to HTTP/2 + 100-continue
    // disable for GETs, so no extra headers needed.

    QNetworkReply* reply = manager_->get(req);
    reply->setParent(this);  // owned by us; onReplyFinished deletes it

    // Network timeout via QTimer. We can't use QNetworkRequest::
    // setTransferTimeout for the overall request in Qt 6.4 (only for
    // individual chunk reads) so we use an explicit QTimer.
    QTimer* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->setInterval(kNetworkTimeoutMs);
    connect(timeout, &QTimer::timeout, this, [this, reply]() {
        if (reply->isRunning()) {
            reply->abort();  // triggers errorOccurred -> onReplyFinished
        }
    });
    timeout->start();

    connect(reply, &QNetworkReply::finished,
            this, &PriceProvider::onReplyFinished);

    emit fetchStarted();
}

void PriceProvider::setAutoRefreshInterval(int intervalMs)
{
    refreshTimer_->stop();
    if (intervalMs <= 0) {
        // 0 (or negative) -> auto-refresh disabled.
        return;
    }
    refreshTimer_->setInterval(intervalMs);
    refreshTimer_->start();
}

QString PriceProvider::formatUsd() const
{
    if (!hasPrice_ || priceUsd_ <= 0.0) {
        // Spec: UI shows "N/A" when no price is known. We deliberately
        // do NOT return "$0.00" because that would imply a real (but
        // zero) price rather than an absence of data.
        return QStringLiteral("N/A");
    }
    // QLocale::toCurrencyString with $ symbol, 5 fractional digits
    // trimmed. V1.0 only renders USD (the spec is hard-locked to USD
    // on the BalanceCard); we don't honor a user locale for the
    // currency symbol - it's always "$".
    QString s = QLocale::c().toString(priceUsd_, 'f', 5);
    // Trim trailing zeroes after the decimal point (so $1.23000 -> $1.23)
    // but keep at least 2 fractional digits so micro-prices still show.
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')) && s.count(QLatin1Char('.')) > 0) {
            // Only strip if we'd still have at least 2 digits after '.'
            const int dot = s.lastIndexOf(QLatin1Char('.'));
            if (s.length() - dot - 1 <= 2) break;
            s.chop(1);
        }
        if (s.endsWith(QLatin1Char('.'))) {
            s.chop(1);
        }
    }
    return QStringLiteral("$") + s;
}

void PriceProvider::clearCache()
{
    settings_->remove(QString::fromLatin1(kCachePriceKey));
    settings_->remove(QString::fromLatin1(kCacheFetchedKey));
    settings_->sync();
    // Update in-memory state. We deliberately do NOT emit priceChanged
    // here: clearCache() is a debug / test affordance, not a UI flow.
    priceUsd_    = 0.0;
    lastFetchMs_ = 0;
    hasPrice_    = false;
    lastError_.clear();
}

// -----------------------------------------------------------------------------
// Private slots
// -----------------------------------------------------------------------------

void PriceProvider::onReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        // Shouldn't happen, but guard against unexpected signal source.
        fetchInFlight_ = false;
        return;
    }

    // Always unhook the timer (reply-owned) and reset the guard so a
    // follow-up refresh() can fire.
    QTimer* timeout = reply->findChild<QTimer*>();
    if (timeout) {
        timeout->stop();
        timeout->deleteLater();
    }
    fetchInFlight_ = false;

    if (reply->error() != QNetworkReply::NoError) {
        // Network-level failure (timeout, DNS, connection refused, ...).
        // Spec: emit priceChanged(0.0), set lastError. We do NOT clobber
        // the cached priceUsd_ value because a UI that shows stale data
        // with a tooltip is more useful than blanking to 0.
        const QString err = reply->errorString();
        reply->deleteLater();
        setPriceInternal(priceUsd_,   // keep last good value
                         lastFetchMs_,
                         hasPrice_,    // keep "we have a price" flag
                         err);
        emit fetchFinished(false, 0.0);
        return;
    }

    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (httpStatus < 200 || httpStatus >= 300) {
        // Non-2xx. Treat as failure; the body might contain an error
        // message but we don't surface it (CoinGecko's free tier can
        // return HTML rate-limit pages that we'd mis-parse as JSON).
        setPriceInternal(priceUsd_, lastFetchMs_, hasPrice_,
                         QStringLiteral("HTTP %1").arg(httpStatus));
        emit fetchFinished(false, 0.0);
        return;
    }

    // Parse JSON: { "<coin_id>": { "<vs_currency>": <number> } }
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        setPriceInternal(priceUsd_, lastFetchMs_, hasPrice_,
                         QStringLiteral("JSON parse: %1").arg(
                             parseErr.errorString()));
        emit fetchFinished(false, 0.0);
        return;
    }
    const QJsonObject root = doc.object();
    if (!root.contains(QString::fromLatin1(kCoinId))) {
        setPriceInternal(priceUsd_, lastFetchMs_, hasPrice_,
                         QStringLiteral("schema: missing '%1' key").arg(
                             QString::fromLatin1(kCoinId)));
        emit fetchFinished(false, 0.0);
        return;
    }
    const QJsonValue coinVal = root.value(QString::fromLatin1(kCoinId));
    if (!coinVal.isObject()) {
        setPriceInternal(priceUsd_, lastFetchMs_, hasPrice_,
                         QStringLiteral("schema: '%1' is not object").arg(
                             QString::fromLatin1(kCoinId)));
        emit fetchFinished(false, 0.0);
        return;
    }
    const QJsonObject coinObj = coinVal.toObject();
    const QJsonValue priceVal = coinObj.value(QString::fromLatin1(kVsCurrency));
    if (!priceVal.isDouble()) {
        setPriceInternal(priceUsd_, lastFetchMs_, hasPrice_,
                         QStringLiteral("schema: '%1' is not number").arg(
                             QString::fromLatin1(kVsCurrency)));
        emit fetchFinished(false, 0.0);
        return;
    }
    const double newPrice = priceVal.toDouble();
    if (newPrice <= 0.0) {
        // CoinGecko can briefly return 0.0 during their data refresh
        // window. Treat as "no data" but do not crash.
        setPriceInternal(priceUsd_, lastFetchMs_, hasPrice_,
                         QStringLiteral("price <= 0 from upstream"));
        emit fetchFinished(false, 0.0);
        return;
    }

    // Success path.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    writeCache(newPrice, nowMs);
    setPriceInternal(newPrice, nowMs, true, QString());
    emit fetchFinished(true, newPrice);
}

void PriceProvider::onAutoRefreshTick()
{
    // Auto-refresh ignores the cache so the user's balance / USD value
    // stays close to real-time. The cache is still written on success
    // so a quick app restart stays snappy.
    refresh();
}

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

QUrl PriceProvider::buildApiUrl() const
{
    QUrl url = apiBase_;
    QUrlQuery q(url);
    q.addQueryItem(QStringLiteral("ids"),
                   QString::fromLatin1(kCoinId));
    q.addQueryItem(QStringLiteral("vs_currencies"),
                   QString::fromLatin1(kVsCurrency));
    url.setQuery(q);
    return url;
}

bool PriceProvider::readCache(double* outPrice, qint64* outFetchedMs) const
{
    if (outPrice)    *outPrice    = 0.0;
    if (outFetchedMs) *outFetchedMs = 0;

    if (!settings_->contains(QString::fromLatin1(kCachePriceKey))) {
        return false;
    }
    if (!settings_->contains(QString::fromLatin1(kCacheFetchedKey))) {
        return false;
    }

    bool ok = false;
    const double p = settings_->value(
        QString::fromLatin1(kCachePriceKey), 0.0).toDouble(&ok);
    if (!ok || p <= 0.0) {
        return false;
    }
    const qint64 m = settings_->value(
        QString::fromLatin1(kCacheFetchedKey), 0).toLongLong(&ok);
    if (!ok || m <= 0) {
        return false;
    }

    if (outPrice)    *outPrice    = p;
    if (outFetchedMs) *outFetchedMs = m;
    return true;
}

void PriceProvider::writeCache(double price, qint64 fetchedMs) const
{
    settings_->setValue(QString::fromLatin1(kCachePriceKey), price);
    settings_->setValue(QString::fromLatin1(kCacheFetchedKey), fetchedMs);
    settings_->sync();
}

void PriceProvider::setPriceInternal(double price,
                                     qint64  fetchedMs,
                                     bool    hasPrice,
                                     const QString& err)
{
    bool changed = false;
    if (!qFuzzyCompare(priceUsd_ + 1.0, price + 1.0)) {
        priceUsd_ = price;
        changed = true;
    }
    if (hasPrice_ != hasPrice) {
        hasPrice_ = hasPrice;
        changed = true;
    }
    if (lastFetchMs_ != fetchedMs) {
        lastFetchMs_ = fetchedMs;
        changed = true;
    }
    if (lastError_ != err) {
        lastError_ = err;
        changed = true;
    }
    // We always emit on failure so the QML side sees lastError update
    // even if the cached priceUsd didn't change.
    if (changed || !err.isEmpty()) {
        emit priceChanged(priceUsd_);
    }
}

} // namespace judecoin_wallet::core::util
