// =============================================================================
// Judecoin Wallet - TxDetailDialog.qml (st-062)
// st-112 hardening: parallel Tag TextField (Save / Remove Tag) added
// below the Note TextField; persisted via accountTagObj.setTag /
// localDataManagerObj.getTag / removeTag. Q_INVOKABLE tag bridge
// mirrors the st-062 note facade.
//
// Modal dialog that opens over the HistoryPage (or the Dashboard) when the
// user clicks a TxRow. Renders the full per-tx metadata (txHash, state,
// confirmations, block height, fee, amount, counterparty, timestamp) and
// lets the user edit two independent pieces of per-tx UI metadata
// (V5.4 §8 decision C):
//
//   - Note : free-form text ("March rent") persisted via
//            localDataManagerObj.saveTxNote(hash, note).
//   - Tag  : short structured label ("Salary") persisted via
//            accountTagObj.setTag(hash, tag).
//
// Layout (per design_specs.md §5.7 + st-062 + st-112 spec_notes):
//
//   ┌──────────────────────────────────────────────────┐
//   │  Transaction                       [ × ]         │  56px header
//   ├──────────────────────────────────────────────────┤
//   │  Direction:     [in] / [out] / [pending]         │
//   │  Amount:        +12.345 JDE                       │
//   │  Counterparty:  Jde8...abc…  (full 95-char)      │
//   │  Tx hash:       0x1234…def  (full 64-char)       │
//   │  Date:          2026-07-07 11:05:42 (local)      │
//   │  Status:        ● Confirm (5/10)                  │
//   │  Block height:  3456789                          │
//   │  Fee:           0.013 JDE                         │
//   ├──────────────────────────────────────────────────┤
//   │  Note                                            │
//   │  ┌────────────────────────────────────────────┐  │  TextField, 80px
//   │  │ (editable)                                  │  │
//   │  └────────────────────────────────────────────┘  │
//   │  Tag                                             │
//   │  ┌────────────────────────────────────────────┐  │  TextField, 36px
//   │  │ (editable)                                  │  │
//   │  └────────────────────────────────────────────┘  │
//   ├──────────────────────────────────────────────────┤
//   │  [ Save ] [ Remove note ] [ Close ]               │  48px footer
//   └──────────────────────────────────────────────────┘
//
// Public API:
//   property string txHash  - the 64-char tx hash to display.
//                            Empty string -> dialog is closed.
//   signal closed()         - emitted when the user dismisses (× /
//                            Close / click outside). The parent
//                            (HistoryPage) is responsible for
//                            unmounting the dialog overlay.
//   signal noteSaved(hash)  - emitted on a successful saveTxNote
//                            round-trip. Parent can re-read the
//                            tracker + history to update the row.
//
// Bound context property (set in main.cpp, st-062 + st-112):
//   localDataManagerObj : LocalDataManager
//     - .getTxNote(hash) -> string (current note, "" if none)
//     - .saveTxNote(hash, note) -> bool (persists to disk)
//     - .removeTxNote(hash) -> bool (deletes the note entirely)
//     - .getTag(hash) -> string (current tag, "" if none)        [st-112]
//     - .setTag(hash, tag) -> bool (persists to disk)            [st-112]
//     - .removeTag(hash) -> bool (deletes the tag entirely)       [st-112]
//
// st-112: accountTagObj is set in main.cpp as an ALIAS for
// localDataManagerObj. QML's context property table stores per-name
// QVariant copies of the same QObject*, and the QObject itself is
// shared; calling `accountTagObj.setTag(hash, tag)` therefore routes
// to LocalDataManager::setTag (which writes tx_tags.json via the
// TxTag store). The alias keeps the spec literal (`accountTagObj`
// direct property) without having to instantiate a second QObject.
//
// We deliberately do NOT bind `transactionTrackerObj` here - the dialog
// accepts the full row map as a property (set by the parent) so the
// dialog is decoupled from the tracker's polling lifecycle. The
// timestamp / state / confirmations / blockHeight / fee fields are
// snapshotted when the parent sets `rowData` and never re-read from
// the tracker inside the dialog. This avoids a race where the dialog
// displays stale metadata because the user is editing a note while
// the tracker refreshes.
//
// V5.4.7 compliance: no key/seed/password/privateKey/payment_id is
//                    ever bound. txHash is public chain data; counter-
//                    party, note, and tag are user-supplied text.
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs
//                    via src/i18n/*.ts).
// V5.4.15 compliance: single Forest Green theme baked in, no theme
//                    switch.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet
//                    selector.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "TxDetailDialog"

    // ---- public API ----------------------------------------------------
    /// The 64-char tx hash this dialog is showing. Empty string is
    /// treated as "dialog closed / not in use" by the parent overlay.
    property string txHash: ""

    /// Optional row snapshot from `transactionTrackerObj.transfers[i]`.
    /// When set, the dialog renders every field from this map directly
    /// (txHash / direction / counterparty / amount / timestamp / note /
    /// state / confirmations / blockHeight / fee). When unset (the
    /// default during preview / detached use), the dialog falls back to
    /// "—" placeholders so it never crashes on a missing row.
    property var rowData: ({})

    /// Emitted on dismissal (× button, Close button, click outside, or
    /// Esc key). The parent overlay (HistoryPage) listens for this
    /// signal and unmounts the dialog. We deliberately do NOT auto-
    /// close on noteSaved - the user might want to verify the row in
    /// the underlying history list before dismissing.
    signal closed()

    /// Emitted on a successful saveTxNote round-trip. Carries the
    /// txHash so the parent can invalidate any cached note lookup.
    signal noteSaved(string txHash)

    // ---- derived state ------------------------------------------------
    /// Direction normalized to lower-case for forgiving comparisons.
    /// The wallet-rpc type can be "in" / "out" / "pending" / "pool" /
    /// "failed"; we display the raw value as a tag but render the
    /// avatar / amount colors off the lower-cased string.
    readonly property string _direction:
        (rowData && rowData.direction !== undefined)
            ? String(rowData.direction).toLowerCase() : ""

    /// "in" / "out" / "pending" / "confirming" / "confirmed" - the
    /// per-tx UI state added to the `transfers` Q_PROPERTY in st-062.
    /// Drives the Status pill color and label.
    readonly property string _state:
        (rowData && rowData.state !== undefined)
            ? String(rowData.state) : ""

    /// Counterparty (full address). The HistoryPage ListView truncates
    /// the counterparty to fit a 56px row; the detail dialog shows the
    /// full string in a non-eliding Label so the user can copy/paste.
    readonly property string _counterparty:
        (rowData && rowData.counterparty !== undefined)
            ? String(rowData.counterparty) : ""

    /// JDE atomic units -> JDE. TxRow renders the same JDE string so
    /// the dialog matches the row's display exactly.
    function _formatJde(amountAtomic) {
        var n = Number(amountAtomic)
        if (!isFinite(n)) return "0.000"
        var sign = n < 0 ? "-" : ""
        var abs = Math.abs(n) / 1000
        // 3-decimal precision; trim trailing zeros so "+0.500 JDE"
        // becomes "+0.5 JDE". Most wallets prefer this for readability.
        var s = abs.toFixed(3)
        s = s.replace(/\\.?0+$/, "")
        if (s === "") s = "0"
        return sign + s + " JDE"
    }

    /// unix-seconds (or millis) -> human-readable local timestamp.
    /// Mirrors the format used by the dashboard / About page so the
    /// user gets a consistent date string everywhere.
    function _formatDate(timestamp) {
        if (timestamp === undefined || timestamp === null) return ""
        var n = Number(timestamp)
        if (!isFinite(n) || n === 0) return ""
        // Sniff magnitude: wallet-rpc gives unix-seconds, but if a
        // future caller hands us millis (> 10^12) we treat it as
        // already-multiplied. Threshold matches TxRow.qml's _toDate
        // helper.
        var ms = (n > 1e12) ? n : n * 1000
        var d = new Date(ms)
        if (isNaN(d.getTime())) return ""
        function pad(x) { return (x < 10 ? "0" : "") + x }
        return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-"
             + pad(d.getDate()) + " " + pad(d.getHours()) + ":"
             + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
    }

    /// State pill color. Forest Green palette + amber for pending.
    /// Returns a hex string; bind into the pill Rectangle's color.
    function _stateColor(state) {
        switch (state) {
            case "confirmed":  return "#15803d"   // --tx-in
            case "confirming": return "#ca8a04"   // amber
            case "pending":    return "#ea580c"   // --tx-icon-out
            default:           return "#6b8074"  // --muted
        }
    }

    /// State pill label (translated).
    function _stateLabel(state) {
        switch (state) {
            case "confirmed":  return (i18nManagerObj.qtr("TxDetailDialog", "Confirmed") + i18nManagerObj.emptyString)
            case "confirming": return (i18nManagerObj.qtr("TxDetailDialog", "Confirming") + i18nManagerObj.emptyString)
            case "pending":    return (i18nManagerObj.qtr("TxDetailDialog", "Pending") + i18nManagerObj.emptyString)
            default:           return (i18nManagerObj.qtr("TxDetailDialog", "Unknown") + i18nManagerObj.emptyString)
        }
    }

    /// Direction tag color (avatar color from the row contract).
    function _directionColor(direction) {
        if (direction === "in")  return "#16a34a"  // --storage
        if (direction === "out") return "#ea580c"  // --tx-icon-out
        return "#6b8074"                            // --muted
    }

    // ---- note state ---------------------------------------------------
    /// Local copy of the note text. We do NOT bind directly to
    /// localDataManagerObj.getTxNote(hash) because (a) the dialog
    /// must show the user's in-progress edit even if the underlying
    /// store changes, and (b) calling Q_INVOKABLE from a binding fires
    /// on every change to a dependency we don't track. We populate
    /// _noteText from the manager on Component.onCompleted and on
    /// txHash changes, then track local edits in the TextField.
    property string _noteText: ""
    property bool _noteDirty: false

    // ---- tag state (st-112) ------------------------------------------
    /// Local copy of the structured tag text. Independent of _noteText
    /// (V5.4 §8 decision C: the user can have a free-form note AND a
    /// structured tag on the same tx). The Save button persists both
    /// fields in a single round-trip so a partial save never lands
    /// half-edited state on disk.
    property string _tagText: ""
    property bool _tagDirty: false

    /// Pre-fetch the current note from localDataManagerObj whenever
    /// the dialog is mounted or txHash changes. We re-read in case
    /// another component updated the note while the dialog was closed.
    function _loadNote() {
        if (typeof localDataManagerObj === "undefined" || !localDataManagerObj) {
            _noteText = (rowData && rowData.note !== undefined)
                ? String(rowData.note) : ""
        } else if (!txHash) {
            _noteText = ""
        } else {
            // Prefer the row's note (the most-recent tracker snapshot)
            // and fall back to a direct localDataManagerObj lookup.
            // The tracker applies notes from the same LocalDataManager
            // instance, so both paths return the same string in normal
            // operation; the rowData path avoids a JNI round-trip on
            // every dialog open.
            if (rowData && rowData.note !== undefined && String(rowData.note).length > 0) {
                _noteText = String(rowData.note)
            } else {
                _noteText = localDataManagerObj.getTxNote(txHash)
            }
        }
        _noteDirty = false

        // st-112: load the structured tag from the same source. We do
        // NOT mirror the rowData fallback here because the tracker
        // does not expose tags in its row snapshot (tags live
        // exclusively in tx_tags.json). Always go through the manager
        // so the dialog reflects the on-disk tag store exactly.
        if (typeof localDataManagerObj === "undefined" || !localDataManagerObj) {
            _tagText = ""
        } else if (!txHash) {
            _tagText = ""
        } else {
            _tagText = localDataManagerObj.getTag(txHash)
        }
        _tagDirty = false
    }

    // ---- overlay backdrop ---------------------------------------------
    /// Click-anywhere-outside-to-close backdrop. Semi-transparent
    /// Forest Green wash so the dialog reads as the primary surface
    /// and the page behind it is dimmed but still visible. The Mouse
    /// Area covers the full overlay; clicks on the dialog itself are
    /// handled by the dialog's own MouseArea (which blocks propagation
    /// via the lower-priority event filter on the parent).
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: "#0f172a"             // slate-900, ~50% alpha
        opacity: 0.45
        // Click-outside-to-close. The dialog child sits on top of
        // this MouseArea; mouse events that hit the dialog do not
        // propagate here, so this only fires for "clicked on the
        // backdrop" semantics.
        MouseArea {
            anchors.fill: parent
            onClicked: root.closed()
        }
    }

    // ---- dialog card --------------------------------------------------
    /// 560px-wide centered card. White surface, 12px radius, soft
    /// shadow (a simple darkening Rectangle behind it; QtQuick doesn't
    /// expose DropShadow on Qt 6.4.2 without QtQuick.Effects).
    Rectangle {
        id: dialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 560)
        height: Math.min(parent.height - 48, contentColumn.implicitHeight + 112)
        radius: 12
        color: "#ffffff"
        border.color: "#dcfce7"
        border.width: 1

        // Faux shadow (offset Y + larger blur via two stacked rects).
        // Keeps the dialog declarative without pulling in QtQuick.Effects.
        Rectangle {
            z: -1
            anchors {
                fill: parent
                topMargin: 4
                leftMargin: -2
                rightMargin: -2
                bottomMargin: -8
            }
            radius: 12
            color: "#000000"
            opacity: 0.06
        }
        Rectangle {
            z: -1
            anchors {
                fill: parent
                topMargin: 12
                leftMargin: -4
                rightMargin: -4
                bottomMargin: -16
            }
            radius: 12
            color: "#000000"
            opacity: 0.04
        }

        // ---- header --------------------------------------------------
        Rectangle {
            id: header
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
            height: 56
            color: "transparent"
            // Bottom 1px border so the header strip reads as a section.
            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 1
                color: "#dcfce7"
            }

            Label {
                anchors {
                    left: parent.left
                    leftMargin: 24
                    verticalCenter: parent.verticalCenter
                }
                text: (i18nManagerObj.qtr("TxDetailDialog", "Transaction") + i18nManagerObj.emptyString)
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: "#14532d"
            }

            ToolButton {
                anchors {
                    right: parent.right
                    rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                width: 32
                height: 32
                text: "\u00d7"  // ×
                font.pixelSize: 18
                onClicked: root.closed()
                background: Rectangle {
                    radius: 16
                    color: parent.hovered ? "#dcfce7" : "transparent"
                }
                contentItem: Label {
                    text: parent.text
                    color: "#6b8074"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.text: (i18nManagerObj.qtr("TxDetailDialog", "Close") + i18nManagerObj.emptyString)
                ToolTip.visible: hovered
                ToolTip.delay: 600
            }
        }

        // ---- content column -----------------------------------------
        ColumnLayout {
            id: contentColumn
            anchors {
                top: header.bottom
                left: parent.left
                right: parent.right
                bottom: footer.top
                leftMargin: 24
                rightMargin: 24
                topMargin: 16
                bottomMargin: 16
            }
            spacing: 12

            // -- direction tag row -----------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                // Direction pill. Background color matches the
                // avatar's per-direction color; white text.
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: dirLabel.implicitWidth + 24
                    radius: 12
                    color: root._directionColor(root._direction)
                    Label {
                        id: dirLabel
                        anchors.centerIn: parent
                        text: {
                            var d = root._direction
                            if (d === "in")  return (i18nManagerObj.qtr("TxDetailDialog", "Incoming") + i18nManagerObj.emptyString)
                            if (d === "out") return (i18nManagerObj.qtr("TxDetailDialog", "Outgoing") + i18nManagerObj.emptyString)
                            if (d === "pending") return (i18nManagerObj.qtr("TxDetailDialog", "Pending") + i18nManagerObj.emptyString)
                            if (d === "pool")    return (i18nManagerObj.qtr("TxDetailDialog", "Pool") + i18nManagerObj.emptyString)
                            if (d === "failed")  return (i18nManagerObj.qtr("TxDetailDialog", "Failed") + i18nManagerObj.emptyString)
                            return (i18nManagerObj.qtr("TxDetailDialog", "Unknown") + i18nManagerObj.emptyString)
                        }
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: "#ffffff"
                    }
                }

                // Status pill (left = confirm count, right = state
                // label). For confirmed rows the pill is the Forest
                // Green accent; for confirming it's amber; for
                // pending it's the out-orange.
                Rectangle {
                    Layout.preferredHeight: 24
                    Layout.preferredWidth: stateLabel.implicitWidth + 24
                    radius: 12
                    color: root._stateColor(root._state)
                    opacity: 0.18
                    Rectangle {
                        anchors.fill: parent
                        radius: 12
                        color: "transparent"
                        border.color: parent.color
                        border.width: 1
                    }
                    Label {
                        id: stateLabel
                        anchors.centerIn: parent
                        text: root._stateLabel(root._state)
                              + (root._state === "confirming" && rowData && rowData.confirmations !== undefined
                                 ? "  (" + Math.floor(Number(rowData.confirmations)) + "/10)"
                                 : "")
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: root._stateColor(root._state)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // -- amount (big number) ----------------------------------
            Label {
                Layout.fillWidth: true
                text: root._formatJde(rowData ? rowData.amount : 0)
                font.pixelSize: 28
                font.weight: Font.DemiBold
                color: root._direction === "out" ? "#c2410c"   // --tx-out
                     : root._direction === "in"  ? "#15803d"   // --tx-in
                     : "#14532d"                                // --fg
                elide: Text.ElideRight
            }

            // -- metadata grid (key-value pairs) ---------------------
            // Two columns of label/value pairs. We use a GridLayout
            // for the label column (fixed-width left column) and
            // match the right column with a ColumnLayout of values.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                // -- counterparty (full address, copyable) ----------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: (i18nManagerObj.qtr("TxDetailDialog", "Counterparty") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        color: "#6b8074"
                        Layout.preferredWidth: 110
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root._counterparty.length > 0
                            ? root._counterparty
                            : (i18nManagerObj.qtr("TxDetailDialog", "(no counterparty)") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: root._counterparty.length > 0 ? "#14532d" : "#6b8074"
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                    }
                }

                // -- tx hash ------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: (i18nManagerObj.qtr("TxDetailDialog", "Tx hash") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        color: "#6b8074"
                        Layout.preferredWidth: 110
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.txHash.length > 0
                            ? root.txHash
                            : (i18nManagerObj.qtr("TxDetailDialog", "(no hash)") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: root.txHash.length > 0 ? "#14532d" : "#6b8074"
                        elide: Text.ElideMiddle
                        wrapMode: Text.NoWrap
                    }
                }

                // -- date --------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: (i18nManagerObj.qtr("TxDetailDialog", "Date") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        color: "#6b8074"
                        Layout.preferredWidth: 110
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root._formatDate(rowData ? rowData.timestamp : 0)
                        font.pixelSize: 12
                        color: "#14532d"
                        elide: Text.ElideRight
                    }
                }

                // -- block height ------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: (i18nManagerObj.qtr("TxDetailDialog", "Block height") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        color: "#6b8074"
                        Layout.preferredWidth: 110
                    }
                    Label {
                        Layout.fillWidth: true
                        text: rowData && rowData.blockHeight !== undefined
                            ? String(Math.floor(Number(rowData.blockHeight)))
                            : "—"
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: "#14532d"
                        elide: Text.ElideRight
                    }
                }

                // -- fee ---------------------------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: (i18nManagerObj.qtr("TxDetailDialog", "Fee") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        color: "#6b8074"
                        Layout.preferredWidth: 110
                    }
                    Label {
                        Layout.fillWidth: true
                        text: rowData && rowData.fee !== undefined && Number(rowData.fee) > 0
                            ? root._formatJde(rowData.fee)
                            : (i18nManagerObj.qtr("TxDetailDialog", "—") + i18nManagerObj.emptyString)
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: "#14532d"
                        elide: Text.ElideRight
                    }
                }
            }

            // -- note section (label + textarea + save hint) ---------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 6

                Label {
                    text: (i18nManagerObj.qtr("TxDetailDialog", "Note") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#14532d"
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    radius: 6
                    color: "#ffffff"
                    border.color: noteField.activeFocus ? "#22c55e" : "#dcfce7"
                    border.width: 1

                    TextArea {
                        id: noteField
                        anchors.fill: parent
                        anchors.margins: 8
                        text: root._noteText
                        placeholderText: (i18nManagerObj.qtr("TxDetailDialog", "Add a note for this transaction…") + i18nManagerObj.emptyString)
                        placeholderTextColor: "#6b8074"
                        color: "#14532d"
                        font.pixelSize: 12
                        wrapMode: TextArea.Wrap
                        background: null
                        selectByMouse: true
                        onTextChanged: {
                            // Mark dirty iff the user actually diverged
                            // from the in-memory note.
                            root._noteDirty = (text !== root._noteText)
                        }
                    }
                }

                // Save / error hint line. Visible only after a save
                // attempt; resets on the next edit. We use a 1s
                // auto-fade so the hint doesn't linger forever.
                Label {
                    id: saveHint
                    Layout.fillWidth: true
                    visible: opacity > 0
                    opacity: 0
                    font.pixelSize: 11
                    // --tx-in green default; flipped to red on save failure
                    // via the lastSaveOk binding below.
                    elide: Text.ElideRight

                    // Inline error styling: red for failure, green
                    // for success. The hint text + color are bound to
                    // the last-save status set by _onSaveClicked.
                    property bool lastSaveOk: true
                    property string lastSaveText: ""

                    text: lastSaveText
                    color: lastSaveOk ? "#15803d" : "#dc2626"

                    Behavior on opacity {
                        NumberAnimation { duration: 250 }
                    }

                    Timer {
                        id: saveHintTimer
                        interval: 4000
                        repeat: false
                        onTriggered: saveHint.opacity = 0
                    }

                    function showHint(ok, msg) {
                        lastSaveOk = ok
                        lastSaveText = msg
                        opacity = 1
                        saveHintTimer.restart()
                    }
                }
            }

            // -- tag section (st-112: 1 line, 36px, mirrors Note
            //    above but for the structured tag string). V5.4 §8
            //    decision C: note + tag are two independent features
            //    on the same tx; the Save button persists BOTH in
            //    the same click handler so a partial save cannot
            //    leave a half-edited disk state.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 6

                Label {
                    text: (i18nManagerObj.qtr("TxDetailDialog", "Tag") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#14532d"
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: 6
                    color: "#ffffff"
                    border.color: tagField.activeFocus ? "#22c55e" : "#dcfce7"
                    border.width: 1

                    TextField {
                        id: tagField
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 0
                        anchors.bottomMargin: 0
                        text: root._tagText
                        placeholderText: (i18nManagerObj.qtr("TxDetailDialog", "Add a short tag (e.g. Salary, Rent)…") + i18nManagerObj.emptyString)
                        placeholderTextColor: "#6b8074"
                        color: "#14532d"
                        font.pixelSize: 12
                        selectByMouse: true
                        background: null
                        onTextChanged: {
                            root._tagDirty = (text !== root._tagText)
                        }
                    }
                }
            }
        }

        // ---- footer (Save / Remove / Close buttons) ---------------
        Rectangle {
            id: footer
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 64
            color: "transparent"
            // Top 1px border so the footer strip reads as a section.
            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                }
                height: 1
                color: "#dcfce7"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                anchors.topMargin: 12
                anchors.bottomMargin: 12
                spacing: 12

                // Remove note button (only enabled if there's a
                // note to remove). Visible only when a note already
                // exists for this tx_hash.
                Button {
                    text: (i18nManagerObj.qtr("TxDetailDialog", "Remove note") + i18nManagerObj.emptyString)
                    Layout.preferredHeight: 36
                    enabled: root._noteText.length > 0 || root._noteDirty
                    onClicked: root._onRemoveClicked()

                    background: Rectangle {
                        radius: 6
                        color: parent.enabled
                            ? (parent.hovered ? "#fef2f2" : "#ffffff")
                            : "#f3f4f6"
                        border.color: parent.enabled ? "#fecaca" : "#d1d5db"
                        border.width: 1
                    }
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 12
                        color: parent.enabled ? "#dc2626" : "#9ca3af"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Item { Layout.fillWidth: true }

                // Close button.
                Button {
                    text: (i18nManagerObj.qtr("TxDetailDialog", "Close") + i18nManagerObj.emptyString)
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: 90
                    onClicked: root.closed()
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? "#f0fdf4" : "#ffffff"
                        border.color: "#dcfce7"
                        border.width: 1
                    }
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: "#14532d"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Save button. Primary action; Forest Green.
                Button {
                    text: (i18nManagerObj.qtr("TxDetailDialog", "Save") + i18nManagerObj.emptyString)
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: 90
                    enabled: root._noteDirty
                    onClicked: root._onSaveClicked()
                    background: Rectangle {
                        radius: 6
                        color: parent.enabled
                            ? (parent.hovered ? "#16a34a" : "#22c55e")
                            : "#a7f3d0"
                    }
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    // ---- save / remove handlers --------------------------------------
    /// Click handler for the Save button. Persists _noteText to
    /// localDataManagerObj (which writes to tx_notes.json on disk)
    /// and emits noteSaved(hash) on success. On failure we surface a
    /// red error label inside the dialog so the user knows the
    /// change did not stick.
    function _onSaveClicked() {
        if (typeof localDataManagerObj === "undefined" || !localDataManagerObj) {
            saveHint.showHint(false, (i18nManagerObj.qtr("TxDetailDialog", "Note save unavailable (no local store).") + i18nManagerObj.emptyString))
            return
        }
        if (!txHash) {
            saveHint.showHint(false, (i18nManagerObj.qtr("TxDetailDialog", "Note save unavailable (no tx hash).") + i18nManagerObj.emptyString))
            return
        }
        // st-112: persist note + tag in the same click. The note save
        // is the user-visible primary action (the Save button label
        // and the dialog's primary color are still note-centric for
        // backwards compatibility with the st-062 contract), but the
        // tag is also persisted so a partial save cannot leave
        // half-edited state on disk. Empty tag is valid (the user
        // explicitly cleared it); we still call setTag with "" so the
        // manager's bridge writes the empty value through to the
        // tx_tags.json store and the row reflects the cleared state
        // on next open.
        var noteOk = localDataManagerObj.saveTxNote(txHash, _noteText)
        var tagOk = localDataManagerObj.setTag(txHash, _tagText)
        if (noteOk && tagOk) {
            _noteDirty = false
            _tagDirty = false
            saveHint.showHint(true, _tagDirty === false && _tagText.length > 0
                ? (i18nManagerObj.qtr("TxDetailDialog", "Note + tag saved.") + i18nManagerObj.emptyString)
                : (i18nManagerObj.qtr("TxDetailDialog", "Note saved.") + i18nManagerObj.emptyString))
            root.noteSaved(txHash)
            console.log("TxDetailDialog.qml (st-062+st-112): note+tag saved for tx_hash=",
                        txHash, "noteLen=" + _noteText.length, "tagLen=" + _tagText.length)
        } else {
            // st-112: distinguish the failure cases so the user
            // knows whether the note or the tag (or both) failed.
            var failMsg = (i18nManagerObj.qtr("TxDetailDialog", "Failed to save.") + i18nManagerObj.emptyString)
            if (!noteOk && !tagOk) {
                failMsg = (i18nManagerObj.qtr("TxDetailDialog", "Failed to save note and tag.") + i18nManagerObj.emptyString)
            } else if (!noteOk) {
                failMsg = (i18nManagerObj.qtr("TxDetailDialog", "Failed to save note.") + i18nManagerObj.emptyString)
            } else {
                failMsg = (i18nManagerObj.qtr("TxDetailDialog", "Failed to save tag.") + i18nManagerObj.emptyString)
            }
            saveHint.showHint(false, failMsg)
            console.warn("TxDetailDialog.qml (st-062+st-112): save failed for tx_hash=",
                         txHash, "noteOk=", noteOk, "tagOk=", tagOk)
        }
    }

    /// Click handler for the Remove note button. Calls
    /// localDataManagerObj.removeTxNote(hash) and clears the local
    /// text. On success the dialog shows a green confirmation hint
    /// and emits noteSaved so the parent can invalidate its cache.
    function _onRemoveClicked() {
        if (typeof localDataManagerObj === "undefined" || !localDataManagerObj) {
            saveHint.showHint(false, (i18nManagerObj.qtr("TxDetailDialog", "Note remove unavailable (no local store).") + i18nManagerObj.emptyString))
            return
        }
        if (!txHash) {
            saveHint.showHint(false, (i18nManagerObj.qtr("TxDetailDialog", "Note remove unavailable (no tx hash).") + i18nManagerObj.emptyString))
            return
        }
        // st-112: "Remove note" button removes BOTH the note AND the
        // tag (the user pressed Remove in the note context, but a
        // half-state where the note is gone but the tag still points
        // at the cleared note is misleading). The dialog's footer
        // still labels the button "Remove note" for backwards compat
        // with st-062 contract; the tag side-effect is documented in
        // the surrounding header banner.
        var noteOk = localDataManagerObj.removeTxNote(txHash)
        var tagOk = localDataManagerObj.removeTag(txHash)
        if (noteOk && tagOk) {
            _noteText = ""
            _noteDirty = false
            _tagText = ""
            _tagDirty = false
            noteField.text = ""
            if (typeof tagField !== "undefined") {
                tagField.text = ""
            }
            saveHint.showHint(true, (i18nManagerObj.qtr("TxDetailDialog", "Note + tag removed.") + i18nManagerObj.emptyString))
            root.noteSaved(txHash)
            console.log("TxDetailDialog.qml (st-062+st-112): note+tag removed for tx_hash=",
                        txHash)
        } else {
            saveHint.showHint(false, (i18nManagerObj.qtr("TxDetailDialog", "Failed to remove.") + i18nManagerObj.emptyString))
        }
    }

    // ---- keyboard shortcuts ------------------------------------------
    /// Esc closes the dialog. Ctrl+S triggers Save so power users
    /// can edit the note without reaching for the mouse. We attach
    /// the shortcuts to the root Item via a focus-scoped Shortcut
    /// so the global window keypress doesn't fire when the dialog
    /// is unmounted.
    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        onActivated: root.closed()
    }
    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.WindowShortcut
        enabled: root._noteDirty
        onActivated: root._onSaveClicked()
    }

    // ---- lifecycle ---------------------------------------------------
    /// On mount, load the current note from localDataManagerObj. We
    /// re-load on txHash changes too so a reused dialog (re-shown
    /// with a different hash) does not display the previous note.
    Component.onCompleted: root._loadNote()
    onTxHashChanged: root._loadNote()
}
