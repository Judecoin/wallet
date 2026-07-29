// =============================================================================
// Judecoin Wallet - RecentTransactions.qml (st-053 + st-101c polish)
//
// Dashboard "recent transactions" panel. Wraps a 5-row ListView around the
// reusable TxRow.qml component (st-052) using
// `transactionTrackerObj.recentTransfers` (a QVariantList slice capped at
// TransactionTracker::kRecentTransfersLimit == 5 rows, see
// src/core/tracker/TransactionTracker.h) as the model.
//
// st-101c (visual polish batch C, "TxRowList" per the spec) updates the
// empty-state card so it reads as a content card rather than a tinted
// placeholder:
//   * background: #ffffff (was #f0fdf4 --surface tint)
//   * border:     1px solid #bbf7d0 (--border-card; was #dcfce7 --border)
//   * implicitHeight: 120 (was unbounded anchors.fill; the empty state
//     used to "stack" the available vertical space, producing an 80px+
//     gap below the headline. The fixed 120px implicitHeight + 10px
//     vertical padding matches the mockup's compact "no transactions"
//     card.)
//
// Layout (per design_specs.md §5.5 + st-053 spec_notes):
//   ┌──────────────────────────────────────────────────┐
//   │  Recent transactions                            │  <- title (16px Bold)
//   ├──────────────────────────────────────────────────┤
//   │  TxRow tx 1 (32x32 avatar + name + status + ↑/↓ + amount)  │  <- 56px row
//   │  TxRow tx 2                                                         │  <- 56px row
//   │  TxRow tx 3                                                         │  <- 56px row
//   │  TxRow tx 4                                                         │  <- 56px row
//   │  TxRow tx 5 (last; bottom border suppressed)                         │  <- 56px row
//   └──────────────────────────────────────────────────┘
//
// Empty state (model.count === 0) — st-101c container:
//   ┌──────────────────────────────────────────────────┐
//   │  Recent transactions                            │  <- title still shown
//   ├──────────────────────────────────────────────────┤
//   │  ╭────────────────────────────────────────────╮ │  <- 1px --border-card
//   │  │          No transactions yet              │ │  <- 14px Bold, 10px pad
//   │  │   Your recent transfers will appear here.  │ │  <- 12px Regular
//   │  ╰────────────────────────────────────────────╯ │  <- implicitHeight 120
//   └──────────────────────────────────────────────────┘
//
// Public API:
//   signal transactionClicked(txHash)
//     - emitted when the user clicks any TxRow. Parents (Dashboard.qml in
//       st-054) route this into a StackView push of the HistoryPage detail
//       (TxDetailDialog from design_specs §5.7) for the matching txHash.
//
// Bound context property (set in main.cpp, st-053):
//   transactionTrackerObj : TransactionTracker
//     - .recentTransfers -> QVariantList of QVariantMap (txHash, direction,
//       counterparty, amount, timestamp, note); NOTIFY
//       recentTransfersChanged().
//
// V5.4.7 compliance: no key/seed/password is ever bound — the only sensitive
//                   identifiers are public-chain tx_hashes + user-supplied
//                   strings (counterparty address alias, optional note).
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs via
//                   src/i18n/*.ts).
// V5.4.15 compliance: single Forest Green theme baked in, no theme switch.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet selector.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Shapes

Item {
    id: root
    objectName: "RecentTransactions"

    // ---- public API -----------------------------------------------------
    /// Emitted when the user clicks a TxRow. Parents (Dashboard.qml in
    /// st-054) bind this to StackView.push() of the HistoryPage detail
    /// view keyed on txHash.
    signal transactionClicked(string txHash)

    // ---- a11y v2 (st-100) chain entry points ---------------------------
    // Exposes a single focusable Item that the parent (Dashboard.qml)
    // can wire its KeyNavigation chain into. The anchor is an invisible
    // 1x1 Item placed over the panel so the user can Tab into the
    // recent-transactions region; from there the user clicks an
    // individual row to drill into the HistoryPage detail. We don't
    // auto-focus the first row because the ListView delegates are
    // dynamically created (the user typically wants to scan the list
    // first); the anchor is the predictable "I am now in the recent
    // transactions panel" stop in the Tab chain.
    readonly property Item firstFocusable: keynavAnchor
    /// Last focusable widget in the panel (cycles back to the page
    /// landmark). Mirrors the `lastFocusable` pattern in
    /// ActionButtons.qml so the host can build a closed Tab loop.
    readonly property Item lastFocusable:  keynavAnchor

    // Invisible focusable anchor. Sized 1x1 and positioned in the
    // title area so focus lands on a "header"-like coordinate; the
    // user perceives the focus rectangle on the title strip rather
    // than on a specific row, which is honest about the affordance
    // (the actual clickable rows are below).
    Item {
        id: keynavAnchor
        objectName: "RecentTransactionsKeynavAnchor"
        anchors.top: titleRow.top
        anchors.left: titleRow.left
        width: 1
        height: 1
        // a11y v2 (st-100): announce as a static region describing
        // the recent transactions panel. Screen readers read the
        // announcement once when focus lands here.
        Accessible.role: Accessible.StaticText
        Accessible.name: (i18nManagerObj.qtr("RecentTransactions", "Recent transactions") + i18nManagerObj.emptyString)
        Accessible.description: (i18nManagerObj.qtr("RecentTransactions", "Press Down to step into the list, or use the row click affordance") + i18nManagerObj.emptyString)
        // The anchor participates in the Tab chain via the host's
        // wiring (Dashboard.qml sets the inbound KeyNavigation.tab on
        // subaddressesButton -> recentTransactions.firstFocusable).
        // No outbound tab is set here so the chain naturally wraps
        // back to the host's root landmark.
        activeFocusOnTab: true
    }

    // st-162 (2026-07-18): drop shadow removed per
    // request ("remove border shadow"). The MultiEffect shadow block is
    // deleted and the card surface below is borderless.

    // ---- derived state -------------------------------------------------
    /// Defensive lookup so missing context property (e.g. QML preview
    /// tooling without an engine) doesn't crash. .length on undefined
    /// would throw — `typeof` short-circuits to a safe 0.
    readonly property int _count:
        typeof transactionTrackerObj !== "undefined"
            && transactionTrackerObj
            && transactionTrackerObj.recentTransfers
            ? transactionTrackerObj.recentTransfers.length
            : 0

    /// Same defensive guard for the model itself. Defaulting to [] keeps
    /// ListView empty (not crashing) when the context property is
    /// missing.
    readonly property var _model:
        typeof transactionTrackerObj !== "undefined"
            && transactionTrackerObj
            && transactionTrackerObj.recentTransfers
            ? transactionTrackerObj.recentTransfers
            : []

    // ---- card surface (st-162) ---------------------------------
    // White rounded card. Border and shadow removed per request
    // ("remove border shadow"). The surface reads as a clean white panel.
    Rectangle {
        id: cardSurface
        objectName: "RecentTransactionsCard"
        anchors.fill: parent
        z: -1
        color: "#ffffff"
        radius: 12
    }

    // ---- title ----------------------------------------------------------
    /// "Recent transactions" header with clock icon (st-162).
    /// 16px DemiBold per design_specs §3.1 (heading token).
    /// Forest Green foreground (#14532d --fg).
    RowLayout {
        id: titleRow
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        spacing: 8

        // st-162 (2026-07-18): clock icon next to the title
        // per request ("create an icon"). Same clock path-data used
        // in Sidebar.qml for the History tab icon.
        Shape {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            antialiasing: true
            ShapePath {
                strokeColor: "#14532d"
                strokeWidth: 1.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: "M3 12 C3 7 7 3 12 3 C17 3 21 7 21 12 C21 17 17 21 12 21 C7 21 3 17 3 12 Z " +
                          "M12 7 V12 L15.5 14"
                }
            }
        }

        Label {
            id: titleLabel
            text: (i18nManagerObj.qtr("RecentTransactions", "Recent transactions") + i18nManagerObj.emptyString)
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: "#14532d"                     // --fg per V5.4.15 Forest Green
            topPadding: 4
            bottomPadding: 12
            elide: Text.ElideRight
        }
    }

    // ---- empty state ----------------------------------------------------
    /// Visible only when the model is empty (no transfers yet). Renders
    /// a centered placeholder card so the panel doesn't collapse to a
    /// 0px sliver and the user sees "everything's working, just no txs
    /// yet" rather than a blank gap.
    ///
    /// st-101c ("TxRowList") visual polish: the empty-state container
    /// now reads as a content card with a white surface + soft
    /// --border-card ring + a fixed implicitHeight of 120 + 10px
    /// vertical padding. The pre-st-101c implementation used
    /// `anchors.fill: parent` which let the card stretch to the full
    /// panel height (often 240-320px on Dashboard) producing a large
    /// empty band below the headline — the "stacked empty area" the
    /// visual_diff_report P0 #96 fix_hint calls out.
    ///
    /// Palette (st-101c):
    ///   * background #ffffff                  (was --surface tint #f0fdf4)
    ///   * border 1px solid #bbf7d0            (was --border #dcfce7)
    ///   * implicitHeight 120 (fixed; not fill parent)
    ///   * padding 10px vertical
    /// Headline + subline colors unchanged (#166534 + #6b8074) so
    /// test_recent_transactions contract case 4 (palette tokens) keeps
    /// the headline color stable; only the surface + border swapped.
    Rectangle {
        id: emptyState
        visible: root._count === 0
        anchors {
            top: titleLabel.bottom
            topMargin: 8
            left: parent.left
            right: parent.right
        }
        // st-101c: fixed implicitHeight of 120 (was unbounded via
        // anchors.fill). The parent (Dashboard.qml) allocates at
        // least 280px for the panel, so the card has comfortable
        // top/bottom whitespace around it without stretching.
        implicitHeight: 120
        // st-101c: white card surface with --border-card ring.
        color: "#ffffff"
        radius: 8
        border.color: "#bbf7d0"              // --border-card
        border.width: 1

        // st-175 (2026-07-19): removed unnecessary Item wrapper
        // that caused "Cannot anchor to an item that isn't a parent or
        // sibling" warning (Qt 6.6). ColumnLayout now anchors directly
        // to emptyState with 10px vertical padding.
        ColumnLayout {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 24
                rightMargin: 24
            }
                spacing: 8

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("RecentTransactions", "No transactions yet") + i18nManagerObj.emptyString)
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    color: "#166534"             // --accent-dark
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("RecentTransactions", "Your recent transfers will appear here.") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.Normal
                    color: "#6b8074"             // --muted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
    }

    // ---- list -----------------------------------------------------------
    /// Up to 5 rows (TransactionTracker::kRecentTransfersLimit == 5).
    /// delegate: TxRow (st-052) reuses the same contract as HistoryPage
    /// will in st-062, so the row contract stays single-sourced.
    ///
    /// We deliberately do NOT cap the ListView's `count` ourselves; the
    /// C++ side already caps the slice at 5 rows. Capping in two places
    /// would let them drift apart.
    ListView {
        id: txList
        visible: root._count > 0
        anchors {
            top: titleRow.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        model: root._model
        spacing: 0

        delegate: TxRow {
            id: rowDelegate
            width: txList.width
            // ---- modelData binding ---------------------------------------
            // modelData is the QVariantMap for this row from
            // transactionTrackerObj.recentTransfers. We defensively
            // default every field (?? operator) so a partially-populated
            // entry from a future schema bump doesn't render as
            // "undefined" / NaN.
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
            // ---- ListView bookkeeping ------------------------------------
            // isLast suppresses the bottom border so the last row doesn't
            // draw a stray line past the list (TxRow.qml design §5.5).
            isLast: index === txList.count - 1
            // useDerivedAvatarColor: opt-in to the 4-shade Forest Green
            // palette so two adjacent received-from-A rows don't both
            // use the same #16a34a background and read as a duplicate
            // (TxRow.qml design §5.5, hash-derived bucket).
            useDerivedAvatarColor: true

            // ---- click routing -------------------------------------------
            // TxRow emits rowClicked(txHash); re-emit as
            // transactionClicked(txHash) so Dashboard.qml can listen on
            // the panel root without poking into the delegate tree.
            onRowClicked: function(txHash) {
                console.log("RecentTransactions.qml (st-053): tx clicked ->",
                            txHash)
                root.transactionClicked(txHash)
            }
        }
    }
}
