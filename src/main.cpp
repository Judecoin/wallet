// =============================================================================
// Judecoin Wallet - main.cpp (st-022 + st-024 + st-030a + st-033)
//
// Entry point: QGuiApplication + StartupManager + QQmlApplicationEngine
// wired to qrc:/qml/main.qml. Signal/slot glue drives the QML scene swap
// on startupComplete / startupFailed.
//
// st-022 adds:
//   - BackupParserFacade exposed as a QML context property so the
//     OnboardingWizard -> Case4BackupRestore can call `backupParserObj.parse(payload)`
//   - WalletManager exposed as a QML context property so the wizard can
//     call `walletManagerObj.generateFromKeysBackup(...)` after a
//     successful backup parse
//
// st-024 adds:
//   - QRImageProvider registered with the QML engine so QrCodeImage.qml
//     can render text into a QR via the image://qr/<source>?size=N
//     URL scheme. The provider owns its own QRGenerator (stateless);
//     we add the provider ONCE before any QML scene loads so the
//     first QML render can find it.
//
// V5.4.9 framing: 30-line skeleton + a few context properties. The QML
// UI (OnboardingWizard + Case4BackupRestore / Subaddresses / ReceivePage)
// owns the rest of the surface area.
//
// V5.4.7 compliance: this process never reads key files; the
// startupManager / backupParserObj / walletManagerObj context properties
// carry no secrets. Keys live only in transient QML local properties
// that are forwarded to wallet-rpc and cleared immediately after.
//
// st-030a adds:
//   - QQuickStyle::setStyle("Fusion") so Qt Quick Controls uses the
//     Fusion palette/look. Without Fusion, controls ignore custom
//     stylesheets on some elements and the Forest Green theme shows
//     through inconsistently.
//   - qApp->setStyleSheet("qrc:/qml/theme.qss") so the Forest Green
//     theme (538 lines, st-030) reaches the QQuickWindow widget tree.
//     The qrc path is the one CMake's qt_add_resources block emits:
//     PREFIX "/qml" + relative path "theme.qss" -> qrc:/qml/theme.qss.
//   - Both calls happen BEFORE engine.load() so the stylesheet is
//     active when main.qml instantiates and the first QQuickWindow
//     paints. Setting the style after load() is a no-op for the first
//     window, which is the regression we saw in st-030 real-test.
//
// st-033 adds:
//   - QCommandLineParser wiring for the --page option so the UI can
//     boot directly into a specific top-level page (Dashboard / Send /
//     History / Subaddress / Onboarding / Settings). Default is
//     "Dashboard" if --page is omitted. The validated value is exposed
//     to QML as the initialPage context property so main.qml can
//     StackView.push(...) during Component.onCompleted.
//
// st-040 adds:
//   - No new C++ context properties or signal/slot glue. The post-st-040
//     layout (sidebar + StackView routing) lives entirely in main.qml.
//     This file's only st-040 contribution is the st-040 marker log
//     line emitted right after setContextProperty(initialPage) so a
//     test (or operator) can confirm "main.cpp knows about st-040's
//     routing contract" without having to load the QML scene.
//   - kAllowedPages was extended in st-102 to add "Receive" (the 5th
//     sidebar tab per decision 5a 2026-07-13). The other 6 values
//     (Dashboard / Send / History / Subaddress / Onboarding / Settings)
//     are unchanged from st-033.
//
// st-042 adds:
//   - PriceProvider constructed as a stack-local QObject and exposed
//     to QML as the context property "priceProviderObj". BalanceCard
//     (planned st-050) binds to priceProviderObj.priceUsd /
//     hasPrice / lastError and renders the USD value with
//     priceProviderObj.formatUsd().
//
// st-053 adds:
//   - TransactionTracker constructed as a stack-local QObject and
//     exposed to QML as the context property "transactionTrackerObj".
//     RecentTransactions.qml (the Dashboard "recent transactions"
//     panel) binds to transactionTrackerObj.recentTransfers (a
//     QVariantList slice of the first 5 rows from the tracked
//     snapshot) and renders each row via TxRow.qml (st-052). The
//     tracker itself is unchanged at the C++ business-logic level;
//     st-053 only adds a Q_PROPERTY bridge + a QML scene; the
//     wallet-rpc polling, classification, and note pairing are all
//     still owned by the st-018 module.
//   - We do NOT call transactionTrackerObj->start() here. There is
//     no wallet-rpc available in the dev-env / unit-test launch;
//     the tracker stays in Idle with an empty snapshot, so
//     recentTransfers() returns [] and RecentTransactions.qml
//     renders the "No transactions yet" empty state. Production
//     callers wire start() once the daemon handshake completes.
//   - No CLI flag for the tx feed in V1.0; refresh() fires from
//     TransactionTracker's own 30s poll timer once start() is called.
//   - PriceProvider reads its 5-min QSettings cache at construction
//     time and emits priceChanged() once with the cached value, so
//     the UI never starts at $0.00 when we have a recent fetch.
//   - No CLI flag for the price feed in V1.0; refresh() fires from
//     QML on Dashboard mount and from PriceProvider's own 5-min
//     auto-refresh timer. V5.4.21: V1.0 CoinGecko only, no Settings
//     toggle for the price source.
//
// st-054 adds:
//   - The Dashboard routing contract now lives entirely in main.qml
//     (post-st-040 layout). This file's only st-054 contribution is
//     the st-054 marker log line emitted right after the st-040 marker
//     so a test (or operator) can confirm "main.cpp knows about
//     st-054's Dashboard integration contract" without having to load
//     the QML scene. The Dashboard.qml root re-emits sendClicked /
//     receiveClicked / subaddressesClicked / transactionClicked
//     signals; main.qml's inline Dashboard{} Component wires each one
//     into root._navigateTo(pageName) which calls StackView.replace
//     on the matching page Component.
//   - No new context properties and no new C++ types: st-054 is a
//     pure-QML integration of three pre-existing components
//     (BalanceCard st-050, ActionButtons st-051, RecentTransactions
//     st-053). All three already get their data from the context
//     properties set up by st-042 (priceProviderObj) and st-053
//     (transactionTrackerObj).
//
// st-110 adds:
//   - Three P0 QML context properties wired so every QML guard like
//     `typeof settingsManagerObj !== "undefined" ? settingsManagerObj : null`
//     (used across 8 settings pages + SendPage + ReceivePage +
//     Subaddresses) resolves to a real QObject instead of falling
//     through to `null`:
//       * settingsManagerObj     - settings::SettingsManager, stack-local.
//                                  Owns the per-user QSettings .conf
//                                  (auto-lock / language / reset); ctor
//                                  uses the same kOrganizationName /
//                                  kApplicationName the production
//                                  binary uses, so existing users'
//                                  settings persist across this rewire.
//       * uriHandlerObj          - uri::UriHandler, stack-local,
//                                  wallet-rpc pointer is null (st-111
//                                  wires the real WalletRpcClient in
//                                  a follow-up). parseUriLocally() and
//                                  looksLikeJudecoinUri() work today;
//                                  parseUri()/makeUri() will be enabled
//                                  by st-111 once the RPC client is
//                                  alive.
//       * subaddressManagerObj   - subaddress::SubaddressManager,
//                                  stack-local, LocalDataManager wired
//                                  so the label store is live; the
//                                  wallet-rpc pointer stays null so
//                                  refresh() surfaces
//                                  "WalletRpcClient not wired" until
//                                  st-111 completes.
//
//   - All three are wired BEFORE engine.load() so the moment the QML
//     scene instantiates and main.qml pushes its first page, every
//     settings sub-page + SendPage + ReceivePage + Subaddresses
//     sees real objects on its root context. St-110 only wires the
//     objects; their RPC behavior is verified in st-111 / st-116.
//
//   - The QML side already uses a `typeof X !== "undefined"` guard so
//     pre-st-110 behaviour (settingsManagerObj undefined -> null ->
//     fall-through silent no-op) was the regression we are fixing.
//     See src/qml/{AboutSettings,AdvancedSettings,AppearanceSettings,
//     AutoLockSettings,LanguageSettings,NetworkSettings,ReceivePage,
//     ResetSettings,SendPage,Settings,Subaddresses,
//     TransferConfirmSettings}.qml for the guard sites.
//
// st-111 adds:
//   - The remoteNodeObj context property so NetworkSettings.qml's
//     "Active Daemon Target" frame and AdvancedSettings.qml's
//     "Remote node" diagnostic row resolve to a real
//     RemoteNodeManager instead of falling through to null. Two
//     QML files (NetworkSettings.qml + AdvancedSettings.qml) have
//     been guarding on `typeof remoteNodeObj !== "undefined" ?
//     remoteNodeObj : null` since st-023 / st-080 respectively;
//     pre-st-111 both surfaces showed the placeholder
//     "(RemoteNodeManager not wired)" string.
//
//   - RemoteNodeManager::testConnection() exposed to QML as a
//     Q_INVOKABLE so the NetworkSettings "Test Connection"
//     button can call remoteNodeObj.testConnection(host, port)
//     and switch on the returned ConnectionTestResult enum to
//     render one of four banners (per V5.4 #7 decision): ok /
//     connection_refused / timeout / version_mismatch.
//
//   - startup.setRemoteNodeManager(&remoteNode) so the
//     StartupManager chain's stepReadSettings() picks up the
//     user's remote-node decision on the next start() cycle
//     (V1.0 default = local daemon, remote selection kicks in
//     on user edit + restart).
//
//   - The QML side already uses the same `typeof X !== "undefined"`
//     guard pattern as st-110, so this is a near-mirror of that
//     sub_task. See src/qml/{NetworkSettings,AdvancedSettings}.qml
//     for the guard sites.
// =============================================================================

#include <QGuiApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QQuickStyle>

// st-133b (2026-07-17 01:46): SIGTERM/SIGINT handler so
// the QGuiApplication::aboutToQuit cleanup slot actually fires when
// the UI process is killed externally. Qt 6 does NOT install a
// SIGTERM handler by default - SIGTERM kills the process abruptly,
// bypassing QGuiApplication::quit() and therefore bypassing the
// aboutToQuit signal (the real-test st-133b verify sends SIGTERM to
// the UI PID and expects the daemon to die, which only happens if
// the aboutToQuit slot runs to completion). We use signal(2) + a
// trivial handler that just calls QGuiApplication::quit(); QGuiApp
// ::quit() is documented as signal-safe enough for this case (it
// just flips an atomic flag).
#include <signal.h>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QStringList>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QDebug>
#include <QImage>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QEventLoop>
#include <QTimer>
#include <QMessageBox>      // FIX-G3 (2026-07-17 17:24): top-level exception dialog
#include <exception>         // FIX-G3: std::current_exception / std::rethrow_exception
#include <cstdlib>           // FIX-G3: std::_Exit in terminate handler

#include "core/commands/CommandSetLoader.h"
#include "core/startup/StartupManager.h"
#include "core/util/BackupParserFacade.h"
#include "core/tracker/TransactionTracker.h"
#include "core/tracker/BalanceTracker.h"
#include "core/util/PriceProvider.h"
#include "core/lock/LockManager.h"
#include "core/localdata/LocalDataManager.h"
#include "core/util/ClipboardHelper.h"
#include "core/util/QRImageProvider.h"
#include "core/util/QrCodeItem.h"
#include "core/wallet/WalletManager.h"
#include "core/wallet/RecoveryManager.h"
#include "core/settings/SettingsManager.h"
#include "core/i18n/I18nManager.h"
#include "core/uri/UriHandler.h"
#include "core/subaddress/SubaddressManager.h"
#include "core/network/RemoteNodeManager.h"
#include "core/process/ProcessManager.h"
#include "core/network/PortConflictDetector.h"
#include "core/rpc/DaemonClient.h"

using judecoin_wallet::core::commands::CommandSet;
using judecoin_wallet::core::startup::StartupManager;
using judecoin_wallet::core::util::BackupParserFacade;
using judecoin_wallet::core::util::PriceProvider;
using judecoin_wallet::LockManager;
using judecoin_wallet::ClipboardHelper;
using judecoin_wallet::QRImageProvider;
using judecoin_wallet::core::tracker::TransactionTracker;
using judecoin_wallet::core::tracker::BalanceTracker;
using judecoin_wallet::core::localdata::LocalDataManager;
using judecoin_wallet::core::wallet::WalletManager;
using judecoin_wallet::core::wallet::RecoveryManager;
using judecoin_wallet::core::settings::SettingsManager;
// Note: I18nManager is declared at the global namespace (see
// src/core/i18n/I18nManager.h); only LanguageInfo and
// kSupportedLanguages() live inside `judecoin_wallet::core::i18n`.
// We use I18nManager directly without a using-declaration, the same
// way QRImageProvider is used a few lines above.
using judecoin_wallet::core::uri::UriHandler;
using judecoin_wallet::core::subaddress::SubaddressManager;
using judecoin_wallet::core::network::RemoteNodeManager;
using judecoin_wallet::core::process::ProcessManager;
using judecoin_wallet::core::network::PortConflictDetector;
using judecoin_wallet::core::rpc::DaemonClient;
using judecoin_wallet::core::wallet::RecoveryManager;

namespace {
// st-033 + st-102: closed set of valid --page values. Kept inline (no
// header) so the constant lives next to the only consumer (this
// translation unit). Order matches main.qml StackView order so visual
// diffs stay stable. st-102 adds "Receive" (5th sidebar tab,
// decision 5a 2026-07-13) so `--page Receive` is a valid CLI value;
// main.qml routes it via mainStack.push(receivePage) the same way the
// Dashboard's ActionButtons Receive CTA does.
const QSet<QString> kAllowedPages = {
    QStringLiteral("Dashboard"),
    QStringLiteral("Receive"),
    QStringLiteral("Send"),
    QStringLiteral("History"),
    QStringLiteral("Subaddress"),
    QStringLiteral("Onboarding"),
    QStringLiteral("Settings")
};
} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Suppress Qt accessibility cache debug noise — these flood stderr and
    // make it impossible to read actual application log output.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.accessibility.cache=false"));

    QCoreApplication::setOrganizationName(QStringLiteral("JudecoinWallet"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("judecoin.local"));
    QCoreApplication::setApplicationName(QStringLiteral("JudecoinWallet"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // st-133b (2026-07-17 01:46): route SIGTERM/SIGINT to
    // QGuiApplication::quit() so the aboutToQuit cleanup slot actually
    // runs when the UI process is killed externally. Without this,
    // `kill -TERM <UI_PID>` kills the process abruptly and the daemon
    // (detached via QProcess::startDetached) survives - exactly the bug
    // we are fixing. We use signal(2) (POSIX.1-2008 async-signal-safe
    // for the call itself; QGuiApplication::quit is a single atomic
    // flag flip that is documented as safe to call from a signal
    // handler context). SIGINT gets the same treatment so Ctrl-C in a
    // terminal also triggers cleanup.
    //
    // We CANNOT install the handler inside the lambda (it must be a
    // plain C function); and we cannot call QGuiApplication::quit()
    // before app exists (it is a static method that does work without
    // an instance, but the static initialization order is fragile).
    // Solution: declare a tiny file-static C function below main and
    // install it here, immediately after app exists.
    auto sigtermHandler = +[](int sig) {
        // async-signal-safe path: just flip the quit flag. No logging,
        // no QFile, no QObject - those are not safe in a handler.
        QGuiApplication::quit();
        (void)sig;
    };
    // Use sigaction (POSIX) so we replace any prior Qt/parent handler
    // deterministically. signal(2) has BSD vs SysV quirks that we
    // don't want on a project that ships across distros.
    {
        struct sigaction sa {};
        sa.sa_handler = sigtermHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT,  &sa, nullptr);
    }
    qInfo().noquote()
        << "st-133b: SIGTERM/SIGINT -> QGuiApplication::quit() handler"
        << "installed (so aboutToQuit fires on external kill)";

    // FIX-G3 (2026-07-17 17:24): top-level terminate handler.
    // Without this, any std::exception that escapes through the QML
    // try/catch boundary (e.g. WalletManager::createWallet's RpcException
    // reaching the main thread event loop) results in std::terminate ->
    // core dump with no UI feedback. Decision: connection failure
    // should show an error dialog instead of terminating.
    //
    // We install AFTER QGuiApplication exists so we can call QMessageBox
    // modal dialog. The handler must NOT throw (we're already inside
    // std::terminate context), and must terminate the process via
    // std::_Exit (NOT exit() which runs atexit handlers in an unsafe
    // state).
    std::set_terminate([]() {
        std::exception_ptr eptr = std::current_exception();
        QString msg = QStringLiteral("Unknown terminate (no current exception)");
        if (eptr) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                msg = QString::fromUtf8(e.what());
            } catch (...) {
                msg = QStringLiteral("Unknown non-std exception");
            }
        }
        qCritical().noquote()
            << "FIX-G3: std::terminate caught:" << msg;

        // Best-effort modal dialog. If we're already inside a Qt event
        // loop spin or the QGuiApplication has been torn down, this is
        // a no-op (QMessageBox::critical returns silently).
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Judecoin Wallet - Error"),
            QStringLiteral("Application terminated abnormally. Please screenshot and report this error:\n\n%1").arg(msg));

        // _Exit skips atexit handlers / static destructors because we
        // don't know what state the process is in.
        std::_Exit(1);
    });
    qInfo().noquote()
        << "FIX-G3: std::set_terminate handler installed (QMessageBox"
        << "modal + std::_Exit(1) on unhandled exception)";

    StartupManager startup;

    // st-022: st-022 wires BackupParser + WalletManager into the QML
    // context so OnboardingWizard -> Case4BackupRestore can drive a
    // backup restore end-to-end. Both are stack-allocated so their
    // lifetime is tied to the engine; the engine's rootContext owns the
    // reference.
    BackupParserFacade backupParser;
    WalletManager walletManager;

    // st-053: TransactionTracker (st-018) exposed to QML as
    // `transactionTrackerObj` so RecentTransactions.qml can bind
    // `model: transactionTrackerObj.recentTransfers` and render up to 5
    // rows on the Dashboard. Stack-allocated; we do NOT call start() in
    // main.cpp because there is no wallet-rpc available in this binary
    // (the daemon / wallet-rpc pair is owned by the production runtime,
    // not the dev-env unit-test launch). The tracker stays in Idle with
    // an empty `transfers_` vector, so recentTransfers() returns [] and
    // RecentTransactions.qml shows its "No transactions yet" empty
    // state. Production callers (sub_tasks that wire the daemon) call
    // transactionTrackerObj->setWalletRpcClient(...) and ->start(...) at
    // the appropriate lifecycle point; that wiring is out of scope for
    // st-053 modify (Dashboard UI shell only).
    TransactionTracker transactionTracker;

    // st-162 (2026-07-18): BalanceTracker polls wallet-rpc's
    // get_accounts every 30s and exposes totalBalance / unlockedBalance
    // to QML via `balanceTrackerObj` so the Dashboard hero card renders
    // live Available / Locked balances.
    BalanceTracker balanceTracker;

    // st-062: LocalDataManager (st-017) exposed to QML as
    // `localDataManagerObj`. HistoryPage.qml's TxDetailDialog sub-page
    // calls `localDataManagerObj.saveTxNote(hash, note)` / `.getTxNote
    // (hash)` / `.removeTxNote(hash)` to read+write the per-tx note
    // metadata. The C++ side already wires the tracker (st-018) to the
    // same LocalDataManager via setLocalDataManager() at the production
    // wire-up point; here we only need to expose the manager itself so
    // the QML save path doesn't have to round-trip through a
    // property-less facade. We do NOT call loadAll() here - the
    // production runtime calls loadAll() once the wallet-rpc handshake
    // completes, and the dev-env unit-test launch never reads tx_notes.
    LocalDataManager localDataManager;

    // st-110: SettingsManager (st-014) exposed to QML as
    // `settingsManagerObj`. The 8 settings pages (Network / Language /
    // AutoLock / TransferConfirm / Appearance / Advanced / Reset /
    // About) bind to settingsManagerObj for read+write of
    // language / autoLockSeconds / transferConfirmDialog /
    // transferConfirmPassword and call settingsManager.reset() from
    // ResetSettings. Stack-allocated; ctor uses
    // SettingsManager::ensureApplicationIdentity() to set
    // QCoreApplication organizationName + applicationName to the same
    // kOrganizationName / kApplicationName constants the production
    // binary uses ("JudecoinWallet" / "JudecoinWallet"), so the
    // per-user .conf lives at the canonical
    // ~/.config/JudecoinWallet/JudecoinWallet.conf path. No wiring
    // needed - QSettings is self-managed inside the ctor.
    //
    // We construct it here (early, next to localDataManager) so the
    // OrganizationName is set before any QML scene load that might
    // also create settings on the side. The QML side already uses a
    // `typeof settingsManagerObj !== "undefined"` guard, so pre-st-110
    // settings pages fell through to a null reference and silently
    // no-op'd every write; st-110 makes those writes effective.
    SettingsManager settingsManager;

    // st-132 (2026-07-16 21:59): wire I18nManager so that
    // SettingsManager::languageLocaleChanged swaps the active QTranslator
    // and QML's qsTr() bindings re-evaluate. Before st-132 the language
    // picker wrote to QSettings but no QTranslator reload happened, so
    // selecting "Japanese" in LanguageSettings.qml had no visible effect
    // until app restart. I18nManager is stack-allocated; its QTranslator*
    // is owned and freed in the dtor. We construct it AFTER settingsManager
    // so we can read the initial language locale at app start, then call
    // setCurrentLanguage (which installs the matching .qm from :/i18n on
    // success). The lambda-thunk on the connect is needed because
    // I18nManager::setCurrentLanguage is Q_INVOKABLE rather than a slot;
    // QObject::connect can target Q_INVOKABLE methods but going through a
    // lambda keeps the binding obvious at the call site.
    I18nManager i18nManager;
    i18nManager.setCurrentLanguage(settingsManager.languageLocale());

    // DIAGNOSTIC: test C++ translate BEFORE QML loads
    qInfo().noquote()
        << "PRE-QML translate(main, Idle) ="
        << QCoreApplication::translate("main", "Idle");

    QObject::connect(&settingsManager,
                     &SettingsManager::languageLocaleChanged,
                     &i18nManager,
                     [&i18nManager](const QString& locale) {
                         i18nManager.setCurrentLanguage(locale);
                     });

    // st-110: UriHandler (st-020) exposed to QML as
    // `uriHandlerObj`. SendPage.qml's "Paste URI" button +
    // ReceivePage.qml's "Generate URI" button + QrCodeImage.qml's
    // makeUri() helper all bind to uriHandlerObj. Default ctor is
    // used here - WalletRpcClient pointer stays null. Implications:
    //
    //   - parseUriLocally() works today (pure string parser, no RPC)
    //     so the Send page "Paste URI" auto-fill of To / Amount /
    //     Payment ID fires immediately.
    //   - looksLikeJudecoinUri() works today (pure bool check).
    //   - parseUri() / makeUri() stay disabled until st-111 wires
    //     the real WalletRpcClient into uriHandler.setWallet(...) -
    //     until then QML calls return empty strings / surface
    //     RpcException("no wallet wired").
    //
    // Stack-allocated, lifetime tied to the engine via
    // setContextProperty - the engine owns the reference; engine
    // destruction drops the UriHandler.
    UriHandler uriHandler;

    // st-110: SubaddressManager (st-019) exposed to QML as
    // `subaddressManagerObj`. SendPage.qml's "From" ComboBox model
    // binds to subaddressManagerObj.entries; Subaddresses.qml's
    // ListView binds to the same. The ctor here takes the existing
    // localDataManager (st-062) so the SubaddressLabel store is
    // wired and the per-subaddress "Subaddress #N - Label" labels
    // stay consistent with what the rest of the wallet writes.
    //
    // WalletRpcClient pointer is null - refresh() will surface
    // "WalletRpcClient not wired" until st-111 wires the real client.
    // That's the documented behaviour (see SubaddressManager::refresh
    // header) and the correct contract for st-110: the OBJECT is
    // present so QML guards don't fall through to null, but the
    // entries() vector stays empty until the daemon-side wiring is
    // done by st-111. SendPage already tolerates an empty model
    // (it falls back to "<no subaddresses yet>" text).
    //
    // We MUST construct this AFTER localDataManager since the ctor
    // dereferences localDataManager to wire its label store.
    SubaddressManager subaddressManager(&localDataManager);

    // FIX-G12 (2026-07-18): wire the real WalletRpcClient into
    // SubaddressManager so SendPage.qml's "From" ComboBox and
    // Subaddresses.qml's ListView get populated entries. The st-110
    // comment documented "WalletRpcClient pointer is null" as a known
    // gap with st-111 as the fix owner; st-111 never materialised.
    // We wire the client NOW so refresh() can reach the daemon and
    // subaddressesUpdated() fires with real data. The raw .data()
    // pointer is safe because walletManager outlives subaddressManager
    // (both are stack-allocated in main() and walletManager is declared
    // before subaddressManager, so it destructs later).
    //
    // We also connect walletOpened -> refresh() so the subaddress list
    // populates automatically every time a wallet is opened (new wallet,
    // re-open, or recovery).
    subaddressManager.setWalletRpcClient(walletManager.rpc().data());

    // st-162 (2026-07-18): BalanceTracker polls wallet-rpc's
    // get_accounts every 30s and pushes totalBalance / unlockedBalance
    // to QML via balanceTrackerObj. Wire the client and start tracking
    // here (same lifecycle point as subaddressManager).
    balanceTracker.setWalletRpcClient(walletManager.rpc());
    balanceTracker.startTracking();

    QObject::connect(&walletManager, &WalletManager::walletOpened,
                     &subaddressManager, [&subaddressManager, &localDataManager](const QString&, qint64) {
                         // FIX (2026-07-18): loadAll() must be called BEFORE
                         // refresh() so the label store has the saved subaddress
                         // indices from disk. Without this, refresh() step 2 finds
                         // no indices and only the primary address is shown.
                         localDataManager.loadAll();
                         subaddressManager.refresh();
                     });
    // If the wallet is already open when we wire (e.g. app restart with
    // persistent wallet), walletOpened won't fire again. Refresh now.
    if (walletManager.state() == WalletManager::State::Open) {
        localDataManager.loadAll();
        subaddressManager.refresh();
    }

    // st-111: RemoteNodeManager (st-023) exposed to QML as
    // `remoteNodeObj`. The ctor takes the freshly-built settingsManager
    // (st-110 step) so refresh() can re-resolve the active host/port
    // straight from QSettings. NetworkSettings.qml's "Test Connection"
    // button calls remoteNodeObj.testConnection(host, port) which
    // returns a ConnectionTestResult enum the QML side renders as one
    // of four user-visible banners (per V5.4 #7 decision). AdvancedSettings
    // .qml binds to remoteNodeObj.activeLabel for the diagnostic
    // "Remote node" row in the collapsed daemon-endpoint group.
    //
    // We MUST construct this AFTER settingsManager (RemoteNodeManager
    // holds a non-owning pointer to it and calls settingsManager.presetNode()
    // / .isCustomRemote() / .remoteDaemonHost() / .remoteDaemonPort()
    // from inside refresh()).
    RemoteNodeManager remoteNode(&settingsManager);

    // st-117 (2026-07-16 16:08): wire StartupManager's 4 required
    // dependencies + the optional RemoteNodeManager BEFORE start()
    // runs, so step 0's dep-null check at StartupManager.cpp:101
    // does not bail out with "missing dependency (settings, process,
    // portDetector, or daemon not wired)" before the 5-step boot
    // chain even begins.
    //
    // Failure mode that motivated this patch: launching the AppImage
    // directly via `DISPLAY=:0 QT_QPA_PLATFORM=xcb ...AppImage`
    // printed st-110/st-111 markers (settingsManagerObj /
    // remoteNodeObj / etc) but then immediately died with
    //   Startup failed at step 0 :
    //       "StartupManager: missing dependency ... or daemon not wired"
    //   StartupManager::start() refused (already running?)
    // Root cause: main.cpp constructed `StartupManager startup;`
    // empty (no convenience ctor used) and called exactly ONE setter
    // (setRemoteNodeManager), and that one set AFTER start(). The
    // other three setters (settings / process / portDetector /
    // daemon) were simply never invoked; ProcessManager /
    // PortConflictDetector / DaemonClient were never even
    // instantiated in this TU. The empty StartupManager then refused
    // start().
    //
    // Fix shape (this version): stack-construct the three missing
    // managers NEXT TO settingsManager / remoteNode (so lifetime is
    // identical: they outlive the engine, are dropped in LIFO order
    // at scope exit), then call all five setters BEFORE start().
    //
    // Placement: AFTER `RemoteNodeManager remoteNode(&settingsManager);`
    // (this line) but BEFORE the st-112 RecoveryManager block. This
    // ensures every C++ identifier referenced by the setters is
    // already declared - the earlier patch (commit 9c5365f) placed
    // the wire block right after `StartupManager startup;`, which
    // tripped "settingsManager/remoteNode not declared" at compile
    // time because those stack objects come later in main().
    //
    // Stack-allocated, non-owning pointers; StartupManager does not
    // take ownership. Setter ordering per StartupManager.h:
    // "Dependency wiring (only valid before start() is called)".

    // st-172 (2026-07-19): re-resolve the active daemon target whenever
    // the user SAVES network settings, so NetworkSettings.qml's
    // "Active Daemon Target" frame updates live. Previously refresh()
    // only ran at construction + StartupManager::stepReadSettings, so
    // a Save showed stale info until app restart. refresh() is cheap
    // (pure in-memory resolution, no I/O). Zero-arg lambda is fine for
    // signals carrying arguments; &remoteNode as context bounds the
    // connection lifetime.
    auto refreshRemoteNode = [&remoteNode]() { remoteNode.refresh(); };
    QObject::connect(&settingsManager, &SettingsManager::isCustomRemoteChanged,
                     &remoteNode, refreshRemoteNode);
    QObject::connect(&settingsManager, &SettingsManager::presetNodeChanged,
                     &remoteNode, refreshRemoteNode);
    QObject::connect(&settingsManager, &SettingsManager::remoteDaemonHostChanged,
                     &remoteNode, refreshRemoteNode);
    QObject::connect(&settingsManager, &SettingsManager::remoteDaemonPortChanged,
                     &remoteNode, refreshRemoteNode);
    ProcessManager   processManager;
    PortConflictDetector portDetector;
    DaemonClient     daemonClient;

    startup.setSettingsManager(&settingsManager);
    startup.setProcessManager(&processManager);
    startup.setPortConflictDetector(&portDetector);
    startup.setDaemonClient(&daemonClient);
    startup.setRemoteNodeManager(&remoteNode);
    // FIX-G7: wire WalletManager so StartupManager can update
    // its WalletRpcClient endpoint to the negotiated port.
    startup.setWalletManager(&walletManager);
    qInfo().noquote()
        << "st-117: StartupManager dependencies wired (settings /"
        << "process / portDetector / daemon / remoteNode); start()"
        << "step 0 dep-null check will now pass";

    // st-118 (2026-07-16 16:32): set absolute paths for the bundled
    // judecoind + judecoin-wallet-rpc binaries that AppRun exposes
    // via the JUDECOIN_DAEMON_BIN / JUDECOIN_WALLET_RPC_BIN env vars
    // (see packaging/AppRun near the PATH export, paired with this
    // change). ProcessManager.startDetachedAndCapturePid() at
    // src/core/process/ProcessManager.cpp:128-130 does a
    // QFileInfo(program) pre-check that does NOT search PATH - so
    // a relative "judecoind" string fails the existence check
    // (exposed at 16:24 while running the rebuilt AppImage:
    // "ProcessManager: program not found / not executable: judecoind"
    // followed by "Startup failed at step 2"). Feeding the absolute
    // paths here sidesteps the PATH-lookup mismatch. Wired through:
    //   setXxxProgramPath -> daemonProgramPath_ / walletRpcProgramPath_
    //                          field
    //   stepStartDaemon / stepStartWalletRpc (StartupManager.cpp:409
    //                          / :500) -> ProcessSpec.program
    //                          = daemonProgramPath_ / walletRpcProgramPath_
    //   ProcessManager.startDaemon() -> startDetachedAndCapturePid(spec)
    //                          -> QFileInfo(spec.program) -> ok
    //
    // Falls back to the existing default ("judecoind" /
    // "judecoin-wallet-rpc") if the env vars are absent (dev-env unit
    // tests, or in-tree launchers that bypass AppRun).
    {
        const QByteArray daemonBin = qgetenv("JUDECOIN_DAEMON_BIN");
        const QByteArray walletRpcBin = qgetenv("JUDECOIN_WALLET_RPC_BIN");
        if (!daemonBin.isEmpty()) {
            startup.setDaemonProgramPath(QString::fromLocal8Bit(daemonBin));
        }
        if (!walletRpcBin.isEmpty()) {
            startup.setWalletRpcProgramPath(QString::fromLocal8Bit(walletRpcBin));
        }
        qInfo().noquote()
            << "st-118: daemon bin ="
            << (daemonBin.isEmpty()
                    ? QStringLiteral("(unset, using default \"judecoind\")")
                    : QString::fromLocal8Bit(daemonBin))
            << "; wallet-rpc bin ="
            << (walletRpcBin.isEmpty()
                    ? QStringLiteral("(unset, using default \"judecoin-wallet-rpc\")")
                    : QString::fromLocal8Bit(walletRpcBin));
    }

    // st-134 (XDG migration + JUDECOIN_DATA_DIR env override):
    // Forward the JUDECOIN_DATA_DIR env var into SettingsManager so
    // it lands in QSettings BEFORE StartupManager::cachedDataDir_ is
    // populated. Paired with packaging/AppRun §8.6 which sets the
    // env var to mirror SettingsManager's XDG-aware default. main.cpp
    // is the single env-injection point for this value; mirrors the
    // JUDECOIN_DAEMON_BIN / JUDECOIN_WALLET_RPC_BIN handling above.
    //
    // setDataDirectory() persists the value, so subsequent reads
    // return the override even if JUDECOIN_DATA_DIR is unset (lets
    // the user edit the path in the Settings page after a one-shot
    // env override).
    {
        const QByteArray envDataDir = qgetenv("JUDECOIN_DATA_DIR");
        if (!envDataDir.isEmpty()) {
            const QString resolved = QString::fromLocal8Bit(envDataDir);
            settingsManager.setDataDirectory(resolved);
            qInfo().noquote()
                << "st-134: JUDECOIN_DATA_DIR override ->"
                << settingsManager.dataDirectory();
        } else {
            qInfo().noquote()
                << "st-134: JUDECOIN_DATA_DIR unset, default ="
                << settingsManager.dataDirectory();
        }
    }

    // st-112: RecoveryManager (st-012) exposed to QML as
    // `recoveryManagerObj`. Case3Recover.qml has been running with a
    // `typeof recoveryManagerObj !== "undefined" ? recoveryManagerObj
    // : null` guard since st-072 — pre-st-112 the guard fell through
    // to null and _submit() fell through to a local-Timer simulation.
    // st-112 replaces that sim with a real recoveryManagerObj.
    // startRecoveryAsync({filename, password, mnemonic}) call, with
    // progressBound via progressCurrent()/progressTotal() refresh
    // paths and recoveryComplete / recoveryFailed signal handlers.
    //
    // Stack-allocated; default ctor leaves the WalletRpcClient pointer
    // null so startRecoveryAsync() will surface a "walletRpc_ is null"
    // diagnostic in dev-env mode (V5.4.7 says the wallet-rpc wiring
    // lives in the production runtime, not the dev-env test harness).
    // The QML side already tolerates this by checking state via the
    // `recoveryManager` guard (per st-072 contract) and falling back
    // to the simulation only if recoveryManagerObj is undefined —
    // once main.cpp wires the real object the QML side will reach the
    // real RPC path every time.
    RecoveryManager recoveryManager(&walletManager, &daemonClient);
    qInfo().noquote()
        << "st-112: recoveryManagerObj constructed with WalletManager"
        << "+ DaemonClient (real RPC wired). Case3Recover.qml will now"
        << "call recoveryManagerObj.startRecoveryAsync at submit time.";

    // st-042: USD price feed. Stack-allocated; lifetime tied to the
    // engine. The QML side (Dashboard -> BalanceCard, planned st-050)
    // binds priceProviderObj.priceUsd / hasPrice / lastError.
    // The QML side calls priceProviderObj.refresh() on page mount;
    // PriceProvider's own QTimer fires refresh() every 5 min
    // automatically (kDefaultAutoRefreshMs).
    PriceProvider priceProvider;
    priceProvider.setAutoRefreshInterval(PriceProvider::kDefaultAutoRefreshMs);
    qInfo().noquote()
        << "st-042: PriceProvider ready; default apiBase=api.coingecko.com"
        << "cache=" << PriceProvider::kDefaultCacheMs << "ms"
        << "auto-refresh=" << PriceProvider::kDefaultAutoRefreshMs << "ms";

    // st-024: QR image provider. Owned by the engine via

    // st-033: CLI --page routing. QCommandLineParser needs the live
    // QGuiApplication for --help / --version handling, so this must run
    // after app exists but before we hand the parsed value to QML.
    //
    // Contract (locked by st-033 spec):
    //   --page <Dashboard|Send|History|Subaddress|Onboarding|Settings>
    //   default: Dashboard (when --page is omitted entirely)
    //   unknown value: print help + exit 1
    QCommandLineParser cliParser;
    cliParser.setApplicationDescription(
        QStringLiteral("Judecoin Wallet V1.0 - mainnet-only GUI client"));
    cliParser.addHelpOption();
    cliParser.addVersionOption();
    QCommandLineOption pageOption(
        QStringList{ QStringLiteral("p"), QStringLiteral("page") },
        QStringLiteral("Initial page to show on startup. "
                       "Allowed: Dashboard, Send, History, Subaddress, "
                       "Onboarding, Settings. Default: Dashboard."),
        QStringLiteral("name"),
        QStringLiteral("Dashboard"));
    cliParser.addOption(pageOption);

    // st-097b: --screenshot <path> headless capture flag. Default value
    // is the empty string so isSet()-style probing is unnecessary: a
    // non-empty cliParser.value(screenshotOption) is the canonical
    // "screenshot mode is on" signal. The flag is what st-097c
    // baseline-capture harness invokes: it boots the QML scene in
    // offscreen mode, grabs the QQuickWindow, writes a PNG, and exits
    // without starting StartupManager (no daemon / wallet-rpc required).
    QCommandLineOption screenshotOption(
        QStringList{ QStringLiteral("s"), QStringLiteral("screenshot") },
        QStringLiteral("Capture the rendered QQuickWindow to the given "
                       "PNG path and exit without starting StartupManager. "
                       "Used by the st-097c baseline-capture harness."),
        QStringLiteral("path"),
        QStringLiteral(""));  // empty default = feature off
    cliParser.addOption(screenshotOption);

    cliParser.process(app);

    const QString initialPage = cliParser.value(pageOption);
    if (!kAllowedPages.contains(initialPage)) {
        // Focused diagnostic + standard parser help banner so the user
        // sees --page description. showHelp(1) prints usage to stderr
        // and exits with code 1 in one call.
        qCritical().noquote()
            << "st-033: unknown --page value:" << initialPage
            << "- allowed:" << kAllowedPages.values();
        cliParser.showHelp(1);
    }
    qInfo() << "st-033: --page" << initialPage
            << "(initialPage context property will be set on engine)";


    // st-097b: parse --screenshot <path>. An empty value means the
    // feature is off and the binary follows the normal StartupManager
    // flow. A non-empty value flips screenshotMode on; the
    // if (screenshotMode) { ... } short-circuit below the StartupManager
    // connect()s loads main.qml directly, grabs the QQuickWindow, writes
    // the PNG, and exits with code 0. No daemon / wallet-rpc is touched
    // in that branch.
    const QString screenshotPath = cliParser.value(screenshotOption);
    const bool screenshotMode = !screenshotPath.isEmpty();
    if (screenshotMode) {
        qInfo().noquote()
            << "st-097b: --screenshot mode active; path=" << screenshotPath
            << "- StartupManager will NOT run; binary exits after PNG save";
    }






























    // FIX-G19 (2026-07-19): register QrCodeItem (QQuickPaintedItem)
    // as the primary QR renderer for QML. It draws QR codes directly
    // via QPainter, bypassing the image://qr/ provider pipeline which
    // suffered from aspect-ratio distortion inside Layout containers.
    // The old QRImageProvider is kept for backward-compat but no longer
    // used by QrCodeImage.qml (now a thin wrapper around QrCodeItem).
    qmlRegisterType<judecoin_wallet::QrCodeItem>(
        "judecoin.wallet.core", 1, 0, "QrCodeItem");

    // addImageProvider (which takes ownership). The provider is
    // stateless - all data flows through requestImage() per URL -
    // so a single instance is enough for the whole QML scene tree.
    QRImageProvider *qrProvider = new QRImageProvider();

    // st-030a: theme wiring MUST happen before QQmlApplicationEngine
    // is constructed (or at the very latest before its first load).
    // QQuickStyle::setStyle is a process-wide singleton; calling it
    // here means any QQuickStyle-aware Controls delegate picks up
    // Fusion the moment the engine instantiates the first QML window.
    //
    // The qrc:/qml/theme.qss URL is the one CMake's qt_add_resources
    // block emits with PREFIX "/qml" + file qml/theme.qss. If the
    // resource is missing the load returns an empty string and the
    // app silently falls back to the default style - this is exactly
    // the st-030 real-test failure mode we are fixing.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // V5.4.9 no_widgets -> QGuiApplication, which has no setStyleSheet()
    // (that method lives on QApplication in Qt Widgets). The Forest Green
    // qss is qrc-registered so the resource is reachable as
    // qrc:/qml/theme.qss; we probe it here with QFile so the proof is
    // "the qss is bundled into the binary AND readable". QtQuick
    // Controls pick up Fusion via the setStyle call above; the Forest
    // Green colors themselves are owned by Theme.qml (planned st-041).
    //
    // If a future sub_task opts into QApplication + Qt Widgets (e.g. to
    // theme native dialogs), the setStyleSheet call becomes a one-liner:
    //   qApp->setStyleSheet(QStringLiteral("qrc:/qml/theme.qss"));
    QFile qssProbe(QStringLiteral(":/qml/theme.qss"));
    if (qssProbe.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const qint64 qssSize = qssProbe.size();
        qInfo() << "st-030a: qrc:/qml/theme.qss loaded, size=" << qssSize
                << "bytes (Forest Green theme is qrc-bundled)";
        qssProbe.close();
    } else {
        qWarning() << "st-030a: qrc:/qml/theme.qss NOT readable:"
                   << qssProbe.errorString()
                   << "- the qt_add_resources wiring is broken";
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("qr"), qrProvider);

    // FIX (2026-07-20): wire the QML engine into I18nManager so
    // retranslate() fires after every QTranslator swap. Without this,
    // qsTr() bindings cache the first language and stay frozen even
    // after setCurrentLanguage() installs a new .qm translator.
    i18nManager.setEngine(&engine);

    // Qt 6.6.3 QML StackView limitation: retranslate() cannot reliably
    // refresh all hidden/cached pages after a runtime language switch.
    // Qt 6.6.3 QML runtime language switch is fundamentally broken.
    // The ONLY reliable approach: spawn a new process + exit the current
    // one. The new process loads the translator before QML boots.
    //
    // CRITICAL: We use std::_Exit() — NOT QCoreApplication::quit().
    // quit() fires aboutToQuit which kills the daemon + wallet-rpc,
    // leaving the new process unable to connect. std::_Exit() skips
    // all cleanup — the daemon stays alive and the new process
    // connects to the same running daemon.
    QObject::connect(&i18nManager, &I18nManager::restartRequested,
                     &app, [&walletManager]() {
        // Close the wallet via wallet-rpc RPC before restart.
        // The wallet-rpc process stays alive; we just close the wallet
        // so the new process can open_wallet without "already opened".
        if (walletManager.state() == WalletManager::State::Open) {
            qInfo().noquote()
                << "i18n: closing wallet before language restart";
            try {
                walletManager.closeWallet();
                qInfo().noquote()
                    << "i18n: wallet closed OK";
            } catch (const std::exception& e) {
                qWarning().noquote()
                    << "i18n: closeWallet failed:" << e.what()
                    << "— continuing restart anyway";
            }
        }

        const QString appPath = QCoreApplication::applicationFilePath();
        qInfo().noquote()
            << "i18n: spawning new process (daemon + wallet-rpc stay alive)";
        if (QProcess::startDetached(appPath, QStringList())) {
            qInfo().noquote()
                << "i18n: new process spawned — exiting via std::_Exit";
        } else {
            qWarning().noquote()
                << "i18n: failed to spawn; user must restart manually";
        }
        // _Exit skips aboutToQuit / atexit → daemon + wallet-rpc survive
        std::_Exit(0);
    });

    // ClipboardHelper exposes QGuiApplication::clipboard()->setText() to QML.
    ClipboardHelper clipboardHelper;
    engine.rootContext()->setContextProperty(
        QStringLiteral("clipboardHelper"), &clipboardHelper);

    // LockManager monitors user inactivity and triggers wallet lock.
    LockManager lockManager;
    lockManager.setSettingsManager(&settingsManager);
    lockManager.setWalletManager(&walletManager);
    engine.rootContext()->setContextProperty(
        QStringLiteral("lockManagerObj"), &lockManager);

    engine.rootContext()->setContextProperty(
        QStringLiteral("startupManager"), &startup);
    engine.rootContext()->setContextProperty(
        QStringLiteral("backupParserObj"), &backupParser);
    engine.rootContext()->setContextProperty(
        QStringLiteral("walletManagerObj"), &walletManager);
    // FIX-G11 (2026-07-18): tell WalletManager where wallet-rpc
    // stores .keys files so Case2Open.qml can list existing wallets
    // via listWalletFiles(). The path mirrors the --wallet-dir argument
    // passed to wallet-rpc in StartupManager.cpp.
    walletManager.setWalletDir(settingsManager.dataDirectory() + QStringLiteral("/wallets"));
    // FIX-G11 (2026-07-18): tell WalletManager where wallet-rpc
    // stores .keys files so Case2Open.qml can list existing wallets
    // via listWalletFiles(). The path mirrors the --wallet-dir argument
    // passed to wallet-rpc in StartupManager.cpp.
    walletManager.setWalletDir(settingsManager.dataDirectory() + QStringLiteral("/wallets"));
    // FIX-G8 (2026-07-17 21:00): expose daemonClientObj so the
    // Dashboard.qml `Connections` block (line 491-528) can listen for
    // `infoUpdated` and `daemonHealthChanged`. Without this binding,
    // syncBlockHeight / syncProgress / syncState stay at defaults
    // (0 / 0 / "synced") and the DashboardSyncPill renders as
    // "Synced · 0 blocks" forever. Heartbeat on the DaemonClient is
    // started by StartupManager::stepVerifyVersion() once the daemon
    // version is verified. Companion change:
    // src/core/startup/StartupManager.cpp.
    engine.rootContext()->setContextProperty(
        QStringLiteral("daemonClientObj"), &daemonClient);

    // st-042: USD price feed. QML reads `priceProviderObj.priceUsd /
    // hasPrice / lastError` and calls `priceProviderObj.refresh()` on
    // page mount. Setting it before engine.load() guarantees the
    // binding is live the moment the QML scene instantiates.
    engine.rootContext()->setContextProperty(
        QStringLiteral("priceProviderObj"), &priceProvider);

    // st-053: transaction tracker for the Dashboard "recent
    // transactions" panel. QML reads
    // `transactionTrackerObj.recentTransfers` (a QVariantList slice
    // capped at 5 rows, see TransactionTracker::kRecentTransfersLimit)
    // and ListView-binds it as the model for RecentTransactions.qml.
    // The NOTIFY signal `recentTransfersChanged()` fires after every
    // successful refresh so the dashboard re-paints as new transfers
    // arrive. Setting it before engine.load() guarantees the binding
    // is live the moment the QML scene instantiates.
    engine.rootContext()->setContextProperty(
        QStringLiteral("transactionTrackerObj"), &transactionTracker);

    // st-162 (2026-07-18): BalanceTracker polls wallet-rpc's
    // get_accounts every 30s. QML reads `balanceTrackerObj.totalBalance`
    // and `balanceTrackerObj.unlockedBalance` so the Dashboard hero card
    // renders live Available / Locked balances.
    engine.rootContext()->setContextProperty(
        QStringLiteral("balanceTrackerObj"), &balanceTracker);

    // st-062: local-data bridge for HistoryPage / TxDetailDialog.
    // QML reads/writes per-tx note metadata via
    // `localDataManagerObj.saveTxNote(hash, note)` and friends. The
    // C++ facade lives at src/core/localdata/LocalDataManager.{h,cpp}
    // and Q_INVOKABLEs the underlying TxNote store (per V5.4.5 storage
    // policy - notes are UI-local JSON, not wallet-rpc state).
    engine.rootContext()->setContextProperty(
        QStringLiteral("localDataManagerObj"), &localDataManager);

    // st-110: SettingsManager context property. Wires the canonical
    // settingsManagerObj so the 8 settings sub-pages stop returning
    // null in their `typeof settingsManagerObj !== "undefined"`
    // guard and start real QSettings reads/writes.
    engine.rootContext()->setContextProperty(
        QStringLiteral("settingsManagerObj"), &settingsManager);

    // st-110: UriHandler context property. Wires the canonical
    // uriHandlerObj so SendPage "Paste URI" + ReceivePage "Generate
    // URI" + QrCodeImage.makeUri() resolve. Wallet-rpc wiring
    // (parseUri/makeUri) lands in st-111.
    engine.rootContext()->setContextProperty(
        QStringLiteral("uriHandlerObj"), &uriHandler);

    // st-110: SubaddressManager context property. Wires the
    // canonical subaddressManagerObj so SendPage "From" ComboBox +
    // Subaddresses.qml ListView resolve. Wallet-rpc wiring
    // (entries population) lands in st-111.
    engine.rootContext()->setContextProperty(
        QStringLiteral("subaddressManagerObj"), &subaddressManager);

    // st-111: RemoteNodeManager context property. Wires the
    // canonical remoteNodeObj so NetworkSettings.qml's
    // `typeof remoteNodeObj !== "undefined"` guard stops falling
    // through to null and the "Active Daemon Target" frame
    // resolves to remoteNode.activeLabel / activeSourceString.
    // Also enables AdvancedSettings.qml's "Remote node" diagnostic
    // row in the collapsed daemon-endpoint group.
    engine.rootContext()->setContextProperty(
        QStringLiteral("remoteNodeObj"), &remoteNode);

    // Expose I18nManager to QML for the emptyString trick:
    //   text: qsTr("Hello") + i18nManagerObj.emptyString
    engine.rootContext()->setContextProperty(
        QStringLiteral("i18nManagerObj"), &i18nManager);

    // Trigger retranslation after QML bindings are all set up.
    // emptyStringChanged emitted during binding evaluation (from
    // inside qtr()) is silently ignored by Qt 6.6.3's QML engine.
    // We defer to the next event-loop cycle so the engine can
    // invalidate + re-evaluate all bindings that depend on emptyString.
    QTimer::singleShot(100, &i18nManager, [&i18nManager]() {
        i18nManager.triggerRetranslate();
    });

    // st-112: RecoveryManager context property. Wires the canonical
    // recoveryManagerObj so Case3Recover.qml's
    // `typeof recoveryManagerObj !== "undefined"` guard stops falling
    // through to null and _submit() stops reaching the local-Timer
    // simulation. The QML submit handler calls
    // recoveryManagerObj.startRecoveryAsync({filename, password,
    // mnemonic}); the progressUpdated / recoveryComplete /
    // recoveryFailed signals bind the ProgressBar + status label
    // + walletReady emission.
    engine.rootContext()->setContextProperty(
        QStringLiteral("recoveryManagerObj"), &recoveryManager);

    // st-112: accountTagObj context property. Aliases the existing
    // localDataManagerObj so TxDetailDialog.qml can write the
    // st-112 spec contract (setTag / getTag / removeTag) directly
    // via `accountTagObj.*` without going through the older
    // localDataManagerObj.saveTxNote bridge. The two names point
    // at the same LocalDataManager instance — QML's context
    // property table stores per-name QVariant copies of the same
    // QObject*, and the QObject itself is shared (single source
    // of truth for tx_notes.json + tx_tags.json + address_book.json
    // + account_tags.json + subaddress_labels.json).
    engine.rootContext()->setContextProperty(
        QStringLiteral("accountTagObj"), &localDataManager);

    // st-033 + st-102: expose the validated initial page name to QML so
    // main.qml Component.onCompleted can StackView.push(...) the right page.
    // We set it before engine.load() so the binding is live the moment
    // the QML scene instantiates. The string is one of the 7 allowed
    // values (Dashboard / Receive / Send / History / Subaddress /
    // Onboarding / Settings; Receive added in st-102) - bad ones already
    // exited via showHelp(1) above.
    engine.rootContext()->setContextProperty(
        QStringLiteral("initialPage"), initialPage);

    // st-040: routing-aware marker. The actual StackView + Sidebar shell
    // lives in main.qml now (post-st-040 layout); here we just emit a
    // log line so a test or operator can prove that main.cpp wired the
    // initialPage context property AND that the binary is aware of the
    // st-040 routing contract (sidebar tabs are a strict subset of the
    // 7 kAllowedPages values (Dashboard / Receive / Send / History /
    // Subaddress / Onboarding / Settings; Receive added in st-102), so any
    // --page accepted by main.cpp is
    // routable from main.qml - either as a sidebar tab or as a special
    // case handled in Component.onCompleted).
    qInfo().noquote()
        << "st-040: main.cpp routing context ready; initialPage="
        << initialPage
        << "- main.qml StackView will honor it via Component.onCompleted";

    // st-054: Dashboard integration marker. The Dashboard.qml root
    // (post-st-054) re-emits sendClicked/receiveClicked/subaddressesClicked
    // and transactionClicked signals; main.qml wires them to
    // StackView.replace(<SendPage|ReceivePage|Subaddresses>) via
    // root._navigateTo(pageName). This log line proves main.cpp is
    // aware of the st-054 contract so a binary-spawn regression test
    // (test_main_routing st-040, test_main_cli_routing st-033) can
    // grep for the marker without re-loading the QML scene.
    qInfo().noquote()
        << "st-054: Dashboard integration context ready;"
        << "BalanceCard/ActionButtons/RecentTransactions compose the page;"
        << "sendClicked/receiveClicked/subaddressesClicked/transactionClicked"
        << "are routed via main.qml _navigateTo() helper";

    // st-062: HistoryPage / TxDetailDialog context marker. Both pages
    // bind to `transactionTrackerObj.transfers` (the full history
    // Q_PROPERTY, NOT the 5-row recentTransfers slice) and to
    // `localDataManagerObj.{save,get,remove}TxNote` for the per-tx
    // note round-trip. The QML side lives at qml/HistoryPage.qml and
    // qml/TxDetailDialog.qml; this marker proves main.cpp is aware of
    // the st-062 contract.
    qInfo().noquote()
        << "st-062: HistoryPage + TxDetailDialog context ready;"
        << "transactionTrackerObj.transfers + localDataManagerObj.{save,"
        << "get,remove}TxNote are wired via main.cpp context properties";

    // st-063: AddressBookPage + EditAddressDialog context marker. Both
    // pages bind to `localDataManagerObj` (st-017 + st-063 bridge):
    // addressBookEntries() / addressBookSize() / addAddressEntry() /
    // updateAddressEntry() / removeAddressEntry() / importAddressBook()
    // / exportAddressBook(). The QML side lives at
    // qml/AddressBookPage.qml and qml/EditAddressDialog.qml; this
    // marker proves main.cpp is aware of the st-063 contract so a
    // binary-spawn regression test (test_address_book st-063) can
    // grep for it without re-loading the QML scene.
    qInfo().noquote()
        << "st-063: AddressBookPage + EditAddressDialog context ready;"
        << "localDataManagerObj.{addressBookEntries,addAddressEntry,"
        << "updateAddressEntry,removeAddressEntry,importAddressBook,"
        << "exportAddressBook} are wired via main.cpp context properties";

    // st-110: P0 QML guard-break marker. The QML side has been
    // running with `typeof settingsManagerObj !== "undefined" ?
    // settingsManagerObj : null` (8 places) and `typeof uriHandlerObj
    // ...` (Send/Receive) and `typeof subaddressManagerObj ...`
    // (Send/Receive/Subaddresses) since st-080 / st-020 / st-019
    // respectively. Until st-110 these guards fell through to null
    // and the QML components were silently no-op'ing every write.
    // This log line lets test_main_wiring_st110 grep for it and
    // prove the wiring was emitted by the binary.
    qInfo().noquote()
        << "st-110: P0 context properties wired;"
        << "settingsManagerObj / uriHandlerObj / subaddressManagerObj"
        << "are live on the engine root context so the QML typeof"
        << "guards no longer fall through to null";

    // st-111: NetworkSettings / AdvancedSettings P0 guard-break marker.
    // NetworkSettings.qml and AdvancedSettings.qml have been running
    // with `typeof remoteNodeObj !== "undefined" ? remoteNodeObj :
    // null` (2 places) since st-023 / st-080 respectively. Until
    // st-111 those guards fell through to null and the user-facing
    // "Active Daemon Target" frame in NetworkSettings showed the
    // placeholder "(RemoteNodeManager not wired)" string, and
    // AdvancedSettings' "Remote node" diagnostic row showed the same
    // placeholder. After st-111 those bindings resolve to a real
    // RemoteNodeManager and surface the resolved host/port/source.
    // This marker lets test_main_wiring_st111 grep for it and prove
    // the runtime half of the contract is live.
    qInfo().noquote()
        << "st-111: remoteNodeObj wired; RemoteNodeManager is live on"
        << "the engine root context and startManager.setRemoteNodeManager()"
        << "is called so stepReadSettings() can layer the user's remote-node"
        << "decision over the local-daemon defaults";

    if (screenshotMode) {
        // st-097b: --screenshot <path> short-circuit. We bypass
        // StartupManager entirely (no daemon / wallet-rpc needed in
        // screenshot mode), load main.qml directly, grab the root
        // QQuickWindow to a QImage, and save it as PNG.
        engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
        if (engine.rootObjects().isEmpty()) {
            qCritical()
                << "st-097b: failed to load qrc:/qml/main.qml (screenshot mode)";
            return 2;
        }
        // st-097c1: keep the root QObject pointer around so we can flip
        // main.qml"s walletReady flag below (it"s a property of the
        // ApplicationWindow root, not of the QQuickWindow subclass).
        QObject *rootObject = engine.rootObjects().first();
        QQuickWindow *win = qobject_cast<QQuickWindow *>(rootObject);
        if (win == nullptr) {
            qCritical()
                << "st-097b: root QObject is not a QQuickWindow; got"
                << rootObject->metaObject()->className();
            return 3;
        }

        // st-097c1: defer grabWindow() until the StackView replaces its
        // initialItem. Without this pump, grabWindow() captures the
        // OnboardingWizard (mounted while walletReady=false) and --page
        // has no visible effect: 16 captures collapse to one MD5
        // (accb7e2e91ec9e1e9148b17a6dcfe149, see
        // devplan/st-097c_real_test_proof.txt). Two things have to happen
        // before the scene-graph first frame reflects the --page choice:
        //
        //   (a) main.qml"s Component.onCompleted already sets
        //       currentPageName based on cliInitialPage -- this fires
        //       synchronously during the initial engine.load() above.
        //       BUT the StackView itself is gated on RowLayout.visible,
        //       which is bound to root.walletReady. The wizard owns the
        //       window while walletReady=false, so the StackView never
        //       mounts and grabWindow() always renders the wizard.
        //   (b) We must pump the event loop until the scene graph emits
        //       QQuickWindow::frameSwapped, otherwise grabWindow()
        //       returns an empty QImage (the offscreen backend has not
        //       rasterized a frame yet). We add a 1000 ms safety timer
        //       so a stuck render thread can"t hang the binary.
        //
        // For --page Onboarding we deliberately keep walletReady=false so
        // the wizard stays mounted (matches the st-040 contract; the
        // wizard IS the page for --page Onboarding).
        if (initialPage != QStringLiteral("Onboarding")) {
            rootObject->setProperty("walletReady", true);
        }
        {
            QEventLoop readyPump;
            QTimer readySafety;
            readySafety.setSingleShot(true);
            readySafety.setInterval(1000);
            QObject::connect(&readySafety, &QTimer::timeout,
                             &readyPump, &QEventLoop::quit);
            QObject::connect(win, &QQuickWindow::frameSwapped,
                             &readyPump, &QEventLoop::quit);
            readySafety.start();
            readyPump.exec();
        }
        qInfo().noquote()
            << "st-097c1: post-load frame wait done; initialPage="
            << initialPage;

        // Primary path: synchronous QQuickWindow::grabWindow(). It
        // returns an empty QImage if the scene graph hasn't yet
        // rasterized a frame; in offscreen mode the first frame lands
        // after the first event-loop tick, so we pump up to 5 short
        // cycles before giving up. 50 ms each = 250 ms worst case,
        // well under the real-test's overall deadline.
        QImage grabbed;
        for (int attempt = 0; attempt < 5 && grabbed.isNull(); ++attempt) {
            grabbed = win->grabWindow();
            if (grabbed.isNull()) {
                QEventLoop pump;
                QTimer::singleShot(50, &pump, &QEventLoop::quit);
                pump.exec();
            }
        }

        if (grabbed.isNull()) {
            // Fallback: documented async path via QQuickItem::grabToImage
            // on the content item. This is the API Qt 6 Quick
            // recommends for headless capture and works in software
            // scene graph backends (offscreen QPA) where the
            // synchronous QQuickWindow::grabWindow() can race.
            qInfo()
                << "st-097b: QQuickWindow::grabWindow() returned empty;"
                << "falling back to contentItem->grabToImage";
            auto grabResult = win->contentItem()->grabToImage();
            if (grabResult.isNull()) {
                qCritical()
                    << "st-097b: contentItem->grabToImage() returned null";
                return 4;
            }
            QEventLoop waitLoop;
            QObject::connect(grabResult.data(),
                             &QQuickItemGrabResult::ready,
                             &waitLoop,
                             &QEventLoop::quit);
            // 10-second safety timeout so a stuck render thread cannot
            // hang the binary forever.
            QTimer::singleShot(10 * 1000, &waitLoop, &QEventLoop::quit);
            waitLoop.exec();
            // QQuickItemGrabResult::ready() returns void in Qt 6 (it is
            // the SIGNAL name; the "ready" state is observable through
            // image() being non-null after the signal has fired). If the
            // 10s safety timer fired first, image() is still null and
            // we treat that as a timeout.
            if (grabResult->image().isNull()) {
                qCritical()
                    << "st-097b: grabToImage timed out before image was"
                    << "ready (10s safety timeout elapsed)";
                return 5;
            }
            grabbed = grabResult->image();
        }

        if (grabbed.isNull()) {
            qCritical() << "st-097b: grab returned a null QImage";
            return 6;
        }

        if (!grabbed.save(screenshotPath, "PNG")) {
            qCritical().noquote()
                << "st-097b: failed to save PNG to" << screenshotPath;
            return 7;
        }

        qInfo().noquote()
            << "st-097b: screenshot saved to" << screenshotPath
            << "size=" << grabbed.size()
            << "bytes=" << QFileInfo(screenshotPath).size();
        // Clean exit so the real-test's `&&` chain succeeds; we do
        // NOT run app.exec() in screenshot mode because the QML
        // scene is fully rendered already and we have nothing to
        // pump.
        return 0;
    }

    // st-131 (2026-07-16 21:31): load main.qml on
    // stateChanged(LoadingSettings) instead of startupComplete.
    // Background: stepVerifyVersion (StartupManager.cpp:560+) makes a
    // synchronous HTTP RPC (daemon.get_info) in a 30-attempt retry
    // loop; in dev-env (no daemon bound on the picked port) this
    // hangs the entire app for 30-90 s before emitting startupFailed.
    // With engine.load gated on startupComplete the user sees a black
    // window for that whole window. With engine.load gated on
    // stateChanged(LoadingSettings) the QML scene renders within one
    // event-loop tick of startup.start(); the OnboardingWizard (or
    // sidebar shell) is visible while steps 2-5 still run, and
    // startupFailed surfaces via Toast.qml's existing onStartupFailed
    // hook (fd515d9) instead of via exit(1).
    //
    // The static `mainLoaded` flag prevents re-load on the next
    // stateChanged. After LoadingSettings we move to StartingDaemon
    // (stateChanged fires again with state=StartingDaemon), so the
    // state == LoadingSettings check already gates this correctly;
    // the flag is belt-and-braces against a future reset() call that
    // re-enters LoadingSettings.
    QObject::connect(&startup, &StartupManager::stateChanged,
                     &engine, [&engine](StartupManager::State state) {
        static bool mainLoaded = false;
        if (state == StartupManager::State::LoadingSettings
            && !mainLoaded) {
            engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
            if (engine.rootObjects().isEmpty()) {
                qCritical() << "Failed to load qrc:/qml/main.qml";
                QCoreApplication::exit(1);
            }
            mainLoaded = true;
        }
    });

    // st-131: startupFailed no longer exit(1). Toast.qml's
    // onStartupFailed hook (fd515d9) shows a top-center modal Toast
    // with the failed-step category mapped to a localised label; the
    // user can dismiss / act on it. Exiting here used to kill the
    // process BEFORE the QML scene was loaded (because engine.load
    // was gated on startupComplete), which left the user staring at
    // a terminal window with no UI to react to. Now the UI is up by
    // the time startupFailed can fire, so staying alive is the right
    // move.
    QObject::connect(&startup, &StartupManager::startupFailed,
                     &app, [](const QString& reason, int step) {
        qCritical().noquote()
            << "Startup failed at step" << step << ":" << reason
            << "(continuing; Toast will surface the error to the user)";
    });

    if (!startup.start()) {
        qWarning() << "StartupManager::start() refused (already running?)";
    }

    // st-133b (2026-07-17 01:42): aboutToQuit cleanup slot.
    //
    // Background: st-132 spec shipped, but manual verification at 23:07
    // (Bug Report: "close UI and reopen -> Startup failed step 4 : DaemonClient
    // ::fetchInfo failed after 30 attempts: unknown exception") showed
    // that closing the AppImage UI leaves two residuals that block the
    // next launch:
    //   - the detached judecoind process (started by AppImage/AppRun via
    //     QProcess::startDetached) keeps running because ProcessManager
    //     ::stopAll() explicitly does NOT kill it (it is designed to
    //     survive the UI for st-133g adoption logic)
    //   - the AppImage FUSE mount at /tmp/.mount_Judeco*/ stays
    //     mounted because no umount/fusermount was issued
    //   - ~/.judecoin/judecoind.pid stays on disk so the next launch's
    //     adoption logic sees a "ghost" PID and races against a stale
    //     daemon
    //
    // We bind to QGuiApplication::aboutToQuit (NOT lastWindowClosed)
    // because aboutToQuit fires AFTER the QGuiApplication has decided
    // to exit and BEFORE the engine is torn down, which is exactly
    // the window where we still have working ProcessManager / QDir /
    // QProcess handles. lastWindowClosed fires earlier (when the
    // last QQuickWindow is closed) but the engine is still alive then
    // - we want the LATER signal so the cleanup runs after the user
    // has committed to exit.
    //
    // Cleanup sequence:
    //   1. processManager.stopAll()        - SIGTERM wallet-rpc (3s)
    //   2. processManager.killDetachedDaemon(5000) - SIGTERM the
    //      judecoind we spawned via startDetached, SIGKILL after 5s
    //   3. fusermount -u /tmp/.mount_Judeco* (skip on error - FUSE
    //      mount may already be gone if AppImage re-launched)
    //   4. QFile::remove ~/.judecoin/judecoind.pid
    //
    // The lambda captures &processManager by reference; the slot is
    // bound to the QGuiApplication instance which lives to end of
    // main(), so the reference is valid for the entire app lifetime.
    // We do NOT capture &startup because reset() is called in the
    // explicit "Cleanup:" block below this slot, after app.exec()
    // returns; the slot itself runs DURING exec()'s teardown.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&processManager]() {
        qInfo().noquote()
            << "st-133b: aboutToQuit fired; running shutdown cleanup";

        // (1) wallet-rpc graceful shutdown (SIGTERM 3s -> SIGKILL).
        // stopAll() is the existing helper which already handles the
        // QProcess child lifecycle.
        processManager.stopAll();

        // (2) detached judecoind graceful shutdown (SIGTERM 5s ->
        // SIGKILL). ProcessManager.killDetachedDaemon is the new
        // st-133b method; before this, stopAll() deliberately left
        // the daemon alive which is exactly the bug we are fixing.
        processManager.killDetachedDaemon(std::chrono::milliseconds(5000));

        // (3) AppImage FUSE mount cleanup. AppRun mounts the AppImage
        // squashfs at /tmp/.mount_Judeco<random>/ via FUSE; on a
        // clean exit we want fusermount -u to release it so subsequent
        // AppImage invocations can re-mount without EBUSY. We list
        // /tmp/.mount_Judeco* (Dirs | Hidden | System because FUSE
        // mountpoints are dot-prefixed and may carry the system
        // attribute), then run fusermount -u on each. Errors are
        // deliberately swallowed (QProcess::execute returns -1 on
        // failure; if fusermount is missing or the mount is already
        // gone the cleanup is best-effort).
        {
            QDir mountDir(QStringLiteral("/tmp"));
            const auto entries = mountDir.entryList(
                QStringList{QStringLiteral(".mount_Judeco*")},
                QDir::Dirs | QDir::Hidden | QDir::System);
            for (const auto& e : entries) {
                const QString mountPath =
                    QStringLiteral("/tmp/") + e;
                const int rc = QProcess::execute(
                    QStringLiteral("umount"),
                    {QStringLiteral("-l"), mountPath});
                qInfo().noquote()
                    << "st-133b: umount -l" << mountPath
                    << "rc=" << rc;
            }
        }

        // (4) Stale PID file cleanup. The detached judecoind writes
        // its PID here on startup (see st-133h AppRun HOME patch);
        // now that the daemon is killed, the file is misleading.
        const QString pidPath = QDir::homePath()
            + QStringLiteral("/.judecoin/judecoind.pid");
        if (QFile::exists(pidPath)) {
            if (QFile::remove(pidPath)) {
                qInfo().noquote()
                    << "st-133b: removed stale PID file" << pidPath;
            } else {
                qWarning().noquote()
                    << "st-133b: failed to remove PID file" << pidPath
                    << "-" << QFile(pidPath).errorString();
            }
        }
    });
    qInfo().noquote()
        << "st-133b: aboutToQuit cleanup slot registered (stopAll +"
        << "killDetachedDaemon + fusermount + PID removal)";

    // st-117 (2026-07-16 16:08): the late setRemoteNodeManager(&remoteNode)
    // call that used to live here was redundant - and racy. It was called
    // AFTER start() ran (so stepReadSettings() had already executed with
    // remoteNode_ == nullptr), which is exactly the "wiring order doesn't
    // matter" reasoning that produced the missing-deps bug in the first
    // place. All 5 setters now run before start() (see st-117 block
    // after RemoteNodeManager declaration); this comment block
    // intentionally contains no follow-up setter calls so the wiring
    // contract has exactly one home.

    // FIX-G3 (2026-07-17 17:24): wrap app.exec() in try/catch so
    // that any std::exception escaping the event loop (e.g. via a
    // signal handler that throws, or a deferred invocation that wasn't
    // guarded) surfaces as a UI dialog instead of std::terminate + core
    // dump. Pairs with the std::set_terminate hook above; this catch
    // handles exceptions caught BEFORE std::terminate is reached.
    int rc;
    try {
        rc = app.exec();
    } catch (const std::exception& e) {
        qCritical().noquote()
            << "FIX-G3: caught std::exception at main():" << e.what();
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Judecoin Wallet - Error"),
            QStringLiteral("Application terminated abnormally:\n\n%1").arg(QString::fromUtf8(e.what())));
        rc = 1;
    } catch (...) {
        qCritical().noquote()
            << "FIX-G3: caught unknown exception at main()";
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Judecoin Wallet - Error"),
            QStringLiteral("Application terminated abnormally (unknown exception). Please screenshot and report."));
        rc = 1;
    }

    // Cleanup: drop the manager back to Idle so a future start() call
    // (e.g. after a config change) is allowed. Pending QTimer hops are
    // cancelled inside reset().
    startup.reset();
    return rc;
}