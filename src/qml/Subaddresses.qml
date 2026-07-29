// =============================================================================
// Subaddresses.qml (st-024 + st-065) - subaddress list with QR per row
//
// V5.4.14 receive flow: the Subaddresses page (the place users go to
// pick a subaddress or grab the primary address for receive) shows a
// per-row [QR] button. Clicking it pops a small QR preview dialog
// rendered by QrCodeImage.qml. This component is the page itself; the
// modal/dialog behaviour is owned by the parent (main.qml or a StackView).
//
// Data source: subaddressManagerObj context property (set in main.cpp).
// Each row carries account_index / address_index / address / label.
// We render rows in a ListView; only addresses that are non-empty get
// the [QR] button (a freshly-initialized wallet with no addresses
// shows a "No subaddresses yet" placeholder).
//
// st-065 (sidebar upgrade):
//   * Root stays an Item (was already an Item in st-024; the spec note
//     "ApplicationWindow -> Item" was for clarity vs. older revisions).
//   * Adds a "Back to Dashboard" button in a Forest Green top bar
//     that emits backClicked(). main.qml routes that into
//     mainStack.pop() so the user returns to whatever page was
//     below (normally Dashboard, the page that hosts the
//     subaddressesClicked trigger).
//   * main.qml now StackView.push(subaddressPage) instead of
//     mainStack.replace(subaddressPage) so pop() has something to
//     pop. The parent page (Dashboard or wherever the user came
//     from) stays mounted so the transition is a slide-in place
//     rather than a page-swap. Same pattern as ReceivePage st-064.
//   * Subaddresses also gains objectName: "Subaddresses" +
//     pageName: "Subaddresses" for static-source contract checks
//     (same pattern as AddressBookPage st-063 + ReceivePage st-064).
//
// V5.4.7 compliance: this view never sees private keys. The address
// string is all we display and feed into QRGenerator via the
// image://qr/ provider.
// V5.4.9 compliance: every visible string is qsTr() wrapped.
// V5.4.15 compliance: single Forest Green theme baked in (#16a34a).
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet UI.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "Subaddresses"

    // ----- public API ----------------------------------------------------
    /// Logical page name (matches the Sidebar tab label and the
    /// Dashboard subaddressesClicked routing target in main.qml).
    /// Surfaced for static-source contract checks (same pattern as
    /// AddressBookPage st-063 + ReceivePage st-064 pageName).
    readonly property string pageName: "Subaddresses"

    /// Emitted when the user clicks the top-bar back button.
    /// main.qml wires this to mainStack.pop() so the StackView reveals
    /// the page below (normally Dashboard, the page whose
    /// subaddressesClicked signal pushed us here).
    signal backClicked()

    // ----- bindings -------------------------------------------------------
    /// Source manager (set as a QML context property by main.cpp).
    /// Lazy lookup via Qt.binding so we don't crash if it's not set
    /// during QML preview tooling.
    readonly property var subaddressManager: typeof subaddressManagerObj !== "undefined"
        ? subaddressManagerObj : null

    /// Reactive model: re-evaluated when subaddressesUpdated fires
    /// (FIX-G14 2026-07-18: root.entries was a static JS var set once;
    /// replaced with direct C++ binding + Connections signal handler).
    property var entriesModel: subaddressManager ? subaddressManager.entries : []

    // ---- balance formatting (st-174) ---------------------------------
    // Same logic as BalanceCard.qml _formatBalance: atomic / 1000
    // with thousand separators and trailing-zero strip.
    // Defined on root so ListView delegates can access it.
    function _fmt(atomic) {
        var v = atomic / 1000.0
        if (v === 0) return "0"
        var s = v.toLocaleString(Qt.locale(), 'f', 5)
        if (s.indexOf('.') < 0) return s
        var trimmed = s.replace(/0+$/, '')
        if (trimmed.charAt(trimmed.length - 1) === '.') {
            trimmed = trimmed.substring(0, trimmed.length - 1)
        }
        return trimmed
    }

    // ----- UI -------------------------------------------------------------
    // ---- page background (st-169, 2026-07-19) ---------------------
    // Mirrors Dashboard.qml (st-161), SendPage.qml (st-165), and
    // ReceivePage.qml (st-166). Horizontal gradient: light-green
    // (#f0fdf4) on the LEFT edge, transitions to pure WHITE by 35%
    // from the left, then stays white so the right ~65% of the page
    // reads as a clean white surface. Anchored to the root Item,
    // z: -1 so the ColumnLayout content sits on top.
    Rectangle {
        id: pageBackground
        objectName: "SubaddressesPageBackground"
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#f0fdf4" }   // left edge
            GradientStop { position: 0.35; color: "#ffffff" }  // fast -> white
            GradientStop { position: 1.0; color: "#ffffff" }   // stays white
        }
    }

    // FIX-G14 (2026-07-18): Listen for subaddressesUpdated signal from C++
    // SubaddressManager so the ListView model refreshes when entries change
    // (on initial refresh, createSubaddress, or manual refresh). Without
    // this, the QML property binding on entriesModel is static (evaluated
    // once) because entries() is a method, not a Q_PROPERTY with NOTIFY.
    Connections {
        target: root.subaddressManager
        function onSubaddressesUpdated(newEntries) {
            root.entriesModel = newEntries
        }
    }

    ColumnLayout {
        id: balanceColumn
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 20

        // -----------------------------------------------------------------
        // TopBar (st-169, 2026-07-19): matches SendPage.qml topBar
        // (st-167). Page title "Subaddresses" on the left, count badge on
        // the right. No Forest Green rectangle header and no in-page
        // back button — the Sidebar provides page navigation, same
        // rationale as SendPage / ReceivePage.
        // -----------------------------------------------------------------
        RowLayout {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 12

            Label {
                id: pageTitle
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("Subaddresses", "Subaddresses") + i18nManagerObj.emptyString)
                color: "#166534"                       // --external (per design_specs §2)
                font.pixelSize: 18
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }

            Label {
                text: (i18nManagerObj.qtr("Subaddresses", "%1 subaddresses").arg(listView.count) + i18nManagerObj.emptyString)
                    color: "#14532d"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }

        // -----------------------------------------------------------------
        // Hint text (describes the page action).
        // -----------------------------------------------------------------
        Label {
            text: (i18nManagerObj.qtr("Subaddresses", "Pick a subaddress to receive funds. Tap [QR] to show the QR code for that address.") + i18nManagerObj.emptyString)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 12
            color: "#14532d"
        }

        // -----------------------------------------------------------------
        // Subaddress list (per-row pattern, replaces the old GroupBox).
        // Each row is a card-style ItemDelegate with a tinted background
        // for primary rows (FIX-G17) and a Forest Green "QR" button on
        // the right. Same column spacing / font palette as SendPage.
        // -----------------------------------------------------------------
        ListView {
            id: listView
            objectName: "SubaddressesListView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.entriesModel
            spacing: 6

            // FIX-G14 (2026-07-18): refresh on mount so existing
            // subaddresses (from prior wallet sessions) appear.
            Component.onCompleted: {
                if (subaddressManager) subaddressManager.refresh()
            }

            // Empty-state placeholder.
            Label {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: (i18nManagerObj.qtr("Subaddresses", "No subaddresses yet. Refresh from the wallet to load addresses.") + i18nManagerObj.emptyString)
                color: "#6b8074"
                font.pixelSize: 12
            }

            delegate: ItemDelegate {
                width: listView.width
                height: 96  // st-174: 64→96 to fit balance row

                required property var modelData
                required property int index

                readonly property string rowAddress:
                    modelData && modelData.address ? modelData.address : ""
                readonly property string rowLabel:
                    modelData && modelData.label  ? modelData.label  : ""
                readonly property bool rowIsPrimary:
                    modelData && modelData.isPrimary === true
                readonly property int rowBalance:
                    modelData && modelData.balance !== undefined ? modelData.balance : 0
                readonly property int rowUnlocked:
                    modelData && modelData.unlocked_balance !== undefined ? modelData.unlocked_balance : 0

                // FIX-G17 (2026-07-18): subtle background tint for primary
                // rows + Forest Green border for non-primary rows so the
                // list reads as a column of cards matching SendPage's
                // TextField / ComboBox chrome.
                background: Rectangle {
                    radius: 6
                    color: rowIsPrimary ? "#dcfce7" : "#ffffff"
                    border.color: "#d1fae5"           // --neutral border (matches TextField idle)
                    border.width: 1
                }

                contentItem: RowLayout {
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: rowIsPrimary
                                ? (rowLabel && rowLabel !== "Primary"
                                    ? (i18nManagerObj.qtr("Subaddresses", "Primary — %1").arg(rowLabel) + i18nManagerObj.emptyString): (i18nManagerObj.qtr("Subaddresses", "Primary") + i18nManagerObj.emptyString))
                                : (rowLabel
                                    ? (i18nManagerObj.qtr("Subaddresses", "Subaddress #%1 — %2")
                                        .arg(modelData.address_index).arg(rowLabel) + i18nManagerObj.emptyString): (i18nManagerObj.qtr("Subaddresses", "Subaddress #%1")
                                        .arg(modelData.address_index) + i18nManagerObj.emptyString))
                            font.pixelSize: rowIsPrimary ? 14 : 13
                            font.weight: rowIsPrimary ? Font.DemiBold : Font.Normal
                            color: "#14532d"           // --fg per V5.4.15
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: rowAddress
                            font.family: "monospace"
                            font.pixelSize: 11
                            color: "#14532d"
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        // st-174: per-subaddress balance row (total + available)
                        Label {
                            text: {
                                const bal  = root._fmt(rowBalance)
                                const unlk = root._fmt(rowUnlocked)
                                return (i18nManagerObj.qtr("Subaddresses", "Balance: %1 JUDE  ·  Available: %2 JUDE")
                                    .arg(bal).arg(unlk) + i18nManagerObj.emptyString)}
                            font.pixelSize: 11
                            color: "#3b82f6"  // blue tone to differentiate from address text
                            visible: true
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        id: qrButton
                        text: (i18nManagerObj.qtr("Subaddresses", "QR") + i18nManagerObj.emptyString)
                        enabled: rowAddress.length > 0
                        Layout.preferredHeight: 32
                        Layout.preferredWidth: 56
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        background: Rectangle {
                            radius: 6
                            color: qrButton.pressed
                                ? "#16a34a"
                                : (qrButton.hovered
                                    ? "#4ade80"
                                    : "#22c55e")
                            border.width: 0
                        }
                        contentItem: Label {
                            text: qrButton.text
                            font: qrButton.font
                            color: "#052e16"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            qrDialog.target = rowAddress;
                            qrDialog.open();
                        }
                    }

                    Button {
                        id: copyBtn
                        text: (i18nManagerObj.qtr("Subaddresses", "Copy") + i18nManagerObj.emptyString)
                        enabled: rowAddress.length > 0
                        Layout.preferredHeight: 32
                        Layout.preferredWidth: 68
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        background: Rectangle {
                            radius: 6
                            color: copyBtn.pressed
                                ? "#16a34a"
                                : (copyBtn.hovered
                                    ? "#4ade80"
                                    : "#22c55e")
                            border.width: 0
                        }
                        contentItem: Label {
                            text: copyBtn.text
                            font: copyBtn.font
                            color: "#052e16"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            if (typeof clipboardHelper !== "undefined"
                                && rowAddress.length > 0) {
                                clipboardHelper.copyToClipboard(rowAddress);
                            }
                        }
                    }
                }
            }
        }

        // -----------------------------------------------------------------
        // Bottom action row (Refresh + Create Subaddress). Forest Green
        // primary buttons matching SendPage's chrome.
        // -----------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                id: refreshButton
                objectName: "SubaddressesRefreshButton"
                text: (i18nManagerObj.qtr("Subaddresses", "Refresh from wallet") + i18nManagerObj.emptyString)
                enabled: subaddressManager !== null
                Layout.preferredHeight: 36
                Layout.preferredWidth: 180
                font.pixelSize: 12
                font.weight: Font.DemiBold
                background: Rectangle {
                    radius: 6
                    color: refreshButton.pressed
                        ? "#16a34a"
                        : (refreshButton.hovered
                            ? "#4ade80"
                            : "#22c55e")
                    border.width: 0
                }
                contentItem: Label {
                    text: refreshButton.text
                    font: refreshButton.font
                    color: "#052e16"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: if (subaddressManager) subaddressManager.refresh()
            }

            // FIX-G13 (2026-07-18): "Create Subaddress" button.
            // Calls SubaddressManager.createSubaddress() which invokes
            // wallet-rpc get_address with the next available index.
            // On success the new entry is appended to entries() and
            // subaddressesUpdated fires -> ListView re-renders with the
            // new row. On failure errorOccurred surfaces the message.
            Button {
                id: createButton
                objectName: "SubaddressesCreateButton"
                text: (i18nManagerObj.qtr("Subaddresses", "Create Subaddress") + i18nManagerObj.emptyString)
                enabled: subaddressManager !== null
                Layout.preferredHeight: 36
                Layout.preferredWidth: 180
                font.pixelSize: 12
                font.weight: Font.DemiBold
                background: Rectangle {
                    radius: 6
                    color: createButton.pressed
                        ? "#16a34a"
                        : (createButton.hovered
                            ? "#4ade80"
                            : "#22c55e")
                    border.width: 0
                }
                contentItem: Label {
                    text: createButton.text
                    font: createButton.font
                    color: "#052e16"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    if (!subaddressManager) return
                    const newEntry = subaddressManager.createSubaddress(0, "")
                    if (newEntry && !newEntry.address) {
                        console.log("Subaddresses.qml (FIX-G13): createSubaddress returned empty",
                                    subaddressManager.lastError)
                    }
                }
            }

            Item { Layout.fillWidth: true }               // spacer (pushes buttons to the left)
        }
    }

    // ----- QR preview dialog ---------------------------------------------
    // Modal overlay; not part of the page chrome so its Qt Quick Controls
    // default styling stays untouched. (The Dialog uses standardButtons:
    // Dialog.Close and inherits the Forest Green theme via the application
    // QSS palette.)
    Dialog {
        id: qrDialog
        objectName: "SubaddressesQrDialog"
        title: (i18nManagerObj.qtr("Subaddresses", "Subaddress QR") + i18nManagerObj.emptyString)
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Close
        property string target: ""

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                text: qrDialog.target
                font.family: "monospace"
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
                Layout.maximumWidth: 320
            }
            QrCodeImage {
                source: qrDialog.target
                pixelSize: 256
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: (i18nManagerObj.qtr("Subaddresses", "Scan with a Judecoin-compatible wallet to send funds to this subaddress.") + i18nManagerObj.emptyString)
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 320
            }
        }
    }
}
