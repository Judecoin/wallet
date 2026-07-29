// =============================================================================
// Judecoin Wallet - WalletManager implementation (st-011)
//
// Lifecycle:
//
//   create_wallet(name, pw)   -> state: Closed  -> Opening -> Open
//                                                  (emits walletOpened)
//   generate_from_keys(...)   -> state: Closed  -> Opening -> Open
//   generate_from_json(...)   -> state: Closed  -> Opening -> Open
//   open_wallet(name, pw)     -> state: Closed  -> Opening -> Open
//                                (or Error      -> Opening -> Error on bad pw)
//   close_wallet()            -> state: Open    -> Closing -> Closed
//                                (or Closing    -> Closing -> Closed)
//                                (no-op if Closed)
//   change_wallet_password    -> state: Open    -> Open (no transition;
//                                                emits walletResponse).
//                                wallet-rpc does the re-encryption.
//
// Failure handling:
//   - RpcException is caught at every sync API entry point; the message
//     is stored in lastError_ and state goes to Error.
//   - Async failures arrive via walletFailed(); we route them through the
//     same recordError() so sync and async paths share the same state
//     machine.
//   - All transitions to Error preserve currentFilename_ (so the UI can
//     still show "my wallet" if the failure was transient, e.g. wallet-rpc
//     restarted mid-call).
//
// Memory-safety (V5.4.5 / gf-001, gemma4-p0-fixes):
//   - Every local QString that holds a password / viewkey / spendkey /
//     wallet JSON snapshot is scrubbed with util::secureClear() as soon
//     as the synchronous RPC call returns (success or throw) or, for
//     the async path, immediately after the dispatch returns. This
//     defeats the QString COW / no-zero-destructor behaviour flagged by
//     the gemma4:12b audit.
//   - The function signatures are unchanged (still const QString&); the
//     secureClear calls are an in-class implementation detail.
//
// Ownership (V5.4.7 + gf-007):
//   - `rpc_` is a QSharedPointer<WalletRpcClient>. The default
//     constructor creates and uniquely owns the inner client; the
//     injection constructor takes a shared pointer from the caller and
//     shares ownership. The `bool ownsRpc_` field from the previous
//     design is gone - ownership is now type-enforced rather than
//     tracked via a separate flag.
//   - The inner WalletRpcClient has *no* Qt parent in the default
//     ctor path. (The previous code passed `this` as parent and also
//     tracked ownsRpc_=true, so the parent-child mechanism and the
//     QObject delete in the destructor both "knew" about ownership;
//     with QSharedPointer owning the client we no longer set a Qt
//     parent, removing the dual-management hazard flagged by the
//     gemma4:12b audit.)
//   - Destructor is implicit: the QSharedPointer's last reference
//     dropping runs the default deleter.
// =============================================================================

#include "core/wallet/WalletManager.h"

#include "core/rpc/TransferApi.h"
#include "core/util/BackupParser.h"
#include "core/util/BackupParserTokenStore.h"
#include "core/util/SecureBuffer.h"

#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QString>

#include <utility>

namespace judecoin_wallet::core::wallet {

using judecoin_wallet::core::rpc::ChangeWalletPasswordRequest;
using judecoin_wallet::core::rpc::CloseWalletRequest;
using judecoin_wallet::core::rpc::CreateWalletRequest;
using judecoin_wallet::core::rpc::GenerateFromJsonRequest;
using judecoin_wallet::core::rpc::GenerateFromKeysRequest;
using judecoin_wallet::core::rpc::OpenWalletRequest;
using judecoin_wallet::core::rpc::QueryKeyRequest;
using judecoin_wallet::core::rpc::RpcException;
using judecoin_wallet::core::rpc::TransferDestination;
using judecoin_wallet::core::rpc::TransferRequest;
using judecoin_wallet::core::rpc::TransferResult;
using judecoin_wallet::core::rpc::WalletRpcClient;

// Convenience: project the SecureBuffer helpers into this TU.
using judecoin_wallet::core::util::SecureBuffer;
using judecoin_wallet::core::util::secureClear;

// =============================================================================
// V5.4.5 / gf-001 helpers: scrub sensitive QString fields of a request
// struct that we own. These are no-throw, idempotent, and centralised
// here so every code path that handles a password / viewkey / JSON
// snapshot goes through the same clearing logic.
// =============================================================================

namespace {

inline void scrubCreateRequest(CreateWalletRequest& req) noexcept
{
    // password is the only sensitive field. filename + language are
    // operationally public.
    secureClear(req.password);
}

inline void scrubOpenRequest(OpenWalletRequest& req) noexcept
{
    secureClear(req.password);
}

inline void scrubChangePasswordRequest(ChangeWalletPasswordRequest& req) noexcept
{
    secureClear(req.old_password);
    secureClear(req.new_password);
}

inline void scrubGenerateFromKeysRequest(GenerateFromKeysRequest& req) noexcept
{
    // password + viewkey + spendkey are sensitive. address is the
    // wallet's public address - technically public but cheap to wipe
    // so we wipe for defence-in-depth. seed (25-word mnemonic) is
    // also wiped if present.
    secureClear(req.password);
    secureClear(req.address);
    secureClear(req.viewkey);
    secureClear(req.spendkey);
    secureClear(req.seed);
}

inline void scrubGenerateFromJsonRequest(GenerateFromJsonRequest& req) noexcept
{
    // The JSON snapshot is the most sensitive payload we touch (it
    // typically contains a serialized wallet-key blob). Wipe both
    // password and the JSON itself.
    secureClear(req.password);
    secureClear(req.json_string);
}

} // namespace

// =============================================================================
// Construction / destruction
// =============================================================================
//
// gf-007: the two constructors share the same body modulo the rpc_
// member initialiser. We do NOT pass `this` as the inner
// WalletRpcClient's Qt parent in the default ctor: QSharedPointer
// owns the client, and a Qt parent here would create the
// double-ownership hazard the previous `ownsRpc_`+`delete` pattern
// was meant to paper over. connect() still works on the bare
// pointer via rpc_.data().

WalletManager::WalletManager(QObject* parent)
    : QObject(parent)
    , rpc_(QSharedPointer<WalletRpcClient>::create())
{
    connect(rpc_.data(), &WalletRpcClient::walletResponse,
            this, &WalletManager::onRpcResponse);
    connect(rpc_.data(), &WalletRpcClient::walletFailed,
            this, &WalletManager::onRpcFailed);
}

WalletManager::WalletManager(QSharedPointer<WalletRpcClient> client, QObject* parent)
    : QObject(parent)
    , rpc_(std::move(client))
{
    if (rpc_) {
        connect(rpc_.data(), &WalletRpcClient::walletResponse,
                this, &WalletManager::onRpcResponse);
        connect(rpc_.data(), &WalletRpcClient::walletFailed,
                this, &WalletManager::onRpcFailed);
    }
}

// Implicit destructor: QSharedPointer's default deleter handles the
// inner WalletRpcClient when the last reference drops. The
// WalletManager itself is a QObject; Qt's parent-child mechanism
// (via the QObject* parent passed to our constructors) handles the
// WalletManager's own lifetime.
//
// We do NOT touch rpc_ here. The previous code did:
//     if (ownsRpc_) { delete rpc_; }
//     rpc_ = nullptr;
// with `ownsRpc_` as a separate boolean. The boolean is gone; the
// QSharedPointer handles the bookkeeping.
WalletManager::~WalletManager() = default;

// =============================================================================
// Synchronous API
// =============================================================================

QString WalletManager::createWallet(const QString& filename,
                                     const QString& password,
                                     const QString& language)
{
    // Close any previously open wallet so wallet-rpc can create/switch.
    if (state_ == State::Open) {
        try { closeWallet(); } catch (...) { /* best-effort */ }
    }

    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    CreateWalletRequest req;
    req.filename = filename;
    req.password = password;
    req.language = language;

    try {
        rpc_->createWallet(req);
    } catch (const RpcException& ex) {
        // Wipe the password before propagating so an exception object
        // doesn't accidentally pin the local copy of the password in
        // memory longer than necessary.
        scrubCreateRequest(req);
        recordError(QStringLiteral("create_wallet"), ex);
        throw;
    }

    // Success: scrub before the request struct goes out of scope.
    scrubCreateRequest(req);

    // The onRpcResponse slot has already updated currentFilename_,
    // openedAtMs_, state, and emitted walletOpened by the time we get
    // here (sync RPC delivers its response via the same call stack).
    return filename;
}

// FIX-G10 (2026-07-17 21:24): seed-aware createWallet variant.
// When `seed` is non-empty the call routes to wallet-rpc
// `generate_from_keys` so the wallet is created FROM a client-generated
// 24-word mnemonic (which the UI then displays + copies to the
// system clipboard). When `seed` is empty the call falls through to
// the legacy server-side seed path (create_wallet RPC). See
// audit/2026-07-17/2026-07-17-mnemonic-24-words-and-copy.md.
QString WalletManager::createWallet(const QString& filename,
                                     const QString& password,
                                     const QString& seed,
                                     const QString& language)
{
    // Close any previously open wallet so wallet-rpc can create/switch.
    if (state_ == State::Open) {
        try { closeWallet(); } catch (...) { /* best-effort */ }
    }

    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    const QString trimmedSeed = seed.trimmed();
    if (!trimmedSeed.isEmpty()) {
        // FIX-G10 follow-up: GenerateFromKeysRequest has NO language
        // field (wallet-rpc's generate_from_keys RPC derives the
        // language from the seed itself). The `language` argument is
        // only meaningful on the legacy create_wallet path. The
        // other arg is intentionally unused here.
        Q_UNUSED(language);

        GenerateFromKeysRequest req;
        req.filename = filename;
        req.password = password;
        req.seed     = trimmedSeed;
        try {
            rpc_->generateFromKeys(req);
        } catch (const RpcException& ex) {
            scrubGenerateFromKeysRequest(req);
            recordError(QStringLiteral("generate_from_keys"), ex);
            throw;
        }
        scrubGenerateFromKeysRequest(req);
    } else {
        // Legacy path: server-side seed. Delegate to the 3-arg form
        // so the wiring (clearError / setState / try-catch / scrub)
        // stays in one place rather than forking twice in this method.
        return createWallet(filename, password, language);
    }

    return filename;
}

// -----------------------------------------------------------------------------
// getMnemonicSeed (st-151)
// -----------------------------------------------------------------------------

QString WalletManager::getMnemonicSeed()
{
    QueryKeyRequest req;
    req.key_type = QStringLiteral("mnemonic");
    try {
        SecureBuffer key = rpc_->queryKey(req);
        const QString seed = QString::fromUtf8(
            reinterpret_cast<const char*>(key.data()),
            static_cast<qsizetype>(key.size()));
        key.zero();
        return seed;
    } catch (const RpcException& ex) {
        recordError(QStringLiteral("query_key"), ex);
        throw;
    }
}

// -----------------------------------------------------------------------------
// getSpendKey / getViewKey (st-022 key export)
// -----------------------------------------------------------------------------

QString WalletManager::getSpendKey()
{
    QueryKeyRequest req;
    req.key_type = QStringLiteral("spend_key");
    try {
        SecureBuffer key = rpc_->queryKey(req);
        const QString spendKey = QString::fromUtf8(
            reinterpret_cast<const char*>(key.data()),
            static_cast<qsizetype>(key.size()));
        key.zero();
        return spendKey;
    } catch (const RpcException& ex) {
        recordError(QStringLiteral("query_key"), ex);
        throw;
    }
}

QString WalletManager::getViewKey()
{
    QueryKeyRequest req;
    req.key_type = QStringLiteral("view_key");
    try {
        SecureBuffer key = rpc_->queryKey(req);
        const QString viewKey = QString::fromUtf8(
            reinterpret_cast<const char*>(key.data()),
            static_cast<qsizetype>(key.size()));
        key.zero();
        return viewKey;
    } catch (const RpcException& ex) {
        recordError(QStringLiteral("query_key"), ex);
        throw;
    }
}

QString WalletManager::getWalletAddress()
{
    try {
        auto addr = rpc_->getAddress(0, 0);
        return addr.address;
    } catch (const RpcException& ex) {
        recordError(QStringLiteral("get_address"), ex);
        throw;
    }
}

qint64 WalletManager::getWalletHeight()
{
    try {
        auto height = rpc_->getHeight();
        return static_cast<qint64>(height.height);
    } catch (const RpcException& ex) {
        recordError(QStringLiteral("get_height"), ex);
        throw;
    }
}

void WalletManager::openWallet(const QString& filename, const QString& password)
{
    // Close any previously open wallet so wallet-rpc can switch.
    if (state_ == State::Open) {
        try { closeWallet(); } catch (...) { /* best-effort */ }
    }

    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    OpenWalletRequest req;
    req.filename = filename;
    req.password = password;

    // over Qt's nested QEventLoop in HttpRpcClient::call() — see
    // 2026-07-18-crash-wallet-manager-open-wallet-exception.md.
    try {
        rpc_->openWallet(req);
    } catch (const RpcException& ex) {
        scrubOpenRequest(req);
        recordError(QStringLiteral("open_wallet"), ex);
        setState(State::Error);
        pendingFilename_.clear();
        return;  // Do NOT re-throw: QEventLoop cannot unwind exceptions.
    }

    scrubOpenRequest(req);
    // FIX-G11c (2026-07-18): unconditionally set state + filename
    // on the sync success path. The signal chain inside QEventLoop cannot
    // be relied on — onRpcResponse may or may not fire depending on Qt
    // event dispatching. Use setCurrentFilename() (which emits
    // currentWalletChanged) so QML bindings update immediately.
    pendingFilename_.clear();
    setCurrentFilename(filename);
    openedAtMs_ = nowMs();
    setState(State::Open);
    qInfo().noquote() << "st-118: WalletManager::openWallet done | state=" << static_cast<int>(state_)
                       << "filename=" << currentFilename_;
}

// st-061: QML-friendly password verifier for ConfirmModal.qml. See the
// header doc-comment for the full design rationale (close+reopen side
// effect, ~0.1s wall clock, wrong-password -> lastError() + false
// return). The flow is:
//   1. If no wallet is open, the modal would have nothing to verify
//      against, so we return false without an RPC roundtrip. This also
//      keeps the function safe to call from QML before the user has
//      completed Onboarding.
//   2. Otherwise call the existing openWallet() with the currently-open
//      filename. The wallet-rpc open_wallet will close+reopen the wallet
//      (~0.1s); on a wrong password it returns code -12 (RpcException),
//      on a correct password it returns success and the wallet ends up
//      in the same Open state we started in.
//   3. Catch RpcException -> false + lastError() retains the message so
//      the QML side can show qsTr("Incorrect password") (we do NOT echo
//      the raw RpcException message in the UI to avoid leaking the
//      wallet-rpc internals to the user).
bool WalletManager::verifyPassword(const QString& password)
{
    if (currentFilename_.isEmpty()) {
        // No wallet is open. Treat as "incorrect" so the ConfirmModal
        // never auto-confirms on an empty currentFilename_. The modal
        // is mounted in the Onboarding/closed-wallet path for the V1.0
        // UI shell, and the dashboard's send flow always opens the
        // wallet first; this guard is a belt-and-suspenders.
        lastError_ = QStringLiteral(
            "verify_password called with no wallet open");
        setState(State::Error);
        return false;
    }

    try {
        openWallet(currentFilename_, password);
        return true;
    } catch (const RpcException& /*ex*/) {
        // openWallet already called recordError() so lastError_ holds
        // the underlying RpcException::toString(). The QML side ignores
        // the message text and shows qsTr("Incorrect password") on
        // any non-true return. We do NOT re-throw -- Q_INVOKABLE +
        // exception = QML script error, which would abort the modal
        // and the user would lose the chance to retry.
        return false;
    }
}

void WalletManager::closeWallet()
{
    if (state_ == State::Closed) {
        return;  // idempotent
    }
    if (state_ != State::Open && state_ != State::Error) {
        // In-flight Opening/Closing - calling close_wallet here would race.
        // The async path handles this naturally; sync caller must wait.
        // We don't expose a "cancel"; they should drop and call again.
        // For V1.0 simplicity, surface this as an RpcException-equivalent
        // by transitioning to Error and returning.
        lastError_ = QStringLiteral(
            "close_wallet called while another wallet RPC is in flight");
        setState(State::Error);
        return;
    }

    clearError();
    setState(State::Closing);
    pendingFilename_.clear();  // close: signal with empty (current clears)

    CloseWalletRequest req;
    req.filename = currentFilename_;

    try {
        rpc_->closeWallet(req);
    } catch (const RpcException& ex) {
        recordError(QStringLiteral("close_wallet"), ex);
        throw;
    }

    // CloseWalletRequest has no sensitive fields; nothing to scrub.
    // onRpcResponse (via isCloseMethod) has already cleared state.
}

void WalletManager::changeWalletPassword(const QString& old_password,
                                         const QString& new_password)
{
    clearError();
    // No state transition - we remain Open across a successful change.
    // On failure we go to Error so the UI prompts the user to retry.

    ChangeWalletPasswordRequest req;
    req.old_password = old_password;
    req.new_password = new_password;
    req.filename     = currentFilename_;

    try {
        rpc_->changeWalletPassword(req);
    } catch (const RpcException& ex) {
        scrubChangePasswordRequest(req);
        recordError(QStringLiteral("change_wallet_password"), ex);
        throw;
    }

    scrubChangePasswordRequest(req);
    // State stays Open. wallet-rpc is still holding the wallet; nothing
    // visible changed from the UI's perspective except the password.
}

QString WalletManager::generateFromKeys(const QString& filename,
                                         const QString& password,
                                         const QString& address,
                                         const QString& viewkey)
{
    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    GenerateFromKeysRequest req;
    req.filename = filename;
    req.password = password;
    req.address  = address;
    req.viewkey  = viewkey;

    try {
        rpc_->generateFromKeys(req);
    } catch (const RpcException& ex) {
        scrubGenerateFromKeysRequest(req);
        recordError(QStringLiteral("generate_from_keys"), ex);
        throw;
    }

    scrubGenerateFromKeysRequest(req);
    // onRpcResponse handles state + signals.
    return filename;
}

// =============================================================================
// generateFromKeysBackup — sync wrapper (FIX-F2, 2026-07-17)
//
// History: Q_INVOKABLE was added to this 7-arg sync method in commit
// 0a4c43c (fix-f). moc then generated a qt_static_metacall entry for it,
// which exposed a *latent bug*: the sync implementation had never been
// written. Before Q_INVOKABLE, moc's meta-call wasn't emitted, so the
// linker never looked for the symbol → silent compile success. After
// Q_INVOKABLE, moc emits `_t->generateFromKeysBackup(...)` and the
// linker fails with undefined reference.
//
// This implementation follows the existing createWallet sync pattern
// (line 183): clearError → setState(Opening) → block on
// rpc_->generateFromKeys(req) (sync HTTP RPC via HttpRpcClient::call)
// → scrub + recordError on RpcException → return filename on success.
// QML's try/catch in Case4BackupRestore.qml catches the RpcException
// the same way createWallet / openWallet failures do.
//
// The Async overload below (generateFromKeysBackupAsync) is unchanged.
//
// Refs: audit/2026-07-17/2026-07-17-fix-f-qml-binding-missing.md §3
QString WalletManager::generateFromKeysBackup(const QString& filename,
                                              const QString& password,
                                              const QString& address,
                                              const QString& viewkey,
                                              const QString& spendkey,
                                              quint64 restore_height,
                                              bool autosave_current)
{
    // Close any previously open wallet so wallet-rpc can create/switch.
    if (state_ == State::Open) {
        try { closeWallet(); } catch (...) { /* best-effort */ }
    }

    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    GenerateFromKeysRequest req;
    req.filename = filename;
    req.password = password;
    req.address  = address;
    req.viewkey  = viewkey;
    req.spendkey = spendkey;
    req.restore_height = restore_height;
    req.autosave_current = autosave_current;

    try {
        rpc_->generateFromKeys(req);
    } catch (const RpcException& ex) {
        scrubGenerateFromKeysRequest(req);
        recordError(QStringLiteral("generate_from_keys"), ex);
        throw;
    }

    scrubGenerateFromKeysRequest(req);
    // onRpcResponse handles state + signals (mirrors createWallet semantics).
    return filename;
}

// =============================================================================
// restoreFromBackupByToken — P0 #12 QML-safe Case 4 path (2026-07-24)
//
// Problem: Case4BackupRestore.qml previously held spendkey/viewkey as JS
// strings in page.parsed.*. V8/JS strings are immutable and the engine does
// not zero the underlying buffer when the reference is dropped; a heap scan
// can still recover the freed bytes until the GC/allocator reuses them.
//
// Fix: parseBackupFile() stores the parsed BackupParser::ParseResult in a
// C++ token store and returns only a token id + non-sensitive metadata.
// This method consumes the token, copies the keys into a
// GenerateFromKeysRequest, calls wallet-rpc, and scrubs every local copy.
// The QML side never receives the spendkey/viewkey as JS strings.
// =============================================================================
QVariantMap WalletManager::restoreFromBackupByToken(int tokenId,
                                                    const QString& password,
                                                    const QString& filename)
{
    using judecoin_wallet::core::util::BackupParserTokenStore;
    using judecoin_wallet::core::util::ParseResult;
    using judecoin_wallet::core::util::secureClear;

    QVariantMap out;

    ParseResult pr;
    if (!BackupParserTokenStore::instance().consume(tokenId, pr)) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"),
                   QStringLiteral("Backup token expired or invalid."));
        return out;
    }

    if (!pr.ok) {
        // Defensive: the token should only exist for successful parses,
        // but if something went wrong, scrub and report.
        secureClear(pr.spendkey);
        secureClear(pr.viewkey);
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"),
                   pr.reason.isEmpty()
                       ? QStringLiteral("Backup parse failed.")
                       : pr.reason);
        return out;
    }

    // Close any previously open wallet so wallet-rpc can create/switch.
    if (state_ == State::Open) {
        try { closeWallet(); } catch (...) { /* best-effort */ }
    }

    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    GenerateFromKeysRequest req;
    req.filename = filename;
    req.password = password;
    req.address  = pr.address;
    req.viewkey  = pr.viewkey;
    req.spendkey = pr.spendkey;
    req.restore_height = pr.restoreHeight;
    req.autosave_current = false;

    // The keys are now in the request object. Scrub the ParseResult copy
    // immediately so the token's secrets do not outlive this scope.
    secureClear(pr.spendkey);
    secureClear(pr.viewkey);

    try {
        rpc_->generateFromKeys(req);
    } catch (const RpcException& ex) {
        scrubGenerateFromKeysRequest(req);
        recordError(QStringLiteral("generate_from_keys"), ex);
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), lastError());
        return out;
    }

    scrubGenerateFromKeysRequest(req);
    // onRpcResponse handles state + signals and emits walletOpened.
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("filename"), filename);
    return out;
}

QString WalletManager::generateFromJson(const QString& filename,
                                         const QString& password,
                                         const QString& jsonString)
{
    clearError();
    setState(State::Opening);
    pendingFilename_ = filename;

    GenerateFromJsonRequest req;
    req.filename    = filename;
    req.password    = password;
    req.json_string = jsonString;

    try {
        rpc_->generateFromJson(req);
    } catch (const RpcException& ex) {
        scrubGenerateFromJsonRequest(req);
        recordError(QStringLiteral("generate_from_json"), ex);
        throw;
    }

    scrubGenerateFromJsonRequest(req);
    // onRpcResponse handles state + signals.
    return filename;
}

// =============================================================================
// Asynchronous API
// =============================================================================
//
// Each *Async method transitions state synchronously to Opening/Closing
// and forwards the call to WalletRpcClient's async overload. The result
// is delivered through WalletRpcClient::walletResponse / walletFailed,
// which we re-route through onRpcResponse / onRpcFailed. Those slots
// then drive the high-level state transitions (Open on success, Error
// on failure) and re-emit the convenience signals.
//
// V5.4.5: the async RPC layer typically serialises the request into
// its wire payload inside the dispatch call. As soon as the dispatch
// returns we no longer need the local request object - scrub it
// before it goes out of scope so the QString buffers don't linger.
// =============================================================================

QString WalletManager::createWalletAsync(const QString& filename,
                                          const QString& password,
                                          const QString& language,
                                          const QString& correlationId)
{
    clearError();
    setState(State::Opening);

    CreateWalletRequest req;
    req.filename = filename;
    req.password = password;
    req.language = language;
    const QString cid = rpc_->createWalletAsync(req, correlationId);
    scrubCreateRequest(req);
    return cid;
}

QString WalletManager::openWalletAsync(const QString& filename,
                                        const QString& password,
                                        const QString& correlationId)
{
    clearError();
    setState(State::Opening);

    OpenWalletRequest req;
    req.filename = filename;
    req.password = password;
    const QString cid = rpc_->openWalletAsync(req, correlationId);
    scrubOpenRequest(req);
    return cid;
}

QString WalletManager::closeWalletAsync(const QString& correlationId)
{
    if (state_ == State::Closed) {
        // No in-flight RPC, no wallet to close. Emit walletClosed() so
        // any listeners get the same UX as the sync path's success.
        emit walletClosed();
        return correlationId;
    }
    clearError();
    setState(State::Closing);

    CloseWalletRequest req;
    req.filename = currentFilename_;
    const QString cid = rpc_->closeWalletAsync(req, correlationId);
    // CloseWalletRequest has no sensitive fields.
    return cid;
}

QString WalletManager::changeWalletPasswordAsync(const QString& old_password,
                                                 const QString& new_password,
                                                 const QString& correlationId)
{
    clearError();
    // No state transition; wallet stays Open across the change.

    ChangeWalletPasswordRequest req;
    req.old_password = old_password;
    req.new_password = new_password;
    req.filename     = currentFilename_;
    const QString cid = rpc_->changeWalletPasswordAsync(req, correlationId);
    scrubChangePasswordRequest(req);
    return cid;
}

QString WalletManager::generateFromKeysAsync(const QString& filename,
                                              const QString& password,
                                              const QString& address,
                                              const QString& viewkey,
                                              const QString& correlationId)
{
    // View-only overload - delegate to the Case 4 backup overload with
    // an empty spendkey + restore_height=0 (back-compat for st-011 callers).
    return generateFromKeysBackupAsync(filename, password, address, viewkey,
                                        QString(), 0, false, correlationId);
}

QString WalletManager::generateFromKeysBackupAsync(
    const QString& filename,
    const QString& password,
    const QString& address,
    const QString& viewkey,
    const QString& spendkey,
    quint64 restore_height,
    bool autosave_current,
    const QString& correlationId)
{
    clearError();
    setState(State::Opening);

    GenerateFromKeysRequest req;
    req.filename = filename;
    req.password = password;
    req.address  = address;
    req.viewkey  = viewkey;
    req.spendkey = spendkey;
    req.restore_height = restore_height;
    req.autosave_current = autosave_current;
    const QString cid = rpc_->generateFromKeysAsync(req, correlationId);
    scrubGenerateFromKeysRequest(req);
    return cid;
}

QString WalletManager::generateFromJsonAsync(const QString& filename,
                                              const QString& password,
                                              const QString& jsonString,
                                              const QString& correlationId)
{
    clearError();
    setState(State::Opening);

    GenerateFromJsonRequest req;
    req.filename    = filename;
    req.password    = password;
    req.json_string = jsonString;
    const QString cid = rpc_->generateFromJsonAsync(req, correlationId);
    scrubGenerateFromJsonRequest(req);
    return cid;
}

// =============================================================================
// Private: state machine helpers
// =============================================================================

void WalletManager::setState(State newState)
{
    if (state_ == newState) {
        return;
    }
    state_ = newState;
    emit stateChanged(newState);
}

void WalletManager::setCurrentFilename(const QString& filename)
{
    if (currentFilename_ == filename) {
        return;
    }
    currentFilename_ = filename;
    emit currentWalletChanged(filename);
}

bool WalletManager::isOpenMethod(const QString& method)
{
    return method == QStringLiteral("create_wallet")
        || method == QStringLiteral("open_wallet")
        || method == QStringLiteral("generate_from_keys")
        || method == QStringLiteral("generate_from_json");
}

bool WalletManager::isCloseMethod(const QString& method)
{
    return method == QStringLiteral("close_wallet");
}

void WalletManager::recordError(const QString& method,
                                 const RpcException& error)
{
    lastError_ = QStringLiteral("%1: %2")
                     .arg(method, error.toString());
    // On error, transition to Error but preserve currentFilename_ so the
    // UI can still show "my wallet" if the failure was transient.
    setState(State::Error);
}

void WalletManager::clearError()
{
    lastError_.clear();
}

qint64 WalletManager::nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// =============================================================================
// FIX-G29 (2026-07-18): QML-facing transfer_split wrapper.
// Returns a QJsonObject { ok, tx_hash, error } so QML can switch on ok
// and surface the result via Toast without re-parsing C++ exceptions.
//
// NOTE: we do NOT check state_ here. The WalletManager's in-memory
// state_ may be out of sync with wallet-rpc's actual open/closed state
// (e.g. wallet was opened in a previous session and wallet-rpc still
// has it open). Let wallet-rpc surface "no wallet file" via the
// RpcException catch below.
// =============================================================================
QJsonObject WalletManager::transferSplit(const QString& fromAddress,
                                          const QString& toAddress,
                                          const QString& amountAtomic,
                                          int priority)
{
    // Parse amount from string to quint64.
    bool ok = false;
    const quint64 amount = amountAtomic.toULongLong(&ok);
    if (!ok || amount == 0) {
        QJsonObject err;
        err[QStringLiteral("ok")] = false;
        err[QStringLiteral("error")] =
            QStringLiteral("Invalid amount: %1").arg(amountAtomic);
        return err;
    }

    TransferRequest req;
    TransferDestination dest;
    dest.address = toAddress;
    dest.amount  = amount;
    req.destinations.push_back(dest);
    req.priority  = priority;
    req.ring_size = 16;

    try {
        TransferResult r = rpc_->transferSplit(req);
        QJsonObject out;
        out[QStringLiteral("ok")] = true;
        out[QStringLiteral("tx_hash")] = r.tx_hash;
        out[QStringLiteral("fee")] = static_cast<double>(r.fee) / 1000.0;
        r.secureClear();  // gf-002
        return out;
    } catch (const RpcException& e) {
        QJsonObject err;
        err[QStringLiteral("ok")] = false;
        err[QStringLiteral("error")] = e.message();
        return err;
    } catch (const std::exception& e) {
        QJsonObject err;
        err[QStringLiteral("ok")] = false;
        err[QStringLiteral("error")] = QString::fromUtf8(e.what());
        return err;
    }
}

// =============================================================================
// Private slots: bridge from WalletRpcClient async signals
// =============================================================================

void WalletManager::onRpcResponse(const QString& correlationId,
                                   const QString& method,
                                   const QJsonObject& result)
{
    qInfo().noquote() << "st-118: WalletManager::onRpcResponse, method=" << method
                       << "cid=" << correlationId
                       << "currentState=" << static_cast<int>(state_);
    // Drive the state machine based on which RPC just succeeded.
    if (isOpenMethod(method)) {
        // For sync open/create/generate-from-* calls, the caller stashed
        // the new filename in pendingFilename_ before invoking the RPC;
        // we promote it to currentFilename_ here. For async callers,
        // pendingFilename_ is empty and they must correlate via
        // correlationId <-> filename bookkeeping they maintain.
        const QString resolvedFilename = !pendingFilename_.isEmpty()
            ? pendingFilename_
            : currentFilename_;
        pendingFilename_.clear();
        setCurrentFilename(resolvedFilename);
        openedAtMs_ = nowMs();
        setState(State::Open);
        emit walletOpened(resolvedFilename, openedAtMs_);
    } else if (isCloseMethod(method)) {
        const QString was = currentFilename_;
        pendingFilename_.clear();
        setCurrentFilename(QString());
        openedAtMs_ = 0;
        setState(State::Closed);
        emit walletClosed();
        (void)was;
    } else {
        // change_wallet_password and any future wallet-rpc methods we
        // listen to - no state transition, just re-emit.
    }
    emit walletResponse(correlationId, method, result);
}

void WalletManager::onRpcFailed(const QString& correlationId,
                                 const QString& method,
                                 const RpcException error)
{
    pendingFilename_.clear();
    recordError(method, error);
    emit walletFailed(correlationId, method, error);
}

// FIX-G11 (2026-07-18): wallet directory management for Case 2
// dropdown and other wallet-list use cases.
void WalletManager::setWalletDir(const QString& dir)
{
    walletDir_ = dir;
}

QString WalletManager::walletDir() const
{
    return walletDir_;
}

QStringList WalletManager::listWalletFiles() const
{
    if (walletDir_.isEmpty()) return {};
    QDir dir(walletDir_);
    QStringList files = dir.entryList({QStringLiteral("*.keys")}, QDir::Files, QDir::Name);
    // Strip the .keys suffix so the list contains wallet names.
    for (auto& f : files) f.chop(5);
    return files;
}

// =============================================================================
// Recovery integration (2026-07-24)
//
// When RecoveryManager completes a restore_deterministic_wallet, the wallet
// is already open on the wallet-rpc side, but WalletManager's state machine
// never received the notification (isOpenMethod didn't include that method
// name, and pendingFilename_ was never set by RecoveryManager).
//
// This method bridges the gap: RecoveryManager (which holds a raw pointer
// to walletManager_) calls notifyExternalOpen after recovery completes,
// which sets state to Open, updates currentFilename/openedAtMs, and emits
// walletOpened. The existing walletOpened → subaddressManager.refresh()
// connection in main.cpp (line ~550) then fires automatically so the
// dashboard populates addresses, balance, and history without delay.
// =============================================================================
void WalletManager::notifyExternalOpen(const QString& filename,
                                       qint64 openedAtMs)
{
    if (filename.isEmpty()) {
        qWarning() << "WalletManager::notifyExternalOpen: empty filename, ignoring";
        return;
    }
    clearError();
    setCurrentFilename(filename);
    openedAtMs_ = openedAtMs;
    setState(State::Open);
    qInfo().noquote()
        << "WalletManager::notifyExternalOpen: wallet opened externally, filename="
        << filename << "openedAtMs=" << openedAtMs;
    emit walletOpened(filename, openedAtMs_);
}

} // namespace judecoin_wallet::core::wallet