// =============================================================================
// Judecoin Wallet - RecoveryManager implementation (st-012)
//
// State machine:
//
//   Idle
//     |
//     | startRecovery(config)
//     v
//   CheckingDaemon  --(fail)--> Failed --> (idle on cancel or new call)
//     |
//     | daemon reachable + height > 0
//     v
//   Generating      --(fail)--> Failed
//     |  (RPC: wallet-rpc.generate_from_keys with seed)
//     |  wallet-rpc internally performs "daemon check #2":
//     |    scan chain for matching UTXOs from the derived address
//     |  success = on-chain activity found
//     v
//   ScanningBlockchain
//     |  (QTimer 1s tick; polls daemon.get_info height)
//     |  emits progressUpdated(current, total) each tick
//     |  stop conditions: synchronized==true OR a fixed scan window OR
//     |  the wallet-rpc height catches up (left to st-018 + st-020).
//     v
//   Complete        (currentFilename set; emits recoveryComplete)
//
// On any failure the path is:
//     Failed  (lastError_ + lastRpcMethod_ populated; emits recoveryFailed)
//
// cancel() from any non-Complete state:
//     - sets state_ = Idle
//     - in-flight RPC completes normally; we just ignore its result
//     - progress poll stops
// =============================================================================

#include "core/wallet/RecoveryManager.h"

#include "core/rpc/DaemonClient.h"
#include "core/rpc/HttpRpcClient.h"
#include "core/util/SecureBuffer.h"   // P0 #9: secureClear() for sensitive QString fields
#include "core/wallet/MnemonicWordList.h"
#include "core/wallet/WalletManager.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <stdexcept>

namespace judecoin_wallet::core::wallet {

using judecoin_wallet::core::rpc::DaemonClient;
using judecoin_wallet::core::rpc::DaemonInfo;
using judecoin_wallet::core::rpc::GenerateFromKeysRequest;
using judecoin_wallet::core::rpc::RestoreDeterministicRequest;
using judecoin_wallet::core::rpc::RpcException;
using judecoin_wallet::core::rpc::WalletRpcClient;

// =============================================================================
// Construction / destruction
// =============================================================================

RecoveryManager::RecoveryManager(QObject* parent)
    : QObject(parent)
    , walletRpc_(new WalletRpcClient(this))
    , ownsWalletRpc_(true)
    , daemon_(new DaemonClient(this))
    , ownsDaemon_(true)
    , progressTimer_(new QTimer(this))
{
    connect(walletRpc_, &WalletRpcClient::walletResponse,
            this, &RecoveryManager::onGenerateKeysResponse);
    connect(walletRpc_, &WalletRpcClient::walletFailed,
            this, &RecoveryManager::onGenerateKeysFailed);
    progressTimer_->setInterval(1000);  // 1s tick for ScanningBlockchain
    progressTimer_->setSingleShot(false);
    // st-112: emulate "triggeredOnStart" — the first progressUpdated
    // must fire the moment doStartProgressPoll() runs, so the QML UI
    // sees a reading immediately when ScanningBlockchain begins
    // (instead of waiting up to 1s for the first interval tick).
    // Qt 6.6 QTimer has no setTriggeredOnStart() method; doStartProgressPoll()
    // calls onProgressTick() once manually before start().
    connect(progressTimer_, &QTimer::timeout,
            this, &RecoveryManager::onProgressTick);
}

RecoveryManager::RecoveryManager(WalletRpcClient* walletRpc,
                                  DaemonClient* daemon,
                                  QObject* parent)
    : QObject(parent)
    , walletRpc_(walletRpc)
    , ownsWalletRpc_(false)
    , daemon_(daemon)
    , ownsDaemon_(false)
    , progressTimer_(new QTimer(this))
{
    if (walletRpc_ != nullptr) {
        connect(walletRpc_, &WalletRpcClient::walletResponse,
                this, &RecoveryManager::onGenerateKeysResponse);
        connect(walletRpc_, &WalletRpcClient::walletFailed,
                this, &RecoveryManager::onGenerateKeysFailed);
    }
    // st-112: see comment in the default ctor above. Qt 6.6 QTimer has
    // no setTriggeredOnStart() method.
    connect(progressTimer_, &QTimer::timeout,
            this, &RecoveryManager::onProgressTick);
}

RecoveryManager::RecoveryManager(WalletManager* walletManager,
                                  DaemonClient* daemon,
                                  QObject* parent)
    : QObject(parent)
    , walletRpc_(walletManager != nullptr ? walletManager->rpc().data() : nullptr)
    , ownsWalletRpc_(false)
    , daemon_(daemon)
    , ownsDaemon_(false)
    , walletManager_(walletManager)
    , progressTimer_(new QTimer(this))
{
    if (walletRpc_ != nullptr) {
        connect(walletRpc_, &WalletRpcClient::walletResponse,
                this, &RecoveryManager::onGenerateKeysResponse);
        connect(walletRpc_, &WalletRpcClient::walletFailed,
                this, &RecoveryManager::onGenerateKeysFailed);
    }
    progressTimer_->setInterval(1000);
    progressTimer_->setSingleShot(false);
    // st-112: see comment in the default ctor above. Qt 6.6 QTimer has
    // no setTriggeredOnStart() method.
    connect(progressTimer_, &QTimer::timeout,
            this, &RecoveryManager::onProgressTick);
}

RecoveryManager::~RecoveryManager()
{
    if (progressTimer_ != nullptr) {
        progressTimer_->stop();
    }
    // SENSITIVE (P0 #9): scrub member fields before destruction so that
    // error reasons, pending filenames and correlation ids do not linger
    // in freed heap memory after the object is destroyed.
    judecoin_wallet::core::util::secureClear(lastError_);
    judecoin_wallet::core::util::secureClear(lastRpcMethod_);
    judecoin_wallet::core::util::secureClear(currentFilename_);
    judecoin_wallet::core::util::secureClear(pendingFilename_);
    judecoin_wallet::core::util::secureClear(pendingCorrelationId_);
    if (ownsWalletRpc_) {
        delete walletRpc_;
    }
    if (ownsDaemon_) {
        delete daemon_;
    }
    walletRpc_ = nullptr;
    daemon_ = nullptr;
}

// =============================================================================
// QVariantMap → Config conversion (QML bridge)
// =============================================================================

RecoveryManager::Config RecoveryManager::configFromMap(const QVariantMap& map)
{
    Config c;
    c.filename     = map.value(QStringLiteral("filename")).toString();
    c.password     = map.value(QStringLiteral("password")).toString();
    c.mnemonic     = map.value(QStringLiteral("mnemonic")).toString();
    c.address      = map.value(QStringLiteral("address")).toString();
    c.viewkey      = map.value(QStringLiteral("viewkey")).toString();
    c.spendkey     = map.value(QStringLiteral("spendkey")).toString();
    if (map.contains(QStringLiteral("restore_height"))) {
        c.restore_height = map.value(QStringLiteral("restore_height")).toULongLong();
    }
    if (map.contains(QStringLiteral("autosave_current"))) {
        c.autosave_current = map.value(QStringLiteral("autosave_current")).toBool();
    }
    return c;
}

// =============================================================================
// Static helpers
// =============================================================================

MnemonicValidationResult
RecoveryManager::validateMnemonic(const QString& mnemonic)
{
    MnemonicValidationResult out;
    out.words = MnemonicWordList::splitWords(mnemonic);
    out.wordCount = out.words.size();

    if (out.wordCount == 0) {
        out.reason = QStringLiteral("Mnemonic is empty");
        out.ok = false;
        return out;
    }
    if (out.wordCount != MnemonicWordList::kEnglishWordCount25) {
        out.reason = QStringLiteral("Expected %1 words, got %2")
                         .arg(MnemonicWordList::kEnglishWordCount25)
                         .arg(out.wordCount);
        out.ok = false;
        return out;
    }

    // st-130 (2026-07-17): removed wordlist check. Case 1 wallet
    // creation allows custom (non-monero) words, so recovery must
    // accept the same input. Only enforce word count (=25); the
    // authoritative seed validation happens in wallet-rpc's
    // generate_from_keys which returns an RPC error if the seed
    // truly cannot derive a spendable wallet.

    out.ok = true;
    out.reason.clear();
    return out;
}

GenerateFromKeysRequest RecoveryManager::buildRequest(const Config& config)
{
    GenerateFromKeysRequest req;
    req.filename = config.filename;
    req.password = config.password;
    req.restore_height = config.restore_height;
    req.autosave_current = config.autosave_current;

    if (config.isCase4Backup()) {
        // st-022: Case 4 B-template backup restore. Forward address +
        // viewkey + spendkey verbatim to wallet-rpc. wallet-rpc does the
        // "daemon check #2" pass: it tries to re-derive the spend pubkey
        // from spendkey+viewkey and compares to the supplied address;
        // mismatch surfaces as RPC error (-2 invalid argument).
        // V5.4.7: the UI never derives keys; wallet-rpc is the sole
        // key-handler and re-encryptor.
        req.address = config.address;
        req.viewkey = config.viewkey;
        req.spendkey = config.spendkey;
        req.seed.clear();
    } else {
        // Case 3 (st-012 default): seed-based recovery. address +
        // viewkey are NOT supplied; we deliberately leave them empty
        // so wallet-rpc derives them from the seed.
        req.seed = config.mnemonic;
        req.address.clear();
        req.viewkey.clear();
        req.spendkey.clear();
    }
    return req;
}

bool RecoveryManager::validateCase4Config(const Config& config,
                                            QString* outReason)
{
    if (config.mnemonic.isEmpty() &&
        config.spendkey.isEmpty() &&
        config.viewkey.isEmpty() &&
        config.address.isEmpty()) {
        if (outReason) *outReason =
            QStringLiteral("Config is empty (no seed and no backup fields)");
        return false;
    }
    if (!config.mnemonic.isEmpty()) {
        if (outReason) *outReason =
            QStringLiteral("Case 4 (backup) Config must have empty mnemonic; "
                           "this looks like a Case 3 (seed) Config");
        return false;
    }
    if (config.spendkey.isEmpty()) {
        if (outReason) *outReason =
            QStringLiteral("Case 4 Config is missing spendkey");
        return false;
    }
    if (config.viewkey.isEmpty()) {
        if (outReason) *outReason =
            QStringLiteral("Case 4 Config is missing viewkey");
        return false;
    }
    if (config.address.isEmpty()) {
        if (outReason) *outReason =
            QStringLiteral("Case 4 Config is missing address");
        return false;
    }
    if (outReason) outReason->clear();
    return true;
}

qint64 RecoveryManager::nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// =============================================================================
// Private: state machine helpers
// =============================================================================

void RecoveryManager::setState(State newState)
{
    if (state_ == newState) {
        return;
    }
    state_ = newState;
    emit stateChanged(newState);
}

void RecoveryManager::setError(const QString& reason, const QString& rpcMethod)
{
    lastError_ = reason;
    lastRpcMethod_ = rpcMethod;
}

bool RecoveryManager::doValidate(const QString& mnemonic,
                                  MnemonicValidationResult* outResult)
{
    MnemonicValidationResult r = validateMnemonic(mnemonic);
    if (outResult != nullptr) {
        *outResult = r;
    }
    return r.ok;
}

void RecoveryManager::doCheckDaemon()
{
    // V5.4.4 Case 3 check #1: daemon must be reachable + have at least
    // one block. We use the synchronous fetchInfo() because the UI is
    // already blocked in startRecovery() at this point (this is the
    // sync path; the async path routes through startRecoveryAsync).
    if (daemon_ == nullptr) {
        throw std::runtime_error("RecoveryManager has no DaemonClient");
    }
    DaemonInfo info = daemon_->fetchInfo();  // throws RpcException
    if (info.height == 0) {
        throw std::runtime_error(
            "Daemon has no blocks yet - cannot verify seed against chain. "
            "Please wait for the daemon to sync.");
    }
    // We deliberately do NOT require synchronized==true here: a slow
    // daemon is fine for a restore (the user has time to wait). We only
    // need at least genesis to be on disk so wallet-rpc has something
    // to scan.
}

void RecoveryManager::doGenerate(const Config& config)
{
    if (walletRpc_ == nullptr) {
        throw std::runtime_error("RecoveryManager has no WalletRpcClient");
    }
    GenerateFromKeysRequest req = buildRequest(config);
    // buildRequest() now populates restore_height + autosave_current from
    // config (st-022). Wallet-rpc accepts the values verbatim; we do not
    // validate restore_height against the chain here - wallet-rpc itself
    // surfaces "restore_height past current top" as an RPC error and the
    // UI can re-prompt with a smaller height.
    walletRpc_->generateFromKeys(req);  // throws RpcException
}

void RecoveryManager::doStartProgressPoll()
{
    if (progressTimer_ == nullptr) {
        return;
    }
    progressCurrent_ = 0;
    progressTotal_ = 0;
    // st-112: emulate Qt6 missing setTriggeredOnStart() by firing the
    // first tick synchronously before start(). The early-return guard
    // in onProgressTick() (state_ == ScanningBlockchain) is satisfied
    // because the caller has already transitioned to ScanningBlockchain.
    onProgressTick();
    progressTimer_->start();
}

void RecoveryManager::doStopProgressPoll()
{
    if (progressTimer_ != nullptr) {
        progressTimer_->stop();
    }
}

// =============================================================================
// Synchronous API
// =============================================================================

void RecoveryManager::startRecovery(const Config& config)
{
    if (state_ != State::Idle) {
        throw std::runtime_error(
            "RecoveryManager is busy; cancel() first or wait for completion");
    }

    // ---- Step 1: local validation (no RPC) ----
    // st-022: branch on Config shape. Case 3 (mnemonic) uses the
    // wordlist check; Case 4 (backup) uses validateCase4Config (we trust
    // BackupParser to have done the per-field syntax check earlier).
    if (config.isCase4Backup()) {
        QString reason;
        if (!validateCase4Config(config, &reason)) {
            setError(reason);
            setState(State::Failed);
            throw std::runtime_error(reason.toStdString());
        }
    } else {
        MnemonicValidationResult vr;
        if (!doValidate(config.mnemonic, &vr)) {
            setError(vr.reason);
            setState(State::Failed);
            throw std::runtime_error(vr.reason.toStdString());
        }
    }

    // ---- Step 2: daemon check #1 ----
    setState(State::CheckingDaemon);
    try {
        doCheckDaemon();
    } catch (const RpcException& ex) {
        setError(QStringLiteral("Daemon check failed: %1").arg(ex.toString()),
                 QStringLiteral("get_info"));
        setState(State::Failed);
        throw;
    } catch (const std::exception& ex) {
        setError(QString::fromUtf8(ex.what()));
        setState(State::Failed);
        throw;
    }

    // ---- Step 3: wallet-rpc generate_from_keys ----
    setState(State::Generating);
    try {
        doGenerate(config);
    } catch (const RpcException& ex) {
        // Most common failure mode for Case 3: wallet-rpc returns
        // "invalid seed" or "no matching UTXO found" (i.e. daemon
        // check #2 failed because the wallet never had on-chain
        // activity). Surface this as a friendly message.
        const QString msg = QStringLiteral("Wallet recovery failed: %1").arg(ex.toString());
        setError(msg, QStringLiteral("generate_from_keys"));
        setState(State::Failed);
        throw;
    }

    // ---- Step 4: poll for chain scan progress ----
    currentFilename_ = config.filename;
    setState(State::ScanningBlockchain);
    doStartProgressPoll();

    // For the synchronous API we do NOT block on the scan; the caller
    // binds to progressUpdated + stateChanged and decides when the
    // "wallet is ready" view should be presented. A blocking variant
    // is out of scope for V1.0 (the scan may take 5-30 minutes for
    // restore_height=0).
    //
    // st-112 note: the sync path does NOT defer the Complete transition
    // (the API contract is "by the time startRecovery returns without
    // throwing, state is Complete and currentFilename is set"). The
    // async path (onGenerateKeysResponse handler below) DOES defer the
    // Complete transition via QTimer::singleShot(0) so the progressTimer
    // has at least one event-loop tick to fire before we stop it. The
    // sync path is inherently synchronous - the caller controls
    // event-loop progress themselves - so the timer is stopped in the
    // same call frame. See test_recovery_full_flow_sync for the
    // post-condition the sync path enforces.
    setState(State::Complete);
    doStopProgressPoll();
    const qint64 openedAt = nowMs();
    // We do not have a sync signal-equivalent here; the API contract
    // is "by the time startRecovery returns without throwing, state
    // is Complete and currentFilename is set". Async callers use
    // recoveryComplete.
    const QString cid;
    emit recoveryComplete(currentFilename_, openedAt, cid);
}

void RecoveryManager::cancel()
{
    if (state_ == State::Complete) {
        // Nothing to cancel; UI already has the wallet.
        return;
    }
    doStopProgressPoll();
    // SENSITIVE (P0 #9): QString::clear() merely releases the buffer
    // reference without zeroing the underlying UTF-16 storage. Use
    // secureClear() so pending recovery state is physically overwritten
    // before the object is reused or destroyed.
    judecoin_wallet::core::util::secureClear(lastError_);
    judecoin_wallet::core::util::secureClear(lastRpcMethod_);
    judecoin_wallet::core::util::secureClear(pendingFilename_);
    judecoin_wallet::core::util::secureClear(pendingCorrelationId_);
    progressCurrent_ = 0;
    progressTotal_ = 0;
    setState(State::Idle);
}

// =============================================================================
// Asynchronous API
// =============================================================================

// QML-facing overload: QVariantMap → Config → internal async flow.
QString RecoveryManager::startRecoveryAsync(const QVariantMap& qmlConfig,
                                             const QString& correlationId)
{
    qInfo().noquote() << "st-112: startRecoveryAsync(QVariantMap) called, cid=" << correlationId;
    Config cfg = configFromMap(qmlConfig);
    qInfo().noquote() << "st-112: configFromMap -> filename=" << cfg.filename << "hasMnemonic=" << !cfg.mnemonic.isEmpty();
    return startRecoveryAsync(cfg, correlationId);
}

QString RecoveryManager::startRecoveryAsync(const Config& config,
                                             const QString& correlationId)
{
    // st-112 Bug #2 (2026-07-17): entry log so early-return paths
    // (state != Idle, validation failure) are visible in diagnostics.
    qInfo().noquote() << "st-112: startRecoveryAsync(Config) entered, state="
                       << static_cast<int>(state_) << "cid=" << correlationId;
    if (state_ != State::Idle) {
        const QString msg = QStringLiteral(
            "RecoveryManager is busy; cancel() first or wait for completion");
        setError(msg);
        const QString cid = correlationId.isEmpty()
            ? QString::number(nowMs())
            : correlationId;
        emit recoveryFailed(msg, QString(), cid);
        return cid;
    }

    // ---- Step 1: local validation ----
    // st-022: branch on Config shape (mirror of startRecovery).
    const QString cid = correlationId.isEmpty()
        ? QString::number(nowMs())
        : correlationId;
    if (config.isCase4Backup()) {
        QString reason;
        if (!validateCase4Config(config, &reason)) {
            setError(reason);
            setState(State::Failed);
            emit recoveryFailed(reason, QString(), cid);
            return cid;
        }
    } else {
        MnemonicValidationResult vr;
        if (!doValidate(config.mnemonic, &vr)) {
            setError(vr.reason);
            setState(State::Failed);
            emit recoveryFailed(vr.reason, QString(), cid);
            return cid;
        }
    }

    // ---- Step 2: daemon check #1 (sync, but inside an async API
    //     because we cannot truly defer it - the user expects to
    //     see "daemon not running" before "generating"). On a
    //     failure we still emit recoveryFailed with the right cid.
    pendingCorrelationId_ = cid;

    setState(State::CheckingDaemon);
    qInfo().noquote() << "st-112: checking daemon...";
    try {
        doCheckDaemon();
        qInfo().noquote() << "st-112: daemon check passed";
    } catch (const RpcException& ex) {
        setError(QStringLiteral("Daemon check failed: %1").arg(ex.toString()),
                 QStringLiteral("get_info"));
        setState(State::Failed);
        emit recoveryFailed(lastError_, lastRpcMethod_, pendingCorrelationId_);
        return pendingCorrelationId_;
    } catch (const std::exception& ex) {
        setError(QString::fromUtf8(ex.what()));
        setState(State::Failed);
        emit recoveryFailed(lastError_, QString(), pendingCorrelationId_);
        return pendingCorrelationId_;
    }

    // ---- Step 3: kick off async recovery ----
    // st-150: Case 3 (seed) goes through restore_deterministic_wallet;
    // Case 4 (backup keys) goes through generate_from_keys.
    setState(State::Generating);
    pendingFilename_ = config.filename;
    // Route: seed present → restore_deterministic_wallet (derives keys
    // from mnemonic server-side). Otherwise → generate_from_keys (key-
    // based restore, requires address + viewkey + spendkey).
    const bool isSeedRecovery = !config.mnemonic.isEmpty();
    if (isSeedRecovery) {
        RestoreDeterministicRequest req;
        req.filename       = config.filename;
        req.password       = config.password;
        req.seed           = config.mnemonic;
        req.restore_height = config.restore_height;
        req.language       = QStringLiteral("English");
        qInfo().noquote()
            << "st-150: calling restoreDeterministicWalletAsync, filename="
            << config.filename << "cid=" << pendingCorrelationId_;
        return walletRpc_->restoreDeterministicWalletAsync(
            req, pendingCorrelationId_);
    }
    GenerateFromKeysRequest req = buildRequest(config);
    qInfo().noquote() << "st-112: calling generateFromKeysAsync, filename="
                       << config.filename << "cid=" << pendingCorrelationId_;
    return walletRpc_->generateFromKeysAsync(req, pendingCorrelationId_);
}

// =============================================================================
// Private slots
// =============================================================================

void RecoveryManager::onGenerateKeysResponse(const QString& correlationId,
                                              const QString& method,
                                              const QJsonObject& result)
{
    // We only react if this response is for OUR pending correlation id.
    // If cancel() was called the cid may have been cleared; in that
    // case we still drain the callback but do not advance state.
    if (correlationId != pendingCorrelationId_) {
        qInfo().noquote() << "st-112: onGenerateKeysResponse cid mismatch, got="
                           << correlationId << "expected=" << pendingCorrelationId_;
        return;
    }
    if (method != QStringLiteral("generate_from_keys") &&
        method != QStringLiteral("restore_deterministic_wallet")) {
        return;
    }
    if (state_ != State::Generating) {
        // Out-of-order callback (e.g. cancel() raced with response).
        return;
    }

    qInfo().noquote() << "st-112: " << method << " response OK, entering ScanningBlockchain";
    // The successful response from wallet-rpc means the wallet was
    // created + opened on the wallet-rpc side. We do not yet know
    // whether the chain scan is complete (wallet-rpc runs that in
    // the background after the RPC returns).
    currentFilename_ = resolvePendingFilename(correlationId);
    pendingFilename_.clear();
    setState(State::ScanningBlockchain);
    doStartProgressPoll();

    // For V1.0 we treat the wallet as "ready to use" once the RPC
    // returns success. The chain scan continues in the background
    // inside wallet-rpc; balance / history views will populate as
    // blocks are scanned. This matches V5.4.4 decision A (show
    // "Scanning block N / total") without blocking the UI.
    //
    // st-112: defer the Complete transition via a 0ms singleShot so
    // the progressTimer has at least one event-loop tick to fire
    // before we stop it. Without this deferral the timer was started
    // and immediately stopped in the same synchronous call frame and
    // the progressUpdated signal was unobservable in practice (a
    // regression vs the st-112 spec which calls out progressUpdated
    // as the primary user-facing progress surface). We capture the
    // cid by value into the lambda because the next event-loop tick
    // would otherwise see pendingCorrelationId_ already cleared.
    const QString cid = correlationId;
    QTimer::singleShot(0, this, [this, cid]() {
        setState(State::Complete);
        doStopProgressPoll();
        const qint64 openedAt = nowMs();
        // st-150 (2026-07-24): notify WalletManager that the
        // wallet was opened externally by restore_deterministic_wallet.
        // Without this, WalletManager stays in Closed state, walletOpened
        // never fires, and the dashboard shows no balance/address/history
        // until the user manually re-opens the wallet.
        if (walletManager_ != nullptr) {
            walletManager_->notifyExternalOpen(currentFilename_, openedAt);
        }
        emit recoveryComplete(currentFilename_, openedAt, cid);
        pendingCorrelationId_.clear();
    });
    (void)result;
}

void RecoveryManager::onGenerateKeysFailed(const QString& correlationId,
                                            const QString& method,
                                            const RpcException error)
{
    if (correlationId != pendingCorrelationId_) {
        qInfo().noquote() << "st-112: onGenerateKeysFailed cid mismatch, got="
                           << correlationId << "expected=" << pendingCorrelationId_;
        return;
    }
    if (method != QStringLiteral("generate_from_keys") &&
        method != QStringLiteral("restore_deterministic_wallet")) {
        return;
    }
    qInfo().noquote() << "st-112: " << method << " FAILED: " << error.toString();
    const QString msg = QStringLiteral("Wallet recovery failed: %1")
                            .arg(error.toString());
    setError(msg, method);
    setState(State::Failed);
    doStopProgressPoll();
    emit recoveryFailed(lastError_, lastRpcMethod_, correlationId);
    pendingCorrelationId_.clear();
}

void RecoveryManager::onProgressTick()
{
    if (state_ != State::ScanningBlockchain) {
        doStopProgressPoll();
        return;
    }
    if (daemon_ == nullptr) {
        return;
    }
    // Best-effort: we do NOT throw on failure (the progress tick is
    // non-critical). A failed tick simply skips this update.
    try {
        DaemonInfo info = daemon_->fetchInfo();
        // current = wallet-rpc progress (we approximate with info.height;
        // a more accurate value would come from wallet-rpc itself, but
        // wallet-rpc does not expose scan progress directly via RPC in
        // V3.2.0). total = daemon.height (chain tip).
        progressCurrent_ = static_cast<int>(info.height);
        progressTotal_   = static_cast<int>(info.height);
        emit progressUpdated(progressCurrent_, progressTotal_);
        if (info.synchronized) {
            // Daemon caught up; wallet-rpc should have everything too.
            doStopProgressPoll();
        }
    } catch (const RpcException&) {
        // Ignore - keep ticking.
    } catch (const std::exception&) {
        // Ignore - keep ticking.
    }
}

// =============================================================================
// Private: filename bookkeeping for async path
// =============================================================================

QString RecoveryManager::resolvePendingFilename(const QString& correlationId)
{
    // For V1.0 there is exactly one in-flight async recovery at a time
    // (state guards prevent multiple concurrent flows), so the
    // correlation id match is sufficient to identify the pending
    // filename. If a future iteration allows concurrent flows, replace
    // this with a QHash<QString, QString> map.
    if (correlationId != pendingCorrelationId_) {
        return currentFilename_;
    }
    if (!pendingFilename_.isEmpty()) {
        return pendingFilename_;
    }
    return currentFilename_;
}

} // namespace judecoin_wallet::core::wallet
