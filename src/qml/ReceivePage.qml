// =============================================================================
// ReceivePage.qml (st-024 + st-064) - Receive page with QR + amount/label form
//
// V5.4.14 receive flow: a dedicated "Receive" page where the user
// picks a subaddress, optionally enters an amount + label, and
// either (a) copies the resulting URI to the clipboard, or (b) scans
// the QR. The QR is rendered by QrCodeImage.qml.
//
// URI is built by UriHandler.makeUri() (st-020) so the page is a
// thin wrapper over the wallet-rpc contract. If wallet-rpc is
// unavailable we fall back to UriHandler.parseUriLocally() style
// local assembly so the user at least gets a copy-able text URI.
//
// V5.4.7 compliance: this page only generates URIs and renders them.
// It never touches keys, seed, .keys files, or wallet-rpc's
// encrypt_payload / decrypt_payload endpoints.
//
// st-064 (sidebar upgrade):
//   * Root is an Item (unchanged from st-024; was always a child of
//     the StackView mounted by main.qml).
//   * Adds a "Back to Dashboard" button in a Forest Green top bar
//     that emits backClicked(). main.qml routes that into
//     mainStack.pop() so the user returns to whatever page was
//     below (normally Dashboard, the page that hosts the
//     receiveClicked trigger).
//   * main.qml now StackView.push(receivePage) instead of
//     mainStack.replace(receivePage) so pop() has something to
//     pop. The Dashboard below stays mounted so the transition is
//     a slide-in place rather than a page-swap.
//
// V5.4.9 compliance: every visible string is qsTr() wrapped.
// V5.4.15 compliance: single Forest Green theme baked in (#16a34a).
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet UI.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "ReceivePage"

    // a11y (st-095): Receive page is a landmark Pane; child form widgets
    // (subaddress picker, amount/payment-id/recipient/description fields,
    // QR + Copy) carry their own Accessible.* roles below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Receive") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Generate a receive URI and QR code") + i18nManagerObj.emptyString)

    // a11y v2 (st-100): forward Tab chain. Pressing Tab from the
    // page-level landmark drops focus onto the first interactive
    // affordance so a keyboard-only user lands somewhere actionable.
    // The chain itself (subCombo -> refreshButton -> amountField ->
    // paymentIdField -> recipientNameField -> descriptionField ->
    // copyButton -> subCombo) is wired on each focusable item below
    // using KeyNavigation.tab / KeyNavigation.backtab, mirroring the
    // SendPage.qml pattern (no KeyNavigation.forward).
    // st-168 (2026-07-19): Tab chain entry updated to subCombo
    // after the in-page backButton was removed (Send-side st-167);
    // the Sidebar provides page navigation so the in-page back
    // affordance is now redundant.
    KeyNavigation.tab: subCombo

    // ----- public API ----------------------------------------------------
    /// Logical page name (matches the Dashboard's receiveClicked routing
    /// target in main.qml). Surfaced for static-source contract checks
    /// (same pattern as AddressBookPage.qml st-063 pageName).
    readonly property string pageName: "Receive"

    /// Emitted when the user clicks the top-bar back button.
    /// main.qml wires this to mainStack.pop() so the StackView reveals
    /// the page below (normally Dashboard).
    signal backClicked()

    // ----- context lookups -----------------------------------------------
    readonly property var uriHandler: typeof uriHandlerObj !== "undefined"
        ? uriHandlerObj : null
    readonly property var subaddressManager: typeof subaddressManagerObj !== "undefined"
        ? subaddressManagerObj : null
    readonly property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null

    // ----- form state ----------------------------------------------------
    /// 0 = no address selected. 1+ = (account, address) indices.
    property int selectedAccountIndex: 0
    property int selectedAddressIndex: 0

    /// Input fields for makeUri.
    property string amountText: ""           // user-typed atomic units as string
    property string paymentId: ""
    property string recipientName: ""
    property string txDescription: ""

    /// Computed URI. Built locally (no RPC) so the UI is responsive
    /// even before wallet-rpc is online; walletManager.makeUri()
    /// would be the canonical source once the daemon is reachable.
    readonly property string generatedUri: {
        if (!subaddressManager) return "";
        const addr = subaddressManager.addressFor
            ? subaddressManager.addressFor(selectedAccountIndex, selectedAddressIndex)
            : "";
        if (!addr) return "";
        let uri = "judecoin:" + addr;
        const parts = [];
        if (paymentId.length > 0)        parts.push("tx_payment_id=" + encodeURIComponent(paymentId));
        if (amountText.length > 0)       parts.push("amount=" + encodeURIComponent(amountText));
        if (recipientName.length > 0)    parts.push("recipient_name=" + encodeURIComponent(recipientName));
        if (txDescription.length > 0)    parts.push("tx_description=" + encodeURIComponent(txDescription));
        if (parts.length > 0) uri += "?" + parts.join("&");
        return uri;
    }

    /// If we have wallet-rpc reachable we still want its canonical
    /// canonical URI (it does its own URL-encoding). Poll lazily.
    property string rpcCanonicalUri: ""

    // ----- UI ------------------------------------------------------------
    // ---- page background (st-166, 2026-07-19) ---------------------
    // Mirrors Dashboard.qml pageBackground (st-161) and SendPage.qml
    // pageBackground (st-165). Horizontal gradient: light-green
    // (#f0fdf4) on the LEFT edge, transitions to pure WHITE by 35%
    // from the left, then stays white so the right ~65% of the page
    // reads as a clean white surface. Anchored to the root Item,
    // z: -1 so the ColumnLayout content (top bar + form + QR) sits
    // on top. The Forest Green top bar above (Layout.preferredHeight:
    // 56, color #16a34a) is fully opaque so it covers the gradient in
    // its own row; the gradient is visible below the top bar.
    Rectangle {
        id: pageBackground
        objectName: "ReceivePageBackground"
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#f0fdf4" }   // left edge
            GradientStop { position: 0.35; color: "#ffffff" }  // fast -> white
            GradientStop { position: 1.0; color: "#ffffff" }   // stays white
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 20

        // -----------------------------------------------------------------
        // TopBar (st-168, 2026-07-19): matches SendPage.qml topBar
        // (st-167). Just the page title "Receive" — no back button, no
        // Forest Green rectangle header. Navigation between pages is
        // handled by the Sidebar (always visible); the in-page back
        // affordance was removed for full visual parity with SendPage.
        // -----------------------------------------------------------------
        RowLayout {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 12

            Label {
                id: pageTitle
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("ReceivePage", "Receive") + i18nManagerObj.emptyString)
                color: "#166534"                       // --external (per design_specs §2)
                font.pixelSize: 18
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
        }

        // -----------------------------------------------------------------
        // Subaddress picker (Label + ComboBox + Refresh button).
        // Same per-field ColumnLayout pattern as SendPage's "From" field.
        // -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: (i18nManagerObj.qtr("ReceivePage", "Subaddress") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#14532d"                       // --fg per V5.4.15
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ComboBox {
                    id: subCombo
                    objectName: "ReceivePageSubCombo"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    model: subaddressManager && subaddressManager.entries
                        ? subaddressManager.entries : []
                    textRole: "label"
                    valueRole: "address_index"
                    // a11y (st-095): expose the subaddress picker so AT
                    // announces it as a combo box of wallet addresses.
                    Accessible.role: Accessible.ComboBox
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Subaddress") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Choose the subaddress that will receive the payment") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): subCombo -> refreshButton (forward),
                    // root (backward, the page-level landmark).
                    KeyNavigation.tab: refreshButton
                    KeyNavigation.backtab: root
                    activeFocusOnTab: true
                    onActivated: {
                        if (currentValue !== undefined) {
                            selectedAddressIndex = currentValue;
                        }
                    }
                    // Custom contentItem (Forest Green theme, matches
                    // SendPage's ComboBox style: 13px, #14532d, padding).
                    contentItem: Label {
                        text: subCombo.model[subCombo.currentIndex]
                            ? subCombo.model[subCombo.currentIndex].label
                            : ""
                        color: "#14532d"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 12
                        rightPadding: 24
                        elide: Text.ElideRight
                    }
                }

                Button {
                    id: refreshButton
                    objectName: "ReceivePageRefreshButton"
                    text: (i18nManagerObj.qtr("ReceivePage", "Refresh") + i18nManagerObj.emptyString)
                    // a11y (st-095): refresh subaddress list from the
                    // wallet-rpc bridge; name describes the side effect.
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Refresh subaddress list") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Reload the subaddress list from the wallet daemon") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): refreshButton -> amountField
                    // (forward), subCombo -> refreshButton (backward).
                    KeyNavigation.tab: amountField
                    KeyNavigation.backtab: subCombo
                    activeFocusOnTab: true
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: 100
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    // Forest Green primary button (matches SendPage's
                    // PasteURI / Submit button palette).
                    background: Rectangle {
                        radius: 6
                        color: refreshButton.pressed
                            ? "#16a34a"                  // --storage, pressed darker
                            : (refreshButton.hovered
                                ? "#4ade80"              // --accent-light, hover
                                : "#22c55e")             // --accent, idle
                        border.width: 0
                    }
                    contentItem: Label {
                        text: refreshButton.text
                        font: refreshButton.font
                        color: "#052e16"               // --text-on-accent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: if (subaddressManager) subaddressManager.refresh()
                }
            }
        }

        // -----------------------------------------------------------------
        // Optional request details (flat section, replaces the old
        // GroupBox wrapper). Section title uses the same Label style as
        // SendPage's per-field labels (12px DemiBold, #14532d) so the
        // visual rhythm matches the From / To / Amount / Priority
        // fields on SendPage.
        // -----------------------------------------------------------------
        Label {
            text: (i18nManagerObj.qtr("ReceivePage", "Optional request details") + i18nManagerObj.emptyString)
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: "#14532d"
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            // Amount field (replaces the GroupBox GridLayout entry).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: (i18nManagerObj.qtr("ReceivePage", "Amount") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#14532d"
                }

                TextField {
                    id: amountField
                    objectName: "ReceivePageAmountField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: (i18nManagerObj.qtr("ReceivePage", "atomic units (optional)") + i18nManagerObj.emptyString)
                    font.pixelSize: 13
                    color: "#14532d"
                    selectByMouse: true
                    text: root.amountText
                    // a11y (st-095): editable text for the requested
                    // amount; name comes from the label so screen readers
                    // announce "Amount, edit, ...".
                    Accessible.role: Accessible.EditableText
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Amount") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Request amount in atomic units (optional)") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): amountField -> paymentIdField (forward),
                    // refreshButton -> amountField (backward).
                    KeyNavigation.tab: paymentIdField
                    KeyNavigation.backtab: refreshButton
                    activeFocusOnTab: true
                    onTextChanged: root.amountText = text
                    background: Rectangle {
                        radius: 6
                        color: "#ffffff"
                        border.color: amountField.activeFocus
                            ? "#22c55e"                 // --accent when focused
                            : "#d1fae5"                 // --neutral border
                        border.width: 1
                    }
                }
            }

            // Payment ID field.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: (i18nManagerObj.qtr("ReceivePage", "Payment ID") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#14532d"
                }

                TextField {
                    id: paymentIdField
                    objectName: "ReceivePagePaymentIdField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: (i18nManagerObj.qtr("ReceivePage", "64-hex (optional)") + i18nManagerObj.emptyString)
                    font.pixelSize: 13
                    color: "#14532d"
                    selectByMouse: true
                    text: root.paymentId
                    Accessible.role: Accessible.EditableText
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Payment ID") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "64-character hex payment identifier (optional)") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): paymentIdField -> recipientNameField
                    // (forward), amountField -> paymentIdField (backward).
                    KeyNavigation.tab: recipientNameField
                    KeyNavigation.backtab: amountField
                    activeFocusOnTab: true
                    onTextChanged: root.paymentId = text
                    background: Rectangle {
                        radius: 6
                        color: "#ffffff"
                        border.color: paymentIdField.activeFocus
                            ? "#22c55e"
                            : "#d1fae5"
                        border.width: 1
                    }
                }
            }

            // Recipient name field.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: (i18nManagerObj.qtr("ReceivePage", "Recipient name") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#14532d"
                }

                TextField {
                    id: recipientNameField
                    objectName: "ReceivePageRecipientNameField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: (i18nManagerObj.qtr("ReceivePage", "(optional)") + i18nManagerObj.emptyString)
                    font.pixelSize: 13
                    color: "#14532d"
                    selectByMouse: true
                    text: root.recipientName
                    Accessible.role: Accessible.EditableText
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Recipient name") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Recipient display name for the payment (optional)") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): recipientNameField -> descriptionField
                    // (forward), paymentIdField -> recipientNameField (backward).
                    KeyNavigation.tab: descriptionField
                    KeyNavigation.backtab: paymentIdField
                    activeFocusOnTab: true
                    onTextChanged: root.recipientName = text
                    background: Rectangle {
                        radius: 6
                        color: "#ffffff"
                        border.color: recipientNameField.activeFocus
                            ? "#22c55e"
                            : "#d1fae5"
                        border.width: 1
                    }
                }
            }

            // Description field.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: (i18nManagerObj.qtr("ReceivePage", "Description") + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "#14532d"
                }

                TextField {
                    id: descriptionField
                    objectName: "ReceivePageDescriptionField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: (i18nManagerObj.qtr("ReceivePage", "(optional)") + i18nManagerObj.emptyString)
                    font.pixelSize: 13
                    color: "#14532d"
                    selectByMouse: true
                    text: root.txDescription
                    Accessible.role: Accessible.EditableText
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Description") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Free-form description for the payment (optional)") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): descriptionField -> copyButton (forward),
                    // recipientNameField -> descriptionField (backward).
                    KeyNavigation.tab: copyButton
                    KeyNavigation.backtab: recipientNameField
                    activeFocusOnTab: true
                    onTextChanged: root.txDescription = text
                    background: Rectangle {
                        radius: 6
                        color: "#ffffff"
                        border.color: descriptionField.activeFocus
                            ? "#22c55e"
                            : "#d1fae5"
                        border.width: 1
                    }
                }
            }
        }

        // -----------------------------------------------------------------
        // QR section (flat, replaces the old GroupBox wrapper). Section
        // title uses the same Label style as form fields above for a
        // consistent visual rhythm with SendPage.
        // -----------------------------------------------------------------
        Label {
            text: (i18nManagerObj.qtr("ReceivePage", "QR") + i18nManagerObj.emptyString)
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: "#14532d"
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            QrCodeImage {
                Layout.alignment: Qt.AlignHCenter
                source: root.generatedUri
                pixelSize: 256
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: root.generatedUri
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: "#14532d"
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                }

                Button {
                    id: copyButton
                    objectName: "ReceivePageCopyButton"
                    text: (i18nManagerObj.qtr("ReceivePage", "Copy") + i18nManagerObj.emptyString)
                    // a11y (st-095): copy affordance; name describes
                    // exactly what gets copied (the payment URI).
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("ReceivePage", "Copy payment URI") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("ReceivePage", "Copy the payment URI to the clipboard or console") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): copyButton -> subCombo (cycle
                    // forward so the chain wraps to the first form
                    // control), descriptionField -> copyButton (backward).
                    KeyNavigation.tab: subCombo
                    KeyNavigation.backtab: descriptionField
                    activeFocusOnTab: true
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: 100
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    // Forest Green primary button (same palette as
                    // SendPage's PasteURI / Submit).
                    background: Rectangle {
                        radius: 6
                        color: copyButton.pressed
                            ? "#16a34a"                  // --storage, pressed darker
                            : (copyButton.hovered
                                ? "#4ade80"              // --accent-light, hover
                                : "#22c55e")             // --accent, idle
                        border.width: 0
                    }
                    contentItem: Label {
                        text: copyButton.text
                        font: copyButton.font
                        color: "#052e16"               // --text-on-accent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: {
                        if (typeof clipboardHelper !== "undefined"
                            && root.generatedUri.length > 0) {
                            clipboardHelper.copyToClipboard(root.generatedUri);
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }               // bottom spacer (pushes content to top)
    }
}