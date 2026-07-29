// =============================================================================
// Judecoin Wallet - HistoryPage.qml (st-062)
//
// Full transaction history page reachable from Sidebar.qml tab #3 ("History")
// and from --page History on the CLI. Renders the same TxRow.qml row contract
// as the Dashboard's RecentTransactions panel, but on the ENTIRE
// transactionTrackerObj.transfers snapshot (not the 5-row slice).
//
// Layout (per design_specs.md §5.6 + st-062 spec_notes):
//
//   ┌──────────────────────────────────────────────────────┐
//   │  TopBar (page title "History" + back button)        │  56px
//   ├──────────────────────────────────────────────────────┤
//   │  ┌─ TextField (search) ─────────────────── [×] ┐    │  40px - debounced 200ms
//   ├──────────────────────────────────────────────────────┤
//   │  [ All ] [ Incoming ] [ Outgoing ] [ Pending ]      │  TabBar, 40px
//   ├──────────────────────────────────────────────────────┤
//   │  TxRow (filter match)                              │  56px
//   │  TxRow (filter match)                              │
//   │  TxRow (filter match)                              │
//   │  ...                                                │
//   ├──────────────────────────────────────────────────────┤
//   │  (empty / no-match)  - "No matching transactions"  │
//   └──────────────────────────────────────────────────────┘
//
// Search box:
//   * Filters by: txHash, counterparty (address), note, amount string.
//   * 200ms debounce timer so we don't refilter on every keystroke.
//   * "×" clear button on the right edge of the TextField.
//
// Tabs (TabBar):
//   * All       - no extra filter
//   * Incoming  - direction == "in"  (positive amount)
//   * Outgoing  - direction == "out" (negative amount)
//   * Pending   - state == "pending" (block_height == 0, in mempool/pool)
//
// TxDetailDialog (st-062 sibling, see TxDetailDialog.qml):
//   * Click on a TxRow -> HistoryPage emits txClicked(txHash) -> main.qml
//     (or a wrapping StackView) opens a TxDetailDialog over the page with
//     the matching hash. TxDetailDialog owns the per-tx note round-trip:
//     localDataManagerObj.{save,get,remove}TxNote(hash, note).
//
// Bound context properties (set in main.cpp):
//   * transactionTrackerObj.transfers  - QVariantList of QVariantMap
//                                         (full snapshot, NOT the 5-row
//                                         recentTransfers slice). NOTIFY
//                                         signal: transfersChanged().
//   * localDataManagerObj             - LocalDataManager Q_INVOKABLE
//                                         bridge for the per-tx note
//                                         round-trip (used by TxDetailDialog,
//                                         not HistoryPage itself).
//
// V5.4.7 compliance: no key/seed/password/privateKey/payment_id is ever
//                    bound. Only the public-chain tx_hash + user-supplied
//                    counterparty label / note are rendered.
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs via
//                    src/i18n/*.ts).
// V5.4.15 compliance: single Forest Green theme baked in, no theme switch.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet selector.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "HistoryPage"

    // ---- public API -----------------------------------------------------
    /// Emitted when the user clicks a TxRow. The wrapping StackView
    /// (main.qml) opens a TxDetailDialog over the page with the matching
    /// txHash. The dialog owns the note save round-trip; HistoryPage
    /// itself does not call localDataManagerObj directly.
    signal txClicked(string txHash)

    /// Logical page name (matches the Sidebar tab label). The parent
    /// StackView can also pass --page=History via the CLI to land here
    /// on app boot (handled in main.qml's Component.onCompleted).
    readonly property string pageName: "History"

    // ---- search state --------------------------------------------------
    /// Raw input from the TextField. Bound immediately so the input
    /// stays responsive. The actual filter is applied through
    /// _searchFilter (debounced 200ms).
    property string _rawSearch: ""

    /// Debounced search value used by _filterModel(). Refreshed by
    /// _searchDebounceTimer.restart() at every TextField textChanged.
    property string _searchFilter: ""

    /// Active tab index. Drives the HistoryPage filter via _tabFilter().
    /// 0 = All, 1 = Incoming, 2 = Outgoing, 3 = Pending.
    property int _activeTab: 0

    // ---- data layer -----------------------------------------------------
    /// Defensive lookup so missing context property (e.g. QML preview
    /// tooling without an engine) doesn't crash. The HistoryPage needs
    /// the FULL list (not the 5-row slice) so we use the `transfers`
    /// Q_PROPERTY added to TransactionTracker in st-062; .length on
    /// undefined throws — `typeof` short-circuits to a safe 0.
    readonly property int _count:
        typeof transactionTrackerObj !== "undefined"
            && transactionTrackerObj
            && transactionTrackerObj.transfers
            ? transactionTrackerObj.transfers.length
            : 0

    /// Same defensive guard for the model itself. Defaulting to [] keeps
    /// ListView empty (not crashing) when the context property is
    /// missing. Note we bind to `transactionTrackerObj.transfers`
    /// (NOT .recentTransfers) - the 5-row slice would silently cap the
    /// HistoryPage at 5 rows, which is the entire point of having a
    /// separate History view.
    readonly property var _model:
        typeof transactionTrackerObj !== "undefined"
            && transactionTrackerObj
            && transactionTrackerObj.transfers
            ? transactionTrackerObj.transfers
            : []

    /// JS-side filter pipeline. We rebuild a new QVariantList on every
    /// tab change / search change. The list is small (V1.0 user
    /// wallets have < 1k rows) so an O(N) rebuild is fine; we do not
    /// use a QSortFilterProxyModel because we need to filter on
    /// multiple fields (search match AND tab match) at once.
    readonly property var _filtered:
        _filterModel()

    // ---- filter implementation ----------------------------------------
    /// Combined search + tab filter. Returns a new QVariantList of
    /// QVariantMaps that satisfy both predicates. Empty / null
    /// inputs short-circuit to the unfiltered model.
    function _filterModel() {
        var input = _model
        if (!input || input.length === 0) return []
        var needle = _searchFilter.toLowerCase()
        var tab = _activeTab
        var result = []
        for (var i = 0; i < input.length; ++i) {
            var row = input[i]
            if (!row) continue
            if (!_tabMatch(row, tab)) continue
            if (needle.length > 0 && !_searchMatch(row, needle)) continue
            result.push(row)
        }
        return result
    }

    /// Tab predicate. 0=All (no filter), 1=Incoming, 2=Outgoing,
    /// 3=Pending. "direction" comes from wallet-rpc as "in" / "out" /
    /// "pending" / "pool" / "failed"; we accept "in" for Incoming and
    /// "out" for Outgoing. Pending is sourced from the `state` field
    /// added to the `transfers` Q_PROPERTY in st-062 (the C++ side
    /// classifies each row via TransactionTracker::classifyState()).
    function _tabMatch(row, tab) {
        if (tab === 0) return true
        if (tab === 1) return row.direction === "in"
        if (tab === 2) return row.direction === "out"
        if (tab === 3) return row.state === "pending"
        return true
    }

    /// Search predicate. Case-insensitive substring match against:
    /// txHash, counterparty, note, and the amount rendered as a
    /// fixed-precision JDE string ("0.5", "12.345", etc.). Empty
    /// needle is a free pass.
    function _searchMatch(row, needle) {
        if (row.txHash && String(row.txHash).toLowerCase().indexOf(needle) !== -1) return true
        if (row.counterparty && String(row.counterparty).toLowerCase().indexOf(needle) !== -1) return true
        if (row.note && String(row.note).toLowerCase().indexOf(needle) !== -1) return true
        if (row.amount !== undefined && row.amount !== null) {
            // Render atomic units (1 JDE = 1000 atomic) as JDE so the
            // user can search "0.5" to find the half-JDE transfer.
            var jde = (Number(row.amount) / 1000).toFixed(3)
            if (jde.indexOf(needle) !== -1) return true
        }
        return false
    }

    // ---- TopBar (page title + back) -----------------------------------
    /// Forest Green rail header. 56px tall, 24px page-title left, 40x40
    /// back button on the right edge. The back button is wired into the
    /// parent StackView's _navigateTo("Dashboard") helper by main.qml
    /// (we expose a `backClicked` signal so the routing layer can hook
    /// in without poking into the page tree).
    signal backClicked()

    Rectangle {
        id: topBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 56
        color: "#ffffff"             // --surface
        // Bottom border so the rail reads as a header strip.
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: "#dcfce7"         // --border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 12
            spacing: 12

            // Page title (24px Bold per design_specs §3.1 heading token).
            Label {
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("HistoryPage", "History") + i18nManagerObj.emptyString)
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: "#14532d"     // --fg
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            // "N transactions" counter on the right. Reads from the
            // FILTERED list so the user sees the live count update as
            // they type in the search box / switch tabs.
            Label {
                text: (i18nManagerObj.qtr("HistoryPage", "%1 transactions").arg(root._filtered.length) + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                font.weight: Font.Normal
                color: "#6b8074"     // --muted
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // ---- error banner (st-130) ----------------------------------------
    /// Visible when the TransactionTracker enters the Error state
    /// (wallet-rpc unreachable, parse failure, etc.). Surfaces the
    /// last error message with a Retry button so the user isn't left
    /// staring at stale data without knowing why.
    readonly property bool _hasTrackerError:
        typeof transactionTrackerObj !== "undefined"
            && transactionTrackerObj
            && transactionTrackerObj.trackerState === 2  // 2 = Error

    readonly property string _trackerErrorMessage:
        _hasTrackerError && transactionTrackerObj.trackerError
            ? transactionTrackerObj.trackerError : ""

    Rectangle {
        id: errorBanner
        visible: root._hasTrackerError
        anchors {
            top: topBar.bottom
            left: parent.left
            right: parent.right
            topMargin: 0
            leftMargin: 24
            rightMargin: 24
        }
        height: visible ? Math.max(44, errorLayout.implicitHeight + 16) : 0
        color: "#fef2f2"             // red-50
        radius: 8
        border.color: "#fecaca"      // red-200
        border.width: 1

        RowLayout {
            id: errorLayout
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 12

            Label {
                text: (i18nManagerObj.qtr("HistoryPage", "Cannot refresh transactions") + i18nManagerObj.emptyString)
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: "#991b1b"     // red-800
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: root._trackerErrorMessage
                font.pixelSize: 11
                color: "#b91c1c"     // red-700
                elide: Text.ElideMiddle
                Layout.maximumWidth: 200
                visible: root._trackerErrorMessage.length > 0
            }
            Button {
                text: (i18nManagerObj.qtr("HistoryPage", "Retry") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                onClicked: {
                    if (typeof transactionTrackerObj !== "undefined"
                        && transactionTrackerObj) {
                        transactionTrackerObj.refresh()
                    }
                }
                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? "#fecaca" : "#fee2e2"
                    border.color: "#fca5a5"
                    border.width: 1
                }
                contentItem: Label {
                    text: parent.text
                    color: "#991b1b"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // ---- search bar ---------------------------------------------------
    /// 200ms debounce so the HistoryPage doesn't refilter the list on
    /// every keystroke. The 200ms value matches the SendPage's URI
    /// paste debounce (st-060 spec) and feels responsive without
    /// causing visible jank on a 1k-row list.
    Rectangle {
        id: searchBar
        anchors {
            top: topBar.bottom
            left: parent.left
            right: parent.right
            topMargin: 16
            leftMargin: 24
            rightMargin: 24
        }
        height: 40
        radius: 8
        color: "#f0fdf4"             // --surface tint
        border.color: searchField.activeFocus ? "#22c55e" : "#dcfce7"
        border.width: 1

        TextField {
            id: searchField
            anchors {
                fill: parent
                leftMargin: 12
                rightMargin: clearButton.visible ? clearButton.width + 12 : 12
            }
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: (i18nManagerObj.qtr("HistoryPage", "Search address, note, hash, or amount") + i18nManagerObj.emptyString)
            placeholderTextColor: "#6b8074"
            color: "#14532d"
            font.pixelSize: 13
            background: null
            selectByMouse: true
            onTextChanged: {
                root._rawSearch = text
                // 200ms debounce: restart the timer on every keystroke
                // so the filter only runs after the user stops typing.
                _searchDebounceTimer.restart()
            }
        }

        // Clear button. Visible only when there's text. Round button,
        // 16px "×" in --muted color so it reads as a soft affordance
        // (not a destructive red X).
        ToolButton {
            id: clearButton
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: 8
            }
            width: 24
            height: 24
            visible: searchField.text.length > 0
            text: "\u00d7"  // ×
            font.pixelSize: 16
            onClicked: {
                searchField.text = ""
                // Trigger an immediate filter (don't wait the 200ms
                // debounce - the user just clicked the clear).
                root._rawSearch = ""
                root._searchFilter = ""
            }
            background: Rectangle {
                radius: 12
                color: parent.hovered ? "#dcfce7" : "transparent"
            }
            contentItem: Label {
                text: parent.text
                color: "#6b8074"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            ToolTip.text: (i18nManagerObj.qtr("HistoryPage", "Clear search") + i18nManagerObj.emptyString)
            ToolTip.visible: hovered
            ToolTip.delay: 600
        }
    }

    /// 200ms debounce timer. Single-shot; restart() from the TextField
    /// onTextChanged handler resets the 200ms window. onTriggered copies
    /// the raw text into _searchFilter which drives the filter pipeline.
    Timer {
        id: _searchDebounceTimer
        interval: 200
        repeat: false
        onTriggered: root._searchFilter = root._rawSearch
    }

    // ---- TabBar -------------------------------------------------------
    /// 4-tab filter: All / Incoming / Outgoing / Pending. 40px tall,
    /// tabs evenly distributed across the bar (Layout.fillWidth on each
    /// item). Active tab is rendered with the Forest Green accent
    /// (#22c55e) and a 2px underline; inactive tabs are --muted.
    TabBar {
        id: filterBar
        anchors {
            top: searchBar.bottom
            topMargin: 16
            left: parent.left
            right: parent.right
            leftMargin: 24
            rightMargin: 24
        }
        currentIndex: root._activeTab
        onCurrentIndexChanged: root._activeTab = currentIndex
        background: Rectangle {
            // No fill - the TabBar's own TabButton handles the underline.
            color: "transparent"
            // Bottom 1px border so the tab strip reads as a row.
            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 1
                color: "#dcfce7"     // --border
            }
        }

        TabButton {
            text: (i18nManagerObj.qtr("HistoryPage", "All") + i18nManagerObj.emptyString)
            width: implicitWidth + 24
            contentItem: Label {
                text: parent.text
                font.pixelSize: 13
                font.weight: parent.checked ? Font.DemiBold : Font.Normal
                color: parent.checked ? "#14532d" : "#6b8074"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    height: 2
                    color: parent.parent.checked ? "#22c55e" : "transparent"
                }
            }
        }
        TabButton {
            text: (i18nManagerObj.qtr("HistoryPage", "Incoming") + i18nManagerObj.emptyString)
            contentItem: Label {
                text: parent.text
                font.pixelSize: 13
                font.weight: parent.checked ? Font.DemiBold : Font.Normal
                color: parent.checked ? "#14532d" : "#6b8074"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    height: 2
                    color: parent.parent.checked ? "#22c55e" : "transparent"
                }
            }
        }
        TabButton {
            text: (i18nManagerObj.qtr("HistoryPage", "Outgoing") + i18nManagerObj.emptyString)
            contentItem: Label {
                text: parent.text
                font.pixelSize: 13
                font.weight: parent.checked ? Font.DemiBold : Font.Normal
                color: parent.checked ? "#14532d" : "#6b8074"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    height: 2
                    color: parent.parent.checked ? "#22c55e" : "transparent"
                }
            }
        }
        TabButton {
            text: (i18nManagerObj.qtr("HistoryPage", "Pending") + i18nManagerObj.emptyString)
            contentItem: Label {
                text: parent.text
                font.pixelSize: 13
                font.weight: parent.checked ? Font.DemiBold : Font.Normal
                color: parent.checked ? "#14532d" : "#6b8074"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    height: 2
                    color: parent.parent.checked ? "#22c55e" : "transparent"
                }
            }
        }
    }

    // ---- list ---------------------------------------------------------
    /// History ListView. Bound to the FILTERED model (not _model
    /// directly) so the tab + search filters apply at the QML layer.
    /// Each delegate is a TxRow (st-052) using the same row contract as
    /// the Dashboard's RecentTransactions panel.
    ///
    /// Empty states:
    ///   1. tracker is empty (no transactions at all) -> "No
    ///      transactions yet" (mirrors RecentTransactions empty card).
    ///   2. tracker has rows but the filter rejects all of them
    ///      (search misses / wrong tab) -> "No matching transactions"
    ///      + a hint to clear the filter.
    ListView {
        id: txList
        visible: root._filtered.length > 0
        anchors {
            top: filterBar.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: 24
            rightMargin: 24
            bottomMargin: 24
        }
        clip: true
        model: root._filtered
        spacing: 0
        // Subtle 1px rounded background so the list reads as a card
        // when there are many rows (white-on-white otherwise).
        // st-101c ("TransactionList"): the visual_diff_report P0 #306
        // fix_hint removes the outer container border so the list
        // background no longer renders as a card on the page; the
        // white-on-white contrast holds because we keep the white
        // surface but drop the --border ring.
        Rectangle {
            anchors.fill: parent
            color: "#ffffff"
            radius: 8
            border.width: 0
            z: -1
        }

        delegate: TxRow {
            id: rowDelegate
            width: txList.width
            // ---- modelData binding --------------------------------------
            // modelData is the QVariantMap for this row from
            // transactionTrackerObj.transfers. We defensively default
            // every field (?? operator) so a partially-populated entry
            // from a future schema bump doesn't render as "undefined" /
            // NaN. The default direction is "in" so an unparseable row
            // renders as a received-transfer, matching the most-common
            // tx type in V1.0 wallets.
            txHash: (modelData && modelData.txHash !== undefined)
                ? modelData.txHash : ""
            direction: (modelData && modelData.direction !== undefined)
                ? modelData.direction : "in"
            counterparty: (modelData && modelData.counterparty !== undefined)
                ? modelData.counterparty : ""
            amount: (modelData && modelData.amount !== undefined)
                ? modelData.amount : 0
            timestamp: (modelData && modelData.timestamp !== undefined)
                ? modelData.timestamp : 0
            note: (modelData && modelData.note !== undefined)
                ? modelData.note : ""
            // ---- ListView bookkeeping -----------------------------------
            // isLast suppresses the bottom border so the last row doesn't
            // draw a stray line past the list (TxRow.qml design §5.5).
            isLast: index === txList.count - 1
            // useDerivedAvatarColor: opt-in to the 4-shade Forest Green
            // palette so two adjacent received-from-A rows don't both
            // use the same #16a34a background and read as a duplicate
            // (TxRow.qml design §5.5, hash-derived bucket).
            useDerivedAvatarColor: true

            // ---- click routing ------------------------------------------
            // TxRow emits rowClicked(txHash); re-emit as txClicked(txHash)
            // so the parent StackView (main.qml) can listen on the
            // HistoryPage root without poking into the delegate tree.
            onRowClicked: function(txHash) {
                console.log("HistoryPage.qml (st-062): tx clicked ->", txHash)
                root.txClicked(txHash)
            }
        }
    }

    // ---- empty state (no rows in model) -------------------------------
    /// Renders when the tracker has zero rows. Same wording as the
    /// RecentTransactions empty card (consistency across the app) so
    /// the user sees a familiar placeholder.
    Rectangle {
        id: emptyStateNoRows
        visible: root._count === 0
        anchors {
            top: filterBar.bottom
            topMargin: 16
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: 24
            rightMargin: 24
            bottomMargin: 24
        }
        color: "#f0fdf4"             // --surface tint
        radius: 8
        border.color: "#dcfce7"      // --border
        border.width: 1

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8
            width: Math.min(parent.width - 48, 320)

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("HistoryPage", "No transactions yet") + i18nManagerObj.emptyString)
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: "#166534"     // --accent-dark
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("HistoryPage", "Your transfers will appear here as they confirm.") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.Normal
                // st-101c ("TransactionList"): empty-state label color
                // upgraded from --muted (#6b8074) to --fg (#14532d) per
                // visual_diff_report P0 #316 fix_hint. The headline
                // (#166534 --accent-dark) is unchanged so the
                // title-vs-body emphasis stays intact.
                color: "#14532d"     // --fg
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---- empty state (filter rejects all rows) ------------------------
    /// Renders when the tracker has rows but the search/tab filter
    /// rejects all of them. The hint guides the user to clear the
    /// filter rather than leave them wondering "where are my txs?".
    Rectangle {
        id: emptyStateNoMatch
        visible: root._count > 0 && root._filtered.length === 0
        anchors {
            top: filterBar.bottom
            topMargin: 16
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: 24
            rightMargin: 24
            bottomMargin: 24
        }
        color: "#f0fdf4"
        radius: 8
        border.color: "#dcfce7"
        border.width: 1

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8
            width: Math.min(parent.width - 48, 320)

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("HistoryPage", "No matching transactions") + i18nManagerObj.emptyString)
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: "#166534"
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("HistoryPage", "Try a different search term or tab.") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.Normal
                // st-101c ("TransactionList"): empty-state label color
                // upgraded from --muted (#6b8074) to --fg (#14532d) per
                // visual_diff_report P0 #316 fix_hint. The headline
                // (#166534 --accent-dark) is unchanged so the
                // title-vs-body emphasis stays intact.
                color: "#14532d"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }

    // ---- forward focus / accessibility --------------------------------
    /// On first render, focus the search field so the user can start
    /// typing without a click. This matches the design spec's "search
    /// is the primary action" intent. We skip the autofocus on
    /// preview tooling (where there's no real focus chain) by checking
    /// the searchField's `activeFocus` capability.
    Component.onCompleted: {
        // Don't steal focus if the user navigated here from a
        // StackView.push (they probably want to read first); focus the
        // search field only on first mount.
        if (typeof searchField !== "undefined") {
            searchField.forceActiveFocus()
        }
    }
}
