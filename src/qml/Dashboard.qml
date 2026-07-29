// =============================================================================
// Judecoin Wallet - Dashboard.qml (st-054 + st-098a + st-101d)
//
// The first page the user lands on after onboarding completes (post-st-040
// shell). It composes the three hero components built in st-050 / st-051 /
// st-053 -- BalanceCard + ActionButtons + RecentTransactions -- under a
// single TopBar (page title + sync status pill), and routes the ActionButtons
// click signals + RecentTransactions row click signal back up to the
// StackView owner in main.qml.
//
// st-098a responsive contract: the root ColumnLayout already uses
// anchors.fill + 24/32 margins so the layout fills whatever the parent
// StackView hands it (1100x700 default, 1400x900 max, 1080x600 min,
// per main.qml's responsive clamp). All child rows + hero cards use
// Layout.fillWidth so the BalanceCard / ActionButtons /
// RecentTransactions stretch to the full content width on wider
// monitors without overflow. No fixed pixel widths are introduced.
//
// Post-st-040 layout (Sidebar.qml StackView, this file replaces the
// st-040 placeholder):
//
//   ApplicationWindow  (main.qml)
//     └─ StackView
//          └─ Dashboard.qml        <-- this file
//               ├─ TopBar         (Dashboard title + sync status pill)
//               ├─ BalanceCard    (st-050)
//               ├─ ActionButtons  (st-051) -- 3 click signals re-emitted
//               └─ RecentTransactions (st-053) -- row click re-emitted
//
// st-054 contract (must-pass):
//   * Root Item (id: dashboardRoot, objectName: "Dashboard").
//   * Vertical ColumnLayout (anchors.fill parent, margins 24/32 per the
//     mockup, spacing 20 between rows).
//   * TopBar row:
//       - Page title "Dashboard" (h2 token, --external = #166534).
//       - Status pill (#dcfce7 bg, 12px radius, dot + "Synced · N blocks" /
//         "Syncing..." text). Dot is 8x8 #16a34a with shadow-glow; state
//         flips from "synced" -> "syncing" -> "offline" so a future sub-task
//         can wire wallet-rpc's get_info into the binding.
//   * BalanceCard (BalanceCard.qml, st-050): the gradient hero card.
//       - balanceAmount / usdValue / isEmpty are exposed as Dashboard
//         properties so the parent (or a future wallet-rpc bridge) can
//         feed live data. Defaults are safe placeholders.
//   * ActionButtons (ActionButtons.qml, st-051): 3 buttons.
//       - sendClicked / receiveClicked / subaddressesClicked are bubbled
//         up as Dashboard-level signals (re-emitted). main.qml wires
//         those to StackView.replace(<SendPage|ReceivePage|Subaddresses>).
//   * RecentTransactions (RecentTransactions.qml, st-053): 5-row list.
//       - transactionClicked(txHash) is bubbled up as a Dashboard-level
//         signal. main.qml logs the click for now (HistoryPage detail
//         dialog ships in a later sub-task per design_specs §5.7).
//   * Public properties so tests / mocks can drive it:
//       - balanceAmount (double, atomic units), usdValue (double, USD),
//         isEmpty (bool), syncBlockHeight (int), syncState
//         ("synced" | "syncing" | "offline").
//   * V5.4.7 compliance: no key/seed/password/privateKey is ever bound.
//   * V5.4.9 compliance: qsTr() wraps every user-visible string
//     (9 langs via src/i18n/*.ts).
//   * V5.4.15 compliance: single Forest Green theme baked in. Colors
//     come from docs/release/design_specs.md §2 color table.
//   * V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet
//     selector.
//
// st-101d polish (v2 visual batch D, 10 P0 items):
//   * Page background body-bg gradient (135deg #f0fdf4 -> #dcfce7) per
//     design_specs §2.3 + visual_diff_report P0 #1146.
//   * Status dot shadow-glow via QtQuick.Effects MultiEffect (0 0 6px
//     #16a34a / amber-600 for syncing, hidden on offline) per
//     visual_diff_report P0 #756/#1386. The existing st-098a bg #dcfce7,
//     radius 12, no-border contract is held.
//   * TopBar title font.pixelSize 18 / Bold held (already conformant
//     from st-054). No source change required.
//   * Dashboard main ColumnLayout 24/32 margins held (already conformant
//     from st-098a). No source change required.
//
// st-133d: TopBar sync ProgressBar + onInfoUpdated binding.
//   * New `syncProgress` property (real, default 0) — % to target height.
//   * New `ProgressBar { id: syncProgressBar }` placed as a sibling of
//     the TopBar RowLayout inside the outer ColumnLayout, so it spans
//     the full content width and sits 4px tall immediately below the
//     TopBar row when the wallet is syncing. Anchored to `topBar.bottom`
//     (matches the spec's `parent.bottom` intent — parent is the
//     ColumnLayout, topBar.bottom is the visible reference).
//   * New `Connections { target: daemonClientObj; ignoreUnknownSignals: true }`
//     that listens for `infoUpdated(info)` (sync height + synchronized
//     flag, mapped to progress%) and `onDaemonHealthChanged(false)`
//     (reset to 'offline'). The ignoreUnknownSignals guard means the
//     connection silently no-ops if `daemonClientObj` is not yet exposed
//     in the engine root context (it currently isn't — wired by the
//     st-133f/st-133g worker via `engine.rootContext()->setContextProperty`;
//     the binding contract here is the QML half). The defaults
//     (syncBlockHeight=828500 / syncState='synced') are preserved so the
//     visual-diff baseline stays stable when no daemon is connected.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

Item {
    id: dashboardRoot

    // ---- page background (st-101d polish, P0 #1146) -------------------
    // body-bg gradient from design_specs §2.3: #f0fdf4 -> #dcfce7.
    // The design_specs notes a 135deg diagonal but QML Gradient only
    // supports Gradient.Vertical / Gradient.Horizontal out of the box
    // (no native angle parameter). We use Gradient.Vertical to match
    // the existing codebase convention (BalanceCard.qml:135 +
    // Sidebar.qml:154 both use Vertical) so the page background reads
    // as a top-to-bottom wash visually identical to the existing
    // mocks / visual_diff_report P0 #1146 baseline.
    //
    // Anchored to the root Item so it spans whatever the parent
    // StackView hands us (1100x700 default, 1400x900 max, 1080x600
    // min per main.qml's responsive clamp, st-098a). z: -1 keeps it
    // behind the ColumnLayout content.
    Rectangle {
        id: pageBackground
        objectName: "DashboardPageBackground"
        anchors.fill: parent
        z: -1
        // st-161 (2026-07-18): switched to a horizontal gradient
        // that fades from the existing light-green (#f0fdf4) on the
        // LEFT edge to pure WHITE on the right edge. The transition
        // reaches white by 35% from the left ("fast progression" per
        // request), then stays white so the right ~65% of the
        // Dashboard reads as a clean white surface. Matches the
        // reference mockup's left-tinted, right-clean look.
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#f0fdf4" }   // left edge
            GradientStop { position: 0.35; color: "#ffffff" }  // fast -> white
            GradientStop { position: 1.0; color: "#ffffff" }   // stays white
        }
    }

    // a11y (st-095): the Dashboard root is a landmark Pane so AT can jump
    // here directly by role + name; child controls (BalanceCard,
    // ActionButtons, RecentTransactions) carry their own role below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Dashboard", "Dashboard") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("Dashboard", "Balance, actions, and recent transactions") + i18nManagerObj.emptyString)

    // a11y v2 (st-100): forward Tab chain entry. Pressing Tab from the
    // page-level landmark drops focus onto the first action button
    // (Send) so a keyboard-only user lands on the primary CTA. The
    // backtab returns to the page landmark itself (the active focus
    // item is the root Item when no inner control has focus, so this
    // is a no-op in practice -- it's declared for symmetry with
    // SendPage.qml's per-element backtab wiring).
    KeyNavigation.tab: actionButtons.firstFocusable
    KeyNavigation.backtab: dashboardRoot
    activeFocusOnTab: true

    // ---- public marker --------------------------------------------------
    /// Public marker so tests / visual-diff can identify which page is
    /// mounted in the StackView.
    objectName: "Dashboard"

    // ---- public API ------------------------------------------------------
    // The four signals below are bubbled up from the child components
    // (ActionButtons / RecentTransactions) so main.qml can connect a
    // single listener per affordance instead of plumbing each button
    // through the StackView child manually.

    /// Emitted when the user clicks the Send button. main.qml routes this
    /// into StackView.replace(SendPage.qml).
    signal sendClicked()

    /// Emitted when the user clicks the Receive button. main.qml routes
    /// this into StackView.replace(ReceivePage.qml).
    signal receiveClicked()

    /// Emitted when the user clicks the Subaddresses button. main.qml
    /// routes this into StackView.replace(Subaddresses.qml).
    signal subaddressesClicked()

    /// Emitted when the user clicks a TxRow inside the recent-transactions
    /// panel. main.qml logs the click for now (HistoryPage detail dialog
    /// ships in a later sub_task per design_specs §5.7).
    signal transactionClicked(string txHash)

    // ---- data properties (driven by main.qml / future wallet-rpc glue) -
    /// JDE balance in atomic units (1 JDE = 1000 atomic, matching
    /// wallet-rpc conventions). 0 -> BalanceCard renders "No wallet loaded"
    /// per st-050 contract.
    property double balanceAmount: 0.0

    /// st-131: Available (unlocked) balance in atomic units. Populated
    /// by the balance tracker from wallet-rpc get_unlocked_balance or
    /// get_accounts. Locked = balanceAmount - unlockedAmount.
    property double unlockedAmount: 0.0

    /// USD value already computed by the parent. When > 0 it preempts
    /// the priceProviderObj fallback inside BalanceCard.
    property double usdValue: 0.0

    /// When true, BalanceCard hides the title + USD + shows the
    /// "No wallet loaded" placeholder (BalanceCard.qml st-050 contract).
    property bool isEmpty: true

    /// Current blockchain height the wallet is synced to. 0 means
    /// unknown / disconnected. Surfaced in the TopBar status pill as
    /// "Synced · N blocks" (or "Syncing..." when syncState == "syncing",
    /// or "Offline" when syncState == "offline").
    property int syncBlockHeight: 0

    /// FIX-G10 (2026-07-18): network tip height the daemon is
    /// catching up to (bound to daemonClientObj.targetHeight below).
    /// 0 means unknown / already synced. When > syncBlockHeight the
    /// syncing pill renders "Syncing · current / target · N left" and
    /// syncProgress uses the real height/target ratio instead of the
    /// h/(h+10000) approximation.
    property int syncTargetHeight: 0

    /// Sync state. One of:
    ///   "synced"   -- dot green,   text "Synced · N blocks"
    ///   "syncing"  -- dot amber,   text "Syncing..."
    ///   "offline"  -- dot red,     text "Offline"
    /// Default is "synced" with block height 828500 (matches the
    /// design mockup's reference value) so the visual-diff baseline
    /// stays stable until a wallet-rpc bridge lands.
    property string syncState: "synced"

    /// Sync progress percentage 0..100 (st-133d). Drives the thin
    /// ProgressBar under the TopBar when `syncState === "syncing"`.
    /// Default 0 means "no progress to report" (matches the
    /// visual-diff baseline where no ProgressBar is visible — the
    /// bar's `visible:` binding to `syncState === "syncing"` hides
    /// it entirely in the synced/offline baseline states).
    /// When `synchronized === true` this snaps to 100. While syncing,
    /// FIX-G10 computes the real ratio
    ///     height / target_height * 100
    /// when the daemon surfaces target_height (> height), and falls
    /// back to the old approximation height / (height + 10000) * 100
    /// when target_height is 0/unknown. Either way the value is capped
    /// at 99 so the pill stays in the "syncing" state until the
    /// daemon's own `synchronized === true` flag flips it.
    property real syncProgress: 0

    // ---- layout ---------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 20

        // -----------------------------------------------------------------
        // TopBar: page title + sync status pill.
        // -----------------------------------------------------------------
        RowLayout {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 12

            // Page title (left). h2 token = 18px Bold per design_specs 3.1.
            Label {
                id: pageTitle
                text: (i18nManagerObj.qtr("Dashboard", "Dashboard") + i18nManagerObj.emptyString)
                color: "#166534"               // --external (per design_specs §2)
                font.pixelSize: 18
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
                // a11y (st-095): expose the page title as a Heading role so
                // a screen reader announces it as a section heading.
                Accessible.role: Accessible.Heading
                Accessible.name: text
                // a11y v2 (st-100): the page title is a heading
                // landmark, but we still surface a KeyNavigation.tab
                // declaration so a keyboard-only user can Tab from
                // the title into the action button row without
                // detouring through the dashboard root.
                KeyNavigation.tab: actionButtons.firstFocusable
                KeyNavigation.backtab: dashboardRoot
                activeFocusOnTab: true
            }

            // FIX-G11 (2026-07-18): wallet name label. Shows the
            // currently-open wallet filename so the user knows which
            // wallet they are in. Bound to walletManagerObj.currentFilename
            // (empty when no wallet is open).
            Label {
                id: walletNameLabel
                text: (typeof walletManagerObj !== "undefined" && walletManagerObj
                       && walletManagerObj.currentFilename)
                    ? "·  " + walletManagerObj.currentFilename
                    : ""
                color: "#6b8074"
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                visible: text.length > 0
            }

            // Spacer: push the sync pill to the right.
            Item { Layout.fillWidth: true }

            // st-162 (2026-07-18): sync status pill removed from
            // the TopBar right side per request. The sync block is now
            // rendered below the sidebar navigation (see Sidebar.qml st-162).
        }

        // st-162 (2026-07-18): sync progress bar removed from
        // Dashboard. Moved to the sidebar sync block (Sidebar.qml st-162).

        // -----------------------------------------------------------------
        // BalanceCard (st-050). Gradient hero card driven by the four
        // public properties above (balanceAmount / usdValue / isEmpty).
        // -----------------------------------------------------------------
        BalanceCard {
            id: balanceCard
            objectName: "DashboardBalanceCard"
            Layout.fillWidth: true
            // st-161 (2026-07-18): preferredHeight 168 -> 336 so
            // the hero banner card reads as a proper hero (matches the
            // doubled implicitHeight in BalanceCard.qml).
            Layout.preferredHeight: 336
            balanceAmount: dashboardRoot.balanceAmount
            unlockedAmount: dashboardRoot.unlockedAmount
            usdValue: dashboardRoot.usdValue
            isEmpty: dashboardRoot.isEmpty

            // st-161 (2026-07-18): forward the white pill button's
            // click on the hero card to the Dashboard-level sendClicked
            // signal so main.qml's _navigateTo("Send") runs. Pill label
            // is "Send" (BalanceCard.primaryActionLabel default).
            onPrimaryActionClicked: dashboardRoot.sendClicked()

            // Kick off the USD price refresh when the card mounts. The
            // priceProviderObj context property owns its own 5-min
            // auto-refresh timer (PriceProvider::kDefaultAutoRefreshMs)
            // so this one-shot call is enough for the page lifetime.
            Component.onCompleted: {
                if (typeof priceProviderObj !== "undefined"
                        && priceProviderObj
                        && priceProviderObj.refresh) {
                    priceProviderObj.refresh()
                }
            }
        }

        // -----------------------------------------------------------------
        // st-161 (2026-07-18): TabNavRow removed per
        // request. The Sent / Received / Pending / "View all" tabs
        // between the hero card and the recent transactions panel
        // are no longer needed. Layout flows directly from
        // BalanceCard (hero) -> RecentTransactions (panel).
        // -----------------------------------------------------------------

        // -----------------------------------------------------------------
        // ActionButtons (st-051). Three buttons under the BalanceCard.
        // The component emits sendClicked / receiveClicked /
        // subaddressesClicked; we forward each one to the Dashboard-
        // level signal so main.qml can route them through the StackView.
        //
        // st-160 (2026-07-18): hidden per the reference mockup.
        // The white pill button on the hero BalanceCard replaces the
        // Send/Receive/Subaddresses triplet as the primary CTA, so we
        // collapse the row to 0 height + visible:false. The component
        // itself is kept (and its signals still bubble up via the
        // forwarders below) so a future sub_task can re-enable the row
        // by toggling visible back to true without touching Dashboard
        // wiring.
        // -----------------------------------------------------------------
        ActionButtons {
            id: actionButtons
            objectName: "DashboardActionButtons"
            Layout.fillWidth: true
            Layout.preferredHeight: 0
            visible: false

            onSendClicked: dashboardRoot.sendClicked()
            onReceiveClicked: dashboardRoot.receiveClicked()
            onSubaddressesClicked: dashboardRoot.subaddressesClicked()

            // a11y v2 (st-100): the inner sendButton KeyNavigation.tab
            // is `receiveButton` (set inside ActionButtons.qml). The
            // subaddressesButton's KeyNavigation.tab is left null
            // inside the component so we can wire it cross-file here
            // (subaddressesButton -> recentTransactions.firstFocusable)
            // once both sub-components are instantiated. We do the
            // wiring imperatively in Component.onCompleted below
            // because the cross-file attached-property override
            // pattern isn't expressible declaratively in QML without
            // an extra alias property. The aliases
            // (subaddressesButtonRef, firstFocusable, lastFocusable)
            // exposed by ActionButtons.qml keep the wiring explicit.
        }

        // -----------------------------------------------------------------
        // RecentTransactions (st-053). 5-row ListView backed by
        // transactionTrackerObj.recentTransfers. The component emits
        // transactionClicked(txHash); we forward it for main.qml's
        // routing layer (History detail dialog ships later).
        // -----------------------------------------------------------------
        RecentTransactions {
            id: recentTransactions
            objectName: "DashboardRecentTransactions"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 280

            onTransactionClicked: function(txHash) {
                dashboardRoot.transactionClicked(txHash)
            }
        }
    }

    // ---------------------------------------------------------------------
    // a11y v2 (st-100): cross-component chain wiring.
    //
    // The ActionButtons internal chain is
    //     sendButton -> receiveButton -> subaddressesButton
    // (declared inside ActionButtons.qml). The host (Dashboard.qml)
    // extends that chain to:
    //     subaddressesButton -> recentTransactions.firstFocusable
    //     recentTransactions.lastFocusable -> actionButtons.firstFocusable
    //     actionButtons.firstFocusable  -> receiveButton
    //     actionButtons.lastFocusable   -> recentTransactions.firstFocusable
    // so the user can Tab forward through the Send CTA, then the Receive
    // and Subaddresses buttons, then drop into the recent-transactions
    // panel, and Shift+Tab back the same path.
    //
    // We also bind the same `firstFocusable` on the page root so the
    // page-level landmark drops focus into the Send CTA on the first
    // Tab press (no need to mouse over to the action row first).
    //
    // Imperative because QML doesn't expose a declarative attached-
    // property override on a sibling's child without an alias property;
    // the alias properties (actionButtons.subaddressesButtonRef,
    // recentTransactions.firstFocusable / lastFocusable) keep the
    // intent explicit and avoid string-typed lookups.
    // ---------------------------------------------------------------------
    // ---------------------------------------------------------------------
    // FIX-G9b (2026-07-17 21:51): replaced the Connections block
    // (which relied on Q_GADGET DaemonInfo signal parameter round-tripping
    // that Qt 6.6.3 QML cannot resolve without explicit type registration)
    // with reactive Q_PROPERTY bindings on daemonClientObj. The three
    // C++ Q_PROPERTY entries (lastHeight / isSynchronized / isHealthy)
    // are simple int/bool types that QML handles natively. Their NOTIFY
    // signals fire on every successful get_info poll and on liveness
    // transitions, so the bindings below re-evaluate automatically.
    //
    // Bindings (evaluated in order by the QML engine):
    //   syncBlockHeight  = daemonClientObj.lastHeight
    //   syncState        = "offline" / "synced" / "syncing"
    //   syncProgress     = 0..100 % (only meaningful while syncing)
    //
    // Companion changes:
    //   - DaemonClient.h: Q_PROPERTY + NOTIFY signals
    //   - DaemonClient.cpp: emit lastHeightChanged / isSynchronizedChanged
    // ---------------------------------------------------------------------

    // syncBlockHeight: directly bound to the C++ Q_PROPERTY.
    // Falls back to 0 when daemonClientObj is absent (e.g. screenshot mode).
    Binding {
        target: dashboardRoot
        property: "syncBlockHeight"
        value: (typeof daemonClientObj !== "undefined" && daemonClientObj)
            ? daemonClientObj.lastHeight
            : 0
        when: typeof daemonClientObj !== "undefined" && daemonClientObj
    }

    // FIX-G10: syncTargetHeight bound to the C++ targetHeight Q_PROPERTY
    // (network tip height; 0 when synced/unknown). Same defensive shape
    // as the syncBlockHeight binding above.
    Binding {
        target: dashboardRoot
        property: "syncTargetHeight"
        value: (typeof daemonClientObj !== "undefined" && daemonClientObj)
            ? daemonClientObj.targetHeight
            : 0
        when: typeof daemonClientObj !== "undefined" && daemonClientObj
    }

    // syncState: three-way state machine bound to daemonClientObj.
    Binding {
        target: dashboardRoot
        property: "syncState"
        value: {
            if (typeof daemonClientObj === "undefined" || !daemonClientObj)
                return "synced"
            if (!daemonClientObj.isHealthy)
                return "offline"
            return daemonClientObj.isSynchronized ? "synced" : "syncing"
        }
        when: typeof daemonClientObj !== "undefined" && daemonClientObj
    }

    // syncProgress: percentage bound to daemon height + synchronized flag.
    // When synchronized, snaps to 100. While syncing, FIX-G10 uses the
    // real height/target ratio when the daemon surfaces target_height;
    // falls back to the old h/(h+10000) approximation when target_height
    // is 0/unknown. Capped at 99 so the pill stays in the "syncing"
    // state until `synchronized === true` flips it.
    Binding {
        target: dashboardRoot
        property: "syncProgress"
        value: {
            if (typeof daemonClientObj === "undefined" || !daemonClientObj)
                return 0
            if (daemonClientObj.isSynchronized)
                return 100
            const h = daemonClientObj.lastHeight
            if (h <= 0)
                return 0
            const t = daemonClientObj.targetHeight
            if (t > h)
                return Math.min(99, (h / t) * 100)
            return Math.min(99, (h / (h + 10000)) * 100)
        }
        when: typeof daemonClientObj !== "undefined" && daemonClientObj
    }

    Component.onCompleted: {
        // Outbound: subaddressesButton (last button in the action row)
        //          -> recentTransactions.firstFocusable (panel anchor).
        if (actionButtons && actionButtons.subaddressesButtonRef) {
            actionButtons.subaddressesButtonRef.KeyNavigation.tab =
                recentTransactions.firstFocusable
        }
        // Outbound: recentTransactions panel anchor -> cycle back to
        //          the action row's first button (Send CTA) so the
        //          chain is a closed loop. Mirrors the
        //          submitButton -> backButton cycle in SendPage.qml.
        if (recentTransactions && recentTransactions.lastFocusable) {
            recentTransactions.lastFocusable.KeyNavigation.tab =
                actionButtons.firstFocusable
            recentTransactions.lastFocusable.KeyNavigation.backtab =
                actionButtons.lastFocusable
        }
    }

    // ---------------------------------------------------------------------
    // st-133e: wallet-action guard for the "syncing but
    // not yet caught up" window. StartupManager no longer waits for
    // info.synchronized === true (see src/core/startup/StartupManager.cpp
    // ::stepVerifyVersion), so by the time Dashboard is mounted the
    // daemon may still be polling peers for blocks. Without this
    // guard, clicking Send / Receive / Subaddress while the chain is
    // mid-sync would race against a half-loaded wallet-rpc and
    // produce confusing failures (silent empty list, RPC timeout,
    // etc.).
    //
    // Behaviour:
    //   * Returns true  -> the caller should proceed with the action.
    //   * Returns false -> a Toast has been queued, the caller MUST
    //                      NOT navigate to the underlying page.
    //
    // Source of truth:
    //   * Primary   : dashboardRoot.syncState -- driven by the
    //                 Connections block above from
    //                 daemonClientObj.infoUpdated(info). info is
    //                 already in scope at this point so we don't need
    //                 to re-query the daemon.
    //   * Secondary : daemonClientObj.lastInfo.synchronized -- exact
    //                 spec contract; works once DaemonClient exposes
    //                 `lastInfo` as a Q_PROPERTY (st-133f) and the
    //                 context property is wired (st-133g). The
    //                 `typeof` + `!== null` guards keep this helper
    //                 compiling + safe before that lands.
    //
    // Note: Toast.qml's `show(category, title, message, severity)`
    // signature is what main.qml::onStartupFailed already calls --
    // `_requireSynced` uses the same shape (severity "info", not
    // "error") so the surfaced pill is blue not red.
    // ---------------------------------------------------------------------
    function _requireSynced(actionName) {
        var isSynced = true
        if (typeof daemonClientObj !== "undefined"
                && daemonClientObj !== null
                && daemonClientObj.lastInfo
                && typeof daemonClientObj.lastInfo.synchronized !== "undefined") {
            isSynced = daemonClientObj.lastInfo.synchronized === true
        } else {
            // Fallback: dashboardRoot.syncState is updated by the
            // Connections block above. Default "synced" matches the
            // visual-diff baseline (no ProgressBar) so the helper
            // never blocks in the screenshot harness.
            isSynced = dashboardRoot.syncState === "synced"
        }
        if (!isSynced) {
            globalToast.show(
                "info",
                (i18nManagerObj.qtr("Dashboard", "Syncing") + i18nManagerObj.emptyString),
                (i18nManagerObj.qtr("Dashboard", "Blockchain is still syncing, please wait...") + i18nManagerObj.emptyString),
                "info"
            )
            return false
        }
        return true
    }
}
