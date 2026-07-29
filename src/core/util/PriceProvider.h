// =============================================================================
// Judecoin Wallet - PriceProvider (st-042)
//
// Background USD price feed for the Judecoin coin. Pulls the public
// CoinGecko simple-price endpoint, caches the result in QSettings for
// kDefaultCacheMs (5 min), and emits priceChanged() whenever the value
// updates (success, network failure, parse failure, or cache hit).
//
// Threading:
//   - Owns its own QNetworkAccessManager (constructed in ctor) and a
//     QTimer for auto-refresh. Both run on the Qt event loop of the
//     thread the PriceProvider was constructed in (main thread in V1.0).
//   - refresh() returns immediately; the result arrives via the
//     priceChanged(double) signal. QML binds to `priceUsd` /
//     `hasPrice` / `lastError` Q_PROPERTYs and refreshes in place.
//
// Privacy (V5.4.7):
//   - The HTTP request is GET-only, no headers, no body, no cookies.
//   - No wallet address, account index, balance, txid, or any other
//     identifying datum is ever added to the request URL or the QSettings
//     cache. The cache stores ONLY (price_usd, fetched_epoch_ms).
//   - The User-Agent header is set to a generic string so CoinGecko can
//     rate-limit anonymous V1.0 client traffic without seeing anything
//     beyond "judecoin-wallet/1.0".
//
// Failure mode contract (locked by st-042 modify spec):
//   - Network error OR non-2xx HTTP status OR JSON parse failure OR
//     schema-mismatch failure: emit priceChanged(0.0), set
//     lastError = "<reason>", and emit fetchFinished(false, 0.0).
//   - The QML side renders "N/A" when priceUsd == 0.0 AND hasPrice
//     is false. A previously-known non-zero price is preserved in
//     `priceUsd` (so the UI can show a stale value with a tooltip
//     "offline"), but hasPrice is set to false so the binding
//     distinguishes "no data" from "we know it's $0.00".
//
// V1.0 single-source contract (V5.4.21):
//   - The provider hard-codes CoinGecko as the upstream. There is no
//     Settings toggle for the price source in V1.0; the spec explicitly
//     notes "V1.0 CoinGecko only, no Settings change API".
//   - If a future stage wants to add a fallback source (e.g. CryptoCompare),
//     it goes behind a constructor parameter or a Settings field - this
//     header's API stays stable.
//
// Test isolation:
//   - The second-arg constructor (`apiBase`) overrides the production
//     endpoint. Tests point it at a `file://...` URL or an in-process
//     HTTP server (e.g. python3 -m http.server serving a stub JSON file)
//     so the suite never depends on the real CoinGecko service.
//   - Cache state lives in QSettings under the global JudecoinWallet
//     org/app names. Tests use the same per-test `setOrganizationName /
//     setApplicationName` swap that test_settings_manager uses (see
//     tests/test_price_provider/test_price_provider.cpp), so each test
//     gets a fresh cache slot and never clobbers the real
//     ~/.config/JudecoinWallet/JudecoinWallet.conf.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_UTIL_PRICE_PROVIDER_H
#define JUDECOIN_WALLET_CORE_UTIL_PRICE_PROVIDER_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
class QTimer;

namespace judecoin_wallet::core::util {

class PriceProvider : public QObject
{
    Q_OBJECT

    // -- QML-bindable state ------------------------------------------------
    //
    // All four properties notify on the same priceChanged() signal so a
    // single QML binding `Connections { target: priceProviderObj; function
    // onPriceChanged() { ... } }` is enough to react to any state change
    // (success, failure, cache hit, manual refresh).
    //
    //   priceUsd     - last successful price as USD. 0.0 when unknown.
    //                  Stays at the last good value during a failed
    //                  refresh so the UI can show "stale" rather than
    //                  blanking out.
    //   hasPrice     - true iff a non-zero price was ever successfully
    //                  fetched. Flipped to false on a hard failure ONLY
    //                  when the cache is empty; on a failed refresh
    //                  with a valid cache, hasPrice stays true.
    //   lastFetchMs  - epoch-ms of the last successful fetch. 0 if no
    //                  fetch has succeeded yet.
    //   lastError    - empty when the last operation succeeded. Set to a
    //                  human-readable diagnostic on any failure. Cleared
    //                  on the next successful refresh.

    Q_PROPERTY(double   priceUsd    READ priceUsd    NOTIFY priceChanged)
    Q_PROPERTY(bool     hasPrice    READ hasPrice    NOTIFY priceChanged)
    Q_PROPERTY(qint64   lastFetchMs READ lastFetchMs NOTIFY priceChanged)
    Q_PROPERTY(QString  lastError   READ lastError   NOTIFY priceChanged)

public:
    /// Production ctor: points at the live CoinGecko simple-price endpoint.
    /// Use this from main.cpp / QML context.
    explicit PriceProvider(QObject* parent = nullptr);

    /// Test / alternate-source ctor: override the API base URL.
    /// Pass `file:///path/to/stub.json` or `http://localhost:NNNN/...`
    /// to redirect the fetch without recompiling.
    PriceProvider(const QUrl& apiBase, QObject* parent = nullptr);

    ~PriceProvider() override;

    // -- Read accessors -----------------------------------------------------

    double   priceUsd()    const { return priceUsd_; }
    bool     hasPrice()    const { return hasPrice_; }
    qint64   lastFetchMs() const { return lastFetchMs_; }
    QString  lastError()   const { return lastError_; }

    // -- Constants ----------------------------------------------------------

    /// 5-minute cache window. CoinGecko free tier is ~10-30 calls/min,
    /// so 5 min keeps us well under the limit even with auto-refresh on.
    static constexpr int kDefaultCacheMs = 5 * 60 * 1000;

    /// Default auto-refresh interval (every 5 min). Caller can override
    /// via setAutoRefreshInterval(). 0 means "no auto-refresh".
    static constexpr int kDefaultAutoRefreshMs = 5 * 60 * 1000;

    /// Network timeout for a single fetch. CoinGecko normally responds
    /// in <2 s; 10 s is the upper bound before we'd rather give up and
    /// serve a stale cached value.
    static constexpr int kNetworkTimeoutMs = 10 * 1000;

    /// Coin id on CoinGecko. V1.0 is hard-coded to "judecoin".
    /// (CoinGecko's path slug for the Judecoin token, requested at
    /// listing time; subject to change in V1.1 if they rebrand.)
    static const char* const kCoinId;

    /// Currency code we render. V1.0 is USD only; the spec calls out
    /// "BalanceCard USD only" and "Settings has no currency toggle".
    static const char* const kVsCurrency;

    // -- Imperative API (Q_INVOKABLE for QML) ------------------------------

    /// Trigger a price refresh. Returns immediately; the result arrives
    /// via priceChanged() / fetchFinished() signals.
    ///
    /// Cache semantics:
    ///   - If a cached value is fresh (< kDefaultCacheMs old), it's
    ///     emitted in-place via priceChanged() WITHOUT making a network
    ///     call. This keeps the UI snappy on app startup and avoids
    ///     hammering CoinGecko when the user toggles between pages.
    ///   - Otherwise a QNetworkAccessManager::get() is dispatched.
    ///
    /// Multiple in-flight refresh() calls collapse to one network
    /// request (a flag suppresses re-entry until the current reply lands).
    Q_INVOKABLE void refresh();

    /// Start / restart the auto-refresh QTimer.
    ///   intervalMs <= 0  -> disable auto-refresh (timer stopped)
    ///   intervalMs >  0  -> refresh every intervalMs ms
    /// Default at construction time: kDefaultAutoRefreshMs.
    Q_INVOKABLE void setAutoRefreshInterval(int intervalMs);

    /// QML helper: render the current price as "$0.01234" / "N/A".
    /// Uses 5 significant fractional digits trimmed to at most 5
    /// trailing zeroes stripped, so $1.23 stays "$1.23" instead of
    /// "$1.23000". hasPrice() == false returns "N/A".
    Q_INVOKABLE QString formatUsd() const;

    /// Wipe the QSettings cache. Used by tests and (in future) by a
    /// "reset price cache" button in the Settings UI.
    Q_INVOKABLE void clearCache();

    /// Public read-only accessor for the URL that refresh() will hit.
    /// Tests use this to assert the URL shape (e.g. privacy check that
    /// no wallet data is appended). Production code should call
    /// refresh() instead.
    QUrl buildApiUrlForTest() const { return buildApiUrl(); }

signals:
    /// Fires on every state transition (cache hit, fresh fetch, error).
    /// `priceUsd` is 0.0 on failure unless we have a cached value, in
    /// which case the cached value is re-emitted so QML stays live.
    void priceChanged(double priceUsd);

    /// Network-fence signals. fetchStarted() fires once when the actual
    /// QNetworkReply is dispatched; fetchFinished() fires once per reply
    /// (including error replies). The bool is true iff the operation
    /// yielded a usable non-zero price.
    void fetchStarted();
    void fetchFinished(bool ok, double priceUsd);

private slots:
    void onReplyFinished();
    void onAutoRefreshTick();

private:
    // -- Internal helpers ---------------------------------------------------

    /// Build the full API URL with the right query params. Public so
    /// tests can assert the URL shape (e.g. `apiBase_` is preserved).
    QUrl buildApiUrl() const;

    /// Read (price, fetchedMs) from QSettings. Returns true iff a valid
    /// entry was found. The values are written to `outPrice` /
    /// `outFetchedMs` regardless (defaults: 0.0 / 0) when no cache hit.
    bool readCache(double* outPrice, qint64* outFetchedMs) const;

    /// Write (price, fetchedMs) to QSettings and sync().
    void writeCache(double price, qint64 fetchedMs) const;

    /// Emit priceChanged + update internal state. Optionally updates
    /// lastError (empty string clears). Used by both the success path
    /// and the failure path so the QML side sees a single notification
    /// per state change.
    void setPriceInternal(double price,
                          qint64  fetchedMs,
                          bool    hasPrice,
                          const QString& err);

    // -- Member state -------------------------------------------------------

    QNetworkAccessManager* manager_   = nullptr;  // owned (child QObject)
    QTimer*                refreshTimer_ = nullptr; // owned (child QObject)
    QSettings*             settings_  = nullptr;  // owned; uses global
                                                  // JudecoinWallet org/app
                                                  // (overridable by tests
                                                  // via setOrg/AppName)

    QUrl    apiBase_;
    double  priceUsd_    = 0.0;
    bool    hasPrice_    = false;
    qint64  lastFetchMs_ = 0;
    QString lastError_;

    /// Re-entry guard. While a network reply is in flight, additional
    /// refresh() calls are silently dropped (the current reply's
    /// priceChanged will satisfy them).
    bool fetchInFlight_ = false;

    Q_DISABLE_COPY(PriceProvider)
};

} // namespace judecoin_wallet::core::util

#endif // JUDECOIN_WALLET_CORE_UTIL_PRICE_PROVIDER_H
