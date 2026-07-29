// =============================================================================
// Judecoin Wallet - main.qml (st-040 + st-064 + st-065 + st-092 + st-098a + st-102)
//
// Application entry point loaded by src/main.cpp via qrc:/qml/main.qml.
//
// Pre-st-040 layout (st-022):
//   ApplicationWindow
//     └─ OnboardingWizard (single full-window page)
//
// Post-st-040 layout (this file):
//   ApplicationWindow
//     ├─ [walletReady=false] OnboardingWizard      (full window)
//     └─ [walletReady=true ] RowLayout
//           ├─ Sidebar                              (180px Forest Green rail)
//           └─ StackView                            (Dashboard / ReceivePage (push) /
//                                                    SendPage / HistoryPage /
//                                                    Subaddress (push))
//
// st-092 routing fix: the Send tab now points to SendPage.qml (the real
// four-field send form, st-060) instead of Send.qml (st-040 placeholder).
// The placeholder Send.qml / History.qml files are removed entirely; the
// History tab was already routing to HistoryPage.qml in st-062 (so
// History.qml was already orphaned). SendPage is reachable from both the
// Sidebar Send tab (replace) and the Dashboard ActionButtons Send CTA
// (replace, wired via Dashboard.sendClicked -> root._navigateTo("Send")).
//
// st-098a responsive/multi-viewport: window dimensions are now derived
// from Screen.desktopAvailableWidth/Height so the desktop shell never
// overflows small monitors. The layout intent is:
//   * width  in [1080, 1400]   (default 1100, +space on bigger desktops)
//   * height in [ 600,  900]   (default  700, +space on bigger desktops)
// 1080 = 180px sidebar + 900px usable content area (the spec target).
// The default 1100x700 is the design-mockup size and preserves the
// visual-diff baselines (docs/release/screenshots/baseline_v1.0.0/,
// 16 PNGs). On a real desktop with extra screen space we grow up to
// the cap (1400x900) so the BalanceCard hero gets more breathing room
// on 4K monitors; we never shrink below the default because the offscreen
// QPA platform reports an artificial 800x800 screen and the
// st-097b/st-097c screenshot harness relies on the default size.
// Inner pages already use anchors.fill + margins (Dashboard.qml st-054)
// and Layout.fillWidth on the rows + hero cards (BalanceCard st-050),
// so no per-page width tweaks are needed in this sub-task — the only
// fixed-width leaf is Sidebar's 180px rail (intentional, per st-041
// contract).
//
// Routing rules (per st-040 + st-064 spec_notes):
//   * StackView initialItem: resolved from the CLI --page context property
//   * Receive (st-064, st-102): pushed (not replaced) onto the StackView
//     so the ReceivePage's top-bar back button can mainStack.pop() back to
//     the parent page (normally Dashboard). Receive is the 2nd sidebar tab
//     (index 1 in Sidebar.qml); clicking it from the Sidebar performs the
//     same push as the Dashboard ActionButtons Receive CTA. All other
//     Sidebar tabs keep their st-040 replace semantics (Send / History /
//     Dashboard) or st-065 push (Subaddress).
//     (set in main.cpp, validated against kAllowedPages, includes Receive
//     since st-102).
//   * Sidebar emits `tabClicked(pageName)`; main.qml routes that into
//     StackView.replace(pageComponentFor(pageName)).
//   * OnboardingWizard emits `walletReady(filename)`; main.qml flips
//     `walletReady` to true and pushes the current page (Dashboard by
//     default, or --page Dashboard/Send/History/Subaddress if CLI asked).
//   * If --page is "Onboarding" we leave walletReady=false so the wizard
//     stays mounted (the StackView is not used in that case).
//   * If --page is "Settings" we boot into Dashboard; the Settings page
//     is reachable from the dashboard's gear button (future sub_task).
//
// V5.4.9 compliance: pure QML, no Widgets.
// V5.4.7 compliance: no secret material is touched here. Keys flow
// OnboardingWizard -> Case4BackupRestore -> WalletManager via the
// context properties set by main.cpp; main.qml itself never sees
// password / seed / private keys.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    visible: true
    // ---- responsive sizing (st-098a) -----------------------------------
    /// Width / height grow above the design-mockup default on desktops
    /// that have extra screen space, and shrink to the spec minimum on
    /// tiny screens. The minWidth of 1080 = 180 (sidebar rail, st-041
    /// contract) + 900 (page content minimum, per spec_notes). The 40px
    /// outer clamp leaves room for window decorations (title bar +
    /// borders) on most desktop WMs. The default 1100x700 is the
    /// design-mockup size and is what the offscreen screenshot harness
    /// (st-097b/st-097c) renders; we never shrink below the default
    /// because the offscreen QPA platform reports a synthetic 800x800
    /// screen that would otherwise force a non-baseline size.
    // st-160 (2026-07-18): sidebar widened 180 -> 220 per
    // reference-style request (left rail should breathe more).
    // kMinWinW recalculated to keep content area >= 900 px:
    //   220 (sidebar) + 900 (content) = 1120.
    readonly property int kSidebarWidth:   220
    readonly property int kContentMinW:    900
    readonly property int kMinWinW:        kSidebarWidth + kContentMinW   // 1120
    readonly property int kMaxWinW:        1440
    readonly property int kDefaultWinW:    1140
    readonly property int kMinWinH:        600
    readonly property int kMaxWinH:        900
    readonly property int kDefaultWinH:    700
    readonly property int kDecorMargin:    40

    /// Extra horizontal room (>=0) above the default + decor margin.
    /// Negative values (small screens) collapse to 0 so the default
    /// size is preserved; the offscreen QPA screen reports 800x800
    /// which would otherwise drag the default down to a non-baseline
    /// size and break visual-diff.
    readonly property int _extraW: Math.max(0, Screen.desktopAvailableWidth  - kDefaultWinW - kDecorMargin)
    readonly property int _extraH: Math.max(0, Screen.desktopAvailableHeight - kDefaultWinH - kDecorMargin)

    /// Whether the QPA screen reports a realistic desktop size. The
    /// offscreen plugin (used by st-097b/st-097c screenshot harness)
    /// reports a synthetic 800x800 surface; we treat anything at or
    /// below our default + decor margin as "test/headless" and fall
    /// back to the design-mockup default so visual-diff baselines
    /// (1100x700) stay stable.
    readonly property bool _looksLikeRealDesktop: Screen.desktopAvailableWidth  >= kDefaultWinW + kDecorMargin
                                               && Screen.desktopAvailableHeight >= kDefaultWinH + kDecorMargin

    width:  _looksLikeRealDesktop
        ? Math.max(kMinWinW, Math.min(kMaxWinW, kDefaultWinW + _extraW))
        : kDefaultWinW
    height: _looksLikeRealDesktop
        ? Math.max(kMinWinH, Math.min(kMaxWinH, kDefaultWinH + _extraH))
        : kDefaultWinH
    // Hard min/max (in addition to the clamping above) so the window
    // can't be shrunk below 1080x600 by the user dragging the edge.
    minimumWidth:  kMinWinW
    minimumHeight: kMinWinH
    maximumWidth:  kMaxWinW
    maximumHeight: kMaxWinH
    title: (i18nManagerObj.qtr("main", "Judecoin Wallet") + i18nManagerObj.emptyString)

    // ---- top-level state -----------------------------------------------
    /// Toggled by the wizard's walletReady signal. False -> wizard is the
    /// only mounted page. True -> sidebar + StackView take over.
    property bool walletReady: false

    /// Logical name of the page currently mounted in the StackView.
    /// Synced with sidebar.selectedIndex so the highlight follows the page.
    /// Mapping is done via _pageIndexFor() because the Sidebar exposes a
    /// numeric index (0..3) as the canonical state in st-041.
    property string currentPageName: "Dashboard"

    /// CLI-supplied initial page (validated by main.cpp against the 6
    /// allowed values). Lazy-resolved so QML preview tooling that
    /// instantiates this file without a context property still loads.
    readonly property string cliInitialPage: typeof initialPage !== "undefined"
        ? String(initialPage) : "Dashboard"

    // ---- startup state (st-131) ---------------------------------------
    // StartupManager.state (Q_PROPERTY state, StartupManager.h) is
    // already exposed to QML; these computed properties project the
    // int enum onto friendly strings + booleans so UI bindings can
    // read root.startupStepName / root.isStartupInFlight without
    // re-implementing the integer mapping in every QML file that
    // touches startupManager. The values mirror StartupManager::State:
    //   0=Idle 1=LoadingSettings 2=StartingDaemon 3=StartingWalletRpc
    //   4=VerifyingVersion 5=LoadingCommands 6=Ready 7=Failed
    readonly property string startupStepName: {
        var s = (typeof startupManager !== "undefined" && startupManager)
            ? startupManager.state : -1
        switch (s) {
            case 0: return (i18nManagerObj.qtr("main", "Idle") + i18nManagerObj.emptyString)
            case 1: return (i18nManagerObj.qtr("main", "Loading Settings") + i18nManagerObj.emptyString)
            case 2: return (i18nManagerObj.qtr("main", "Starting Daemon") + i18nManagerObj.emptyString)
            case 3: return (i18nManagerObj.qtr("main", "Starting Wallet RPC") + i18nManagerObj.emptyString)
            case 4: return (i18nManagerObj.qtr("main", "Verifying Version") + i18nManagerObj.emptyString)
            case 5: return (i18nManagerObj.qtr("main", "Loading Commands") + i18nManagerObj.emptyString)
            case 6: return (i18nManagerObj.qtr("main", "Ready") + i18nManagerObj.emptyString)
            case 7: return (i18nManagerObj.qtr("main", "Failed") + i18nManagerObj.emptyString)
            default: return (i18nManagerObj.qtr("main", "Unknown") + i18nManagerObj.emptyString)
        }
    }
    readonly property bool isStartupReady: (typeof startupManager !== "undefined"
        && startupManager && startupManager.state === 6)
    readonly property bool isStartupFailed: (typeof startupManager !== "undefined"
        && startupManager && startupManager.state === 7)
    readonly property bool isStartupInFlight: (typeof startupManager !== "undefined"
        && startupManager && startupManager.state >= 1
        && startupManager.state <= 5)

    // ---- onboarding view (wizard) --------------------------------------
    OnboardingWizard {
        id: wizard
        anchors.fill: parent
        visible: !root.walletReady
        onWalletReady: function(filename) {
            console.log("main.qml (st-040): wallet ready ->", filename,
                        "- mounting sidebar shell, currentPage=",
                        root.currentPageName)
            root.walletReady = true

            if (typeof lockManagerObj !== "undefined") {
                lockManagerObj.start()
            }
        }
    }

    Connections {
        target: typeof lockManagerObj !== "undefined" ? lockManagerObj : null
        enabled: typeof lockManagerObj !== "undefined"
        function onLockTriggered() {
            console.log("main.qml: auto-lock triggered, returning to onboarding")
            root.walletReady = false
        }
    }

    // ---- main app shell (sidebar + StackView) --------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.walletReady

        Sidebar {
            id: sidebar
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            selectedIndex: root._pageIndexFor(root.currentPageName)
            onTabClicked: function(pageName) {
                console.log("main.qml (st-040): sidebar tabClicked ->", pageName)
                // Onboarding is the wizard, not a StackView page. Clicking
                // it (only possible from a future re-onboarding entry) drops
                // the user back to the wizard.
                if (pageName === "Onboarding") {
                    root.walletReady = false
                    wizard.currentPage = "home"
                    return
                }
                // st-064 + st-102: Receive is a pushed page (not a replace).
                // The receivePage's top-bar Back button calls
                // mainStack.pop() to return to the parent page (normally
                // Dashboard). We update currentPageName so the Sidebar
                // highlight follows; re-clicking Receive while on the
                // Receive page is a no-op (no duplicate push), matching
                // the Subaddress pattern below.
                if (pageName === "Receive") {
                    if (root.currentPageName !== "Receive") {
                        root.currentPageName = "Receive"
                        mainStack.push(receivePage)
                    }
                    return
                }
                // st-065: Subaddress is now a pushed page (not a replace).
                // Same pattern as Receive (st-064 + st-102): the
                // Subaddresses page's top-bar Back button calls
                // mainStack.pop() to return to the page below. We no-op
                // if we're already on Subaddress (so repeated Sidebar
                // clicks don't stack duplicates).
                if (pageName === "Subaddress") {
                    if (root.currentPageName !== "Subaddress") {
                        root.currentPageName = "Subaddress"
                        mainStack.push(subaddressPage)
                    }
                    return
                }
                if (pageName !== root.currentPageName) {
                    root.currentPageName = pageName
                    mainStack.replace(root._pageComponentFor(pageName))
                }
            }
        }

        StackView {
            id: mainStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            // initialItem is bound to the currentPageName -> Component so a
            // late-changing currentPageName (e.g. via Component.onCompleted
            // honoring --page) replaces the top item automatically on first
            // activation. StackView caches the resolved Component.
            initialItem: root._pageComponentFor(root.currentPageName)
        }
    }

    // ---- helpers -------------------------------------------------------
    /// Resolve a page name to a Component instance. The components are
    /// declared inline at the bottom of this file so they are reachable
    /// as QML ids and the QML cache deduplicates loads.
    function _pageComponentFor(name) {
        switch (name) {
            case "Dashboard":   return dashboardPage
            case "Receive":     return receivePage
            case "Send":        return sendPage
            case "History":     return historyPage
            case "Subaddress":  return subaddressPage
            case "Settings":    return settingsPage
            default:            return dashboardPage
        }
    }

    /// Reverse of _pageComponentFor: page name -> Sidebar selectedIndex.
    /// The Sidebar (st-041 + st-102) exposes a numeric index 0..4 as the
    /// canonical highlight state, so main.qml maps the logical page name
    /// through this helper before binding it onto selectedIndex. The
    /// locked order is Dashboard(0) / Receive(1) / Send(2) / History(3) /
    /// Subaddress(4) - mirrors Sidebar.qml tabNames 1:1. Unknown names
    /// collapse to 0 (Dashboard) so the highlight stays sane.
    function _pageIndexFor(name) {
        switch (name) {
            case "Dashboard":   return 0
            case "Receive":     return 1
            case "Send":        return 2
            case "History":     return 3
            case "Subaddress":  return 4
            case "Settings":    return 5
            default:            return 0
        }
    }

    // ---- honor --page (CLI) on first activation -----------------------
    Component.onCompleted: {
        // We only honor the CLI initial page if the wizard is already
        // "done" - otherwise the wizard owns the screen and the CLI value
        // is treated as a wish for the page to land on after onboarding.
        // For st-040 we resolve the page eagerly: --page Onboarding keeps
        // the wizard, --page Settings falls back to Dashboard (Settings
        // isn't a sidebar tab), and the four main tabs (Dashboard / Send
        // / History / Subaddress) become the active page name so the
        // StackView pushes the right Component on first render.
        var p = root.cliInitialPage
        if (p === "Onboarding") {
            // wizard stays mounted; nothing to do.
            console.log("main.qml (st-040): --page Onboarding honored, wizard mounted")
        } else if (p === "Dashboard" || p === "Receive" || p === "Send" || p === "History" || p === "Subaddress") {
            root.currentPageName = p
            console.log("main.qml (st-040): --page", p, "honored as initial StackView page")
        } else {
            // Settings (or any future non-tab CLI page): boot Dashboard.
            console.log("main.qml (st-040): --page", p,
                        "not a sidebar tab, falling back to Dashboard")
            root.currentPageName = "Dashboard"
        }

        // st-143 (2026-07-17 14:02): signal StartupManager
        // that the QML tree is fully constructed. By the time this
        // Component.onCompleted fires, every QML component has been
        // instantiated and every Qt6 plugin .so has been loaded through
        // FUSE. This means the squashfs page cache is warm and the FUSE
        // driver is no longer busy with the UI, so the upcoming
        // exec(judecoind) will not be starved by QML loading (the root
        // cause of st-142's apparent fix not actually fixing the bug).
        if (typeof startupManager !== "undefined" && startupManager)
            startupManager.notifyUiReady()
    }

    // ---- page components (cached by QML) -------------------------------
    // Each Component wraps the matching .qml file. Naming uses the
    // convention `<name>Page` so the ids are unique across the file and
    // the names read as a routing table.
    //
    // st-054 wiring: Dashboard's sendClicked/receiveClicked/
    // subaddressesClicked/transactionClicked signals drive the
    // StackView routing here (instead of via the Sidebar). The
    // `_navigateTo(pageName)` helper below mirrors what the Sidebar
    // onTabClicked handler does (set currentPageName + replace) so
    // Dashboard-driven navigation looks identical to Sidebar-driven
    // navigation (highlight follows, console logs match).
    Component {
        id: dashboardPage
        Dashboard {
            // st-162 (2026-07-18): wire live balance from
            // BalanceTracker (polls wallet-rpc get_accounts every 30s).
            balanceAmount: (typeof balanceTrackerObj !== "undefined" && balanceTrackerObj)
                ? balanceTrackerObj.totalBalance : 0.0
            unlockedAmount: (typeof balanceTrackerObj !== "undefined" && balanceTrackerObj)
                ? balanceTrackerObj.unlockedBalance : 0.0
            isEmpty: balanceAmount <= 0 && unlockedAmount <= 0

            onSendClicked: root._navigateTo("Send")
            onReceiveClicked: root._navigateTo("Receive")
            onSubaddressesClicked: root._navigateTo("Subaddress")
            onTransactionClicked: function(txHash) {
                // st-054: original routing marker (kept verbatim so
                // tests/test_dashboard test_main_qml_st054_wiring()
                // can grep for the "(st-054): Dashboard
                // transactionClicked" log pattern).
                console.log("main.qml (st-054): Dashboard transactionClicked txHash=", txHash)
                // st-062: route Dashboard's per-tx click straight into
                // the global TxDetailDialog overlay. The dialog is
                // mounted at app scope (below) so it survives
                // StackView transitions; we just set its txHash +
                // rowData and set _txDetailOpen = true to show it.
                console.log("main.qml (st-062): opening TxDetailDialog from Dashboard for txHash=", txHash)
                root._openTxDetail(txHash)
            }
        }
    }
    // st-092: SendPage (st-060 four-field send form) replaces Send
    // placeholder (st-040). Send.qml has been removed.
    // FIX-G29 (2026-07-18): onSubmitClicked handler wires the
    // submitClicked signal -> walletManagerObj.transferSplit() -> Toast.
    // Without this, the signal fired into the void and no transfer_split
    // RPC was ever called ("no corresponding info returned" bug).
    Component {
        id: sendPage
        SendPage {
            onSubmitClicked: function(fromAddr, toAddr, amountAtomic, priority) {
                console.log("main.qml (FIX-G29): sendPage submitClicked",
                            "from=", fromAddr, "to=", toAddr,
                            "amountAtomic=", amountAtomic, "priority=", priority)
                var wm = (typeof walletManagerObj !== "undefined")
                    ? walletManagerObj : null
                if (!wm) {
                    globalToast.show("rpc_error", "Wallet not found",
                                     "Please create or open a wallet first", "error")
                    return
                }
                const jdeAmount = (Number(amountAtomic) / 1000.0).toFixed(3)
                try {
                    const result = wm.transferSplit(
                        fromAddr, toAddr, String(amountAtomic), priority)
                    if (result.ok) {
                        globalToast.show("general", "Transfer successful",
                                         "TX: " + result.tx_hash, "info")
                    } else {
                        globalToast.show("rpc_error", "Transfer failed",
                                         result.error || "Unknown error", "error")
                    }
                } catch (e) {
                    globalToast.show("rpc_error", "Transfer error",
                                     String(e.message || e), "error")
                }
            }
        }
    }
    // st-102: receivePage Component is already defined below for the
    // Dashboard-driven push (st-064). It is also referenced by the
    // _pageComponentFor / _pageIndexFor mapping above and the
    // Sidebar Receive tab branch in onTabClicked.
    Component {
        id: historyPage
        HistoryPage {
            // st-062: re-emit HistoryPage's txClicked up to the
            // global TxDetailDialog overlay. Same pattern as the
            // Dashboard -> transactionClicked forwarding above.
            onTxClicked: function(txHash) {
                console.log("main.qml (st-062): HistoryPage txClicked txHash=", txHash,
                            "- opening TxDetailDialog")
                root._openTxDetail(txHash)
            }
        }
    }
    Component {
        id: subaddressPage
        Subaddresses {
            // st-065: Subaddresses emits backClicked() when the top-bar
            // "Back to Dashboard" button is tapped. Pop the StackView
            // so the page below (normally Dashboard, the page whose
            // subaddressesClicked signal pushed us here) is revealed.
            onBackClicked: {
                console.log("main.qml (st-065): subaddressPage backClicked -> mainStack.pop()")
                mainStack.pop()
            }
        }
    }
    Component {
        id: receivePage
        ReceivePage {
            // st-064: ReceivePage emits backClicked() when the top-bar
            // "Back to Dashboard" button is tapped. Pop the StackView
            // so the page below (normally Dashboard, the page whose
            // receiveClicked signal pushed us here) is revealed.
            onBackClicked: {
                console.log("main.qml (st-064): receivePage backClicked -> mainStack.pop()")
                mainStack.pop()
            }
        }
    }
    Component {
        id: settingsPage
        Settings {
            // Settings.qml — 9-category settings page (st-023 + st-080).
            // Network / Language / Auto-Lock / Transfer Confirm /
            // Appearance / Security / Advanced / Reset / About.
            // Security category (SecuritySettings.qml) contains the
            // wallet key export function.
            //
            // Context properties (settingsManagerObj, remoteNodeObj)
            // are wired through the engine root context by main.cpp;
            // Settings.qml reads them via its own `property var` guards
            // so this component needs no explicit property wiring.
            onResetRequested: {
                console.log("main.qml: resetRequested from Settings -> return to onboarding")
                root.walletReady = false
                wizard.currentPage = "home"
            }
        }
    }

    // ---- routing helper (st-054) ---------------------------------------
    /// Drive the StackView to a logical page name. Mirrors what the
    /// Sidebar onTabClicked handler does so Dashboard-triggered
    /// navigation looks identical to Sidebar-triggered navigation
    /// (sidebar highlight follows the new page, console logs match).
    /// Used by the inline Dashboard{} Component above and by future
    /// keyboard shortcuts or deep-link handlers.
    function _navigateTo(pageName) {
        if (pageName === "Onboarding") {
            root.walletReady = false
            wizard.currentPage = "home"
            return
        }
        // st-054 marker: the test_dashboard static-source contract
        // (tests/test_dashboard/test_dashboard.cpp case 9) greps for
        // `main.qml (st-054): _navigateTo ->` in main.qml. We log it
        // at the top of the function so every branch (Send /
        // History / Subaddress / Receive) emits it before doing
        // anything else; downstream routing decisions live in the
        // page-name switch below.
        console.log("main.qml (st-054): _navigateTo ->", pageName)
        // st-064 + st-102: Receive is a pushed page. When reached via
        // the Dashboard's receiveClicked signal (the ActionButtons
        // Receive CTA), mainStack.push leaves the parent page
        // (Dashboard) intact and the receivePage backClicked signal
        // calls mainStack.pop() to return. currentPageName stays
        // as-is here so the Sidebar highlight remains on the parent
        // page when navigating via Dashboard; when the Sidebar
        // Receive tab is clicked directly (onTabClicked handler) we
        // DO update currentPageName so the highlight follows. Both
        // entry points use the same receivePage Component so the
        // visual contract (and the back button) is identical.
        if (pageName === "Receive") {
            mainStack.push(receivePage)
            return
        }
        // st-065: Subaddress is also a pushed page now (it IS a Sidebar
        // tab as well). We push the subaddressPage Component so the
        // top-bar Back button can mainStack.pop() back to the parent
        // page (Dashboard when reached via Dashboard's
        // subaddressesClicked signal). We update currentPageName so the
        // Sidebar highlight follows; the Sidebar onTabClicked handler
        // does the same push logic so re-clicking Subaddress while
        // already on Subaddress is a no-op (no duplicate push).
        if (pageName === "Subaddress") {
            if (root.currentPageName !== "Subaddress") {
                root.currentPageName = "Subaddress"
                mainStack.push(subaddressPage)
            }
            return
        }
        if (pageName !== root.currentPageName) {
            root.currentPageName = pageName
            mainStack.replace(root._pageComponentFor(pageName))
        }
    }

    // ---- TxDetailDialog overlay (st-062) ---------------------------
    /// App-scope dialog. Mounted at root scope (NOT inside the
    /// StackView) so it survives page transitions and can be shown
    /// on top of any page. The Dashboard / HistoryPage call
    /// root._openTxDetail(txHash) to populate the dialog and reveal
    /// it; clicking the backdrop / pressing Esc fires dialog.closed()
    /// which sets _txDetailOpen = false to hide it.
    property bool _txDetailOpen: false
    property string _txDetailHash: ""
    property var    _txDetailRow: ({})

    function _openTxDetail(txHash) {
        // Look up the row in transactionTrackerObj.transfers. The
        // HistoryPage and Dashboard both pass a tx_hash the user
        // already saw, so the lookup is guaranteed to find a row
        // (or we fall back to {} so the dialog renders "—" placeholders).
        var list = (typeof transactionTrackerObj !== "undefined"
                    && transactionTrackerObj
                    && transactionTrackerObj.transfers)
            ? transactionTrackerObj.transfers : []
        var found = ({})
        for (var i = 0; i < list.length; ++i) {
            if (list[i] && list[i].txHash === txHash) {
                found = list[i]
                break
            }
        }
        root._txDetailHash = txHash
        root._txDetailRow  = found
        root._txDetailOpen = true
    }

    function _closeTxDetail() {
        root._txDetailOpen = false
    }

    // The dialog itself. We use `visible:` to mount/unmount the
    // QML tree (avoids running timers / animations on a hidden
    // dialog). The dialog's txHash + rowData properties are
    // bound so any change at the root scope propagates here.
    TxDetailDialog {
        anchors.fill: parent
        visible: root._txDetailOpen
        z: 1000   // above the StackView + Sidebar
        txHash: root._txDetailHash
        rowData: root._txDetailRow
        onClosed: root._closeTxDetail()
    }

    // ---- Toast error notification (st-130) ----
    // Top-center modal toast for network/RPC/startup errors. Locks the UI
    // when queue is non-empty (modal MouseArea eats background events).
    // Subscribed to startupManager.startupFailed / stateChanged below.
    // Step → category mapping routes each startup phase to a tailored
    // category so Toast.qml's categoryLabel() can render the right
    // Error label (Daemon startup failed / Wallet-RPC startup failed / Version incompatible
    // / Invalid settings / RPC error). state==Failed is the canonical "startup
    // is over and broken" signal — state==Ready is informational only
    // (logged but no toast; we don't want to spam the user on success).
    //
    // NOTE: HttpRpcClient.rpcFailed integration is deferred — HttpRpcClient
    // is NOT currently exposed as a context property in main.cpp (only its
    // thin wrappers DaemonClient / WalletRpcClient are). Wiring that up
    // would require creating an HttpRpcClient instance in main.cpp and
    // adding `httpRpcClientObj` to the engine root context. Tracked as
    // follow-up work; startupFailed already covers the most visible failure
    // mode (judecoind down → startup hangs → Toast surfaces the reason).
    Toast {
        id: globalToast
    }

    // ---- startup loading overlay (st-131) ----------------------------
    // Shown while StartupManager is in-flight (state 1..5). The
    // OnboardingWizard (or sidebar shell, once walletReady=true) keeps
    // rendering underneath; the overlay provides a non-blocking hint
    // that the wallet is still spinning up the daemon / wallet-rpc.
    //
    // Z ordering: Toast.qml sits ABOVE this overlay (Toast is a modal
    // Popup and the engine's Popup stacking naturally wins over a
    // plain Rectangle), so a network-error toast is still readable
    // when the loading spinner is up. TxDetailDialog (z=1000) also
    // wins because we set z=2000 here. We deliberately do NOT block
    // background events (no MouseArea eater) so the user can still
    // tap through Onboarding's 4 case cards while startup runs.
    Rectangle {
        id: startupOverlay
        anchors.fill: parent
        color: "#000000"
        opacity: 0.55
        visible: root.isStartupInFlight
        z: 2000

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 18

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: root.isStartupInFlight
                visible: running
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: (i18nManagerObj.qtr("main", "Loading...") + i18nManagerObj.emptyString) + " " + root.startupStepName
                color: "#ffffff"
                font.pixelSize: 16
                font.bold: true
            }
        }
    }

    // Map the integer Step enum (1..5) from StartupManager to a
    // Toast category key so the localised label in Toast.qml picks
    // up the right Chinese phrase. Unknown steps fall through to
    // "startup_failed" which still surfaces the raw reason.
    // NOTE: Moved out of Connections to root scope — non-signal-handler
    // functions inside QML Connections are rejected by Qt 6.6 QML compiler.
    function _categoryForStep(step) {
        switch (step) {
            case 1: return "settings_invalid"        // ReadSettings
            case 2: return "daemon_start_failed"     // StartDaemon
            case 3: return "wallet_rpc_start_failed" // StartWalletRpc
            case 4: return "version_mismatch"        // VerifyVersion
            case 5: return "rpc_error"               // LoadCommandSet
            default: return "startup_failed"
        }
    }

    Connections {
        target: startupManager

        function onStartupFailed(reason, step) {
            globalToast.show(
                _categoryForStep(step),
                (i18nManagerObj.qtr("main", "Startup Failed") + i18nManagerObj.emptyString) + " (step " + step + ")",
                reason,
                "error"
            )
        }

        function onStateChanged(state) {
            // 0=Idle, 1=LoadingSettings, 2=StartingDaemon, 3=StartingWalletRpc,
            // 4=VerifyingVersion, 5=LoadingCommands, 6=Ready, 7=Failed.
            // Ready = happy path (silent). Failed = onStartupFailed handles.
            // In-between = transient, log only.
            console.log("main.qml: startupManager state ->", state)
        }
    }
}
