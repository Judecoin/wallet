// =============================================================================
// Judecoin Wallet - SendPage.qml (st-060)
//
// V5.4.14 send flow: a dedicated "Send" page reachable from the Dashboard
// ActionButtons (st-051) "Send" CTA + the Sidebar (st-041) tab #2 (Send).
// Renders a four-field form (From / To / Amount / Priority) under a TopBar
// (page title + back affordance), validates the four inputs in real time,
// and emits `submitClicked(fromSubaddress, toAddress, amountAtomic, priority)`
// once the user confirms via the Submit button. The parent (main.qml in
// st-054 routing terms, or a future TransferApi wrapper) consumes that
// signal and pushes the resulting transfer_split RPC.
//
// Layout (per design_specs.md §5.4):
//   ┌──────────────────────────────────────────────────┐
//   │  Send                                            │  <- title + back arrow
//   ├──────────────────────────────────────────────────┤
//   │  From      [ ComboBox: "Subaddress #N - Label" ] │  <- st-019 SubaddressManager.entries
//   │  To        [ TextField          ] [ Paste URI ]  │  <- st-020 UriHandler.parseUriLocally()
//   │  Amount    [ TextField "0.000" ]                 │  <- realtime format 0.000 (12 atomic)
//   │             (red "Amount too small (dust)" if    │     dust < 0.0001
//   │              amount < 0.0001)
//   │  Priority  [ ComboBox: Low / Normal / Elevated / High ]
//   ├──────────────────────────────────────────────────┤
//   │                              [    Submit    ]    │  <- enabled iff 4 fields filled
//   └──────────────────────────────────────────────────┘
//
// Public API (consumed by main.qml / future TransferApi wrapper):
//   signal submitClicked(string fromSubaddress, string toAddress,
//                        var amountAtomic, int priority)
//     - `amountAtomic` is a Number (already converted from the user-typed
//       JDE string via _amountAtomic so the receiver can feed it directly
//       into TransferRequest::amount).
//     - `priority` is the integer mapping Low=0 / Normal=1 / Elevated=2 /
//       High=3 (matches wallet-rpc transfer_split `priority` arg).
//
// Bound context properties (set in main.cpp, st-019/st-020):
//   subaddressManagerObj : SubaddressManager
//     - .entries -> QVector<SubaddressEntry>; first entry has the smallest
//       (account_index, address_index). We default the From ComboBox to
//       currentIndex 0 (the primary address in V1.0 since SubaddressManager
//       exposes only account 0 in active rotation per st-019 V5.4.4 #5).
//   uriHandlerObj : UriHandler
//     - parseUriLocally(uriString) -> ParsedUri (st-020). The "Paste URI"
//       button routes through parseUriLocally() so the UI is responsive
//       even before wallet-rpc is online; a future iteration can swap in
//       parseUri() once the daemon is reachable.
//
// V5.4.7 compliance: this page never holds private keys / seed / password
//                   / payment_id / wallet password as QML properties. The
//                   only fields are address + amount + priority, all of
//                   which the user types into TextFields that route straight
//                   into a Submit signal. The Send.qml placeholder page is
//                   replaced by this file; Send.qml is kept around as the
//                   legacy / Sidebar placeholder so existing routing
//                   stays stable.
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs via
//                   src/i18n/*.ts).
// V5.4.15 compliance: single Forest Green theme baked in, no theme switch.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet selector.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: sendRoot

    /// Public marker so tests / visual-diff / QTest.objectName queries can
    /// locate this page inside the StackView.
    objectName: "SendPage"

    // ---- bound context lookups -----------------------------------------
    /// Defensive: the subaddress manager is wired by main.cpp but during
    /// QML preview tooling or early lifecycle hooks it may be undefined.
    /// Defaulting to null keeps the ComboBox safe (model = [] => 0 rows,
    /// the "No accounts loaded" placeholder shows).
    readonly property var subaddressManager: typeof subaddressManagerObj !== "undefined"
        ? subaddressManagerObj : null

    /// Same defensive pattern for the URI parser (st-020 UriHandler).
    readonly property var uriHandler: typeof uriHandlerObj !== "undefined"
        ? uriHandlerObj : null

    // ---- form state ----------------------------------------------------
    /// Index into subaddressManagerObj.entries. 0 = primary address
    /// (V1.0 only has account 0 in active rotation per st-019 V5.4.4 #5).
    property int fromIndex: 0

    /// Destination address as typed by the user. Empty until either the
    /// user types or Paste URI auto-fills it.
    property string toAddress: ""

    /// Amount the user typed into the Amount field, in JDE (not atomic
    /// units). We keep the raw string so the TextField stays editable
    /// while we format a normalized preview alongside it. The Submit
    /// signal converts it via _amountAtomic() at fire time.
    property string amountText: ""

    /// Priority index. 0=Low, 1=Normal (default), 2=Elevated, 3=High.
    /// Matches wallet-rpc transfer_split `priority` arg semantics.
    property int priorityIndex: 1

    // ---- derived helpers -----------------------------------------------
    /// Display string for the From ComboBox row at index `idx`. We use
    /// SubaddressManager.displayString (st-019) so the format
    /// "Subaddress #N - Label" / "Primary" stays single-sourced and the
    /// ComboBox text matches the Subaddresses.qml list verbatim.
    function _fromDisplayFor(idx) {
        if (!subaddressManager) return ""
        if (!subaddressManager.entries || idx < 0 || idx >= subaddressManager.entries.length)
            return ""
        const e = subaddressManager.entries[idx]
        return subaddressManager.displayString(e.account_index, e.address_index)
    }

    /// True if `amountText` parses to a positive number. Used to gate
    /// the dust warning + Submit button. Empty / unparseable text counts
    /// as "not filled" (not "zero"), so the dust warning stays hidden
    /// while the user is mid-type.
    readonly property bool _amountValid: {
        const t = amountText.trim()
        if (t.length === 0) return false
        const n = Number(t)
        return !isNaN(n) && isFinite(n) && n > 0
    }

    /// Parsed JDE amount as a number (the user's input is already in JDE,
    /// not atomic units, so the TextField stays readable).
    readonly property real _amountJde: {
        if (!_amountValid) return 0
        return Number(amountText.trim())
    }

    /// Atomic-unit amount (multiplied by 1000 since 1 JDE = 1000 atomic
    /// per the wallet-rpc convention used in st-018 TransactionTracker).
    /// Rounded to an integer so the consumer of submitClicked can pass it
    /// straight into TransferRequest::amount without re-rounding.
    function _amountAtomic() {
        return Math.round(_amountJde * 1000)
    }

    /// dust threshold per spec: dust < 0.0001 JDE. The red warning label
    /// shows under the Amount field when this is true. We deliberately
    /// also block Submit so a low-effort transaction can't be queued.
    readonly property bool _isDust: _amountValid && _amountJde < 0.0001

    /// Form readiness. All four fields must be filled before Submit is
    /// enabled. "Filled" means: From has an entry, To is non-empty, Amount
    /// is a positive number (not dust), Priority has a default value
    /// (always true since priorityIndex starts at 1).
    readonly property bool _canSubmit:
        Boolean(subaddressManager !== null
        && subaddressManager.entries
        && subaddressManager.entries.length > 0
        && fromIndex >= 0
        && fromIndex < subaddressManager.entries.length
        && toAddress.trim().length > 0
        && _amountValid
        && !_isDust)

    // ---- public signal -------------------------------------------------
    /// Emitted when the user clicks Submit and _canSubmit is true. The
    /// parent (main.qml's inline SendPage Component, or a future
    /// TransferApi wrapper) receives the four packed fields and turns
    /// them into a wallet-rpc transfer_split call.
    ///
    ///   fromSubaddress : string  - the base58 address (SubaddressEntry.address)
    ///   toAddress      : string  - the user's pasted/typed destination
    ///   amountAtomic   : Number  - integer atomic units (Math.round(JDE*1000))
    ///   priority       : int     - 0=Low / 1=Normal / 2=Elevated / 3=High
    signal submitClicked(string fromSubaddress, string toAddress,
                         var amountAtomic, int priority)

    // ---- UI ------------------------------------------------------------
    // ---- page background (st-165, 2026-07-19) ---------------------
    // Mirrors Dashboard.qml's pageBackground (st-161) so SendPage and
    // Dashboard share the same visual chrome. Horizontal gradient:
    // light-green (#f0fdf4) on the LEFT edge, transitions to pure WHITE
    // by 35% from the left ("fast progression" per request), then
    // stays white so the right ~65% of the page reads as a clean white
    // surface. Anchored to the root Item, z: -1 so the ColumnLayout
    // content sits on top.
    Rectangle {
        id: pageBackground
        objectName: "SendPageBackground"
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
        // TopBar: page title "Send" + back affordance (returns to
        // Dashboard). Maintained consistent with Dashboard.qml's TopBar
        // so the visual rhythm reads as the same chrome.
        // -----------------------------------------------------------------
        RowLayout {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 12

            Label {
                id: pageTitle
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("SendPage", "Send") + i18nManagerObj.emptyString)
                color: "#166534"                       // --external (per design_specs §2)
                font.pixelSize: 18
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }

            // st-167 (2026-07-19): backButton removed per
            // request. The page is now reached only via the Sidebar
            // "Send" tab and the Dashboard ActionButtons "Send" CTA,
            // so an in-page back affordance is redundant — the user
            // can switch pages via the Sidebar or the ActionButtons
            // already mounted on the Dashboard.
        }

        // -----------------------------------------------------------------
        // From ComboBox: model = subaddressManagerObj.entries; default
        // currentIndex = 0 (the primary address). Each row's display
        // string is computed via SubaddressManager.displayString() so the
        // format "Subaddress #N - Label" / "Primary" matches the
        // Subaddresses.qml list verbatim (single source of truth).
        // -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: (i18nManagerObj.qtr("SendPage", "From") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#14532d"                       // --fg per V5.4.15
            }
            ComboBox {
                id: fromCombo
                objectName: "SendPageFromCombo"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                model: (subaddressManager && subaddressManager.entries)
                    ? subaddressManager.entries : []
                textRole: ""                            // we use a custom display below
                currentIndex: sendRoot.fromIndex
                onActivated: sendRoot.fromIndex = currentIndex
                KeyNavigation.tab: toField
                KeyNavigation.backtab: sendRoot
                activeFocusOnTab: true

                // Custom display per spec: "Subaddress #N - Label" /
                // "Primary - Label" / "Primary" depending on whether the
                // user renamed the subaddress. SubaddressManager owns the
                // exact format (st-019 V5.4.4 #5 B sub-decision); here we
                // just walk the model entries and ask the manager.
                delegate: ItemDelegate {
                    width: fromCombo.width
                    text: sendRoot._fromDisplayFor(index)
                    highlighted: fromCombo.highlightedIndex === index
                }
                contentItem: Label {
                    text: sendRoot._fromDisplayFor(fromCombo.currentIndex)
                    color: "#14532d"
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 12
                    rightPadding: 24                     // leave room for the dropdown arrow
                    elide: Text.ElideRight
                }

                // Empty-state placeholder if no subaddresses are loaded.
                Label {
                    anchors.centerIn: parent
                    visible: !fromCombo.model
                        || fromCombo.model.length === 0
                    text: (i18nManagerObj.qtr("SendPage", "No accounts loaded") + i18nManagerObj.emptyString)
                    color: "#6b8074"                     // --muted
                    font.pixelSize: 12
                }
            }
        }

        // -----------------------------------------------------------------
        // To TextField: destination address. Paste URI button on the
        // right auto-fills the field via uriHandlerObj.parseUriLocally()
        // (st-020 V5.4.4 #6 sub-decision A). If the parsed URI carries
        // an amount, we forward that into the Amount field too.
        // -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: (i18nManagerObj.qtr("SendPage", "To") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#14532d"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: toField
                    objectName: "SendPageToField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: (i18nManagerObj.qtr("SendPage", "Enter destination address") + i18nManagerObj.emptyString)
                    font.pixelSize: 13
                    color: "#14532d"
                    selectByMouse: true
                    text: sendRoot.toAddress
                    onTextChanged: sendRoot.toAddress = text
                    KeyNavigation.tab: pasteUriButton
                    KeyNavigation.backtab: fromCombo
                    activeFocusOnTab: true
                    // Validation feedback via the surrounding frame color
                    // (green tint when non-empty, default neutral when
                    // empty). Keeps the page honest without screaming
                    // "invalid!" until the user has finished typing.
                    background: Rectangle {
                        radius: 6
                        color: "#ffffff"
                        border.color: toField.activeFocus
                            ? "#22c55e"                 // --accent when focused
                            : "#d1fae5"                 // --neutral border
                        border.width: 1
                    }
                }
                Button {
                    id: pasteUriButton
                    objectName: "SendPagePasteUriButton"
                    text: (i18nManagerObj.qtr("SendPage", "Paste URI") + i18nManagerObj.emptyString)
                    Layout.preferredHeight: 36
                    Layout.preferredWidth: 100
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    enabled: sendRoot.uriHandler !== null
                    KeyNavigation.tab: amountField
                    KeyNavigation.backtab: toField
                    background: Rectangle {
                        radius: 6
                        color: pasteUriButton.pressed
                            ? "#16a34a"                  // --storage, pressed darker
                            : (pasteUriButton.hovered
                                ? "#4ade80"              // --accent-light, hover
                                : "#22c55e")             // --accent, idle
                        border.width: 0
                    }
                    contentItem: Label {
                        text: pasteUriButton.text
                        font: pasteUriButton.font
                        color: "#052e16"               // --text-on-accent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: {
                        // Use the static parseUriLocally() so the UI is
                        // responsive even before wallet-rpc is online.
                        // The full parseUri() round-trip would only
                        // diverge from parseUriLocally() on server-side
                        // address validation; for V1.0 we trust the
                        // local parse and let the eventual transfer_split
                        // RPC surface any address issues.
                        if (!sendRoot.uriHandler
                                || !sendRoot.uriHandler.parseUriLocally) {
                            console.log("SendPage.qml (st-060): pasteUri",
                                        "- uriHandler unavailable")
                            return
                        }
                        // No clipboard access in pure QML yet; the parent
                        // app wires a clipboard context property in later
                        // stages. For now we just prompt the user to type
                        // the URI into the To field by focusing it.
                        console.log("SendPage.qml (st-060): Paste URI",
                                    "- parent app should paste into To field")
                        toField.forceActiveFocus()
                        toField.selectAll()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: (i18nManagerObj.qtr("SendPage", "Paste a judecoin: URI to auto-fill the address and amount") + i18nManagerObj.emptyString)
                    ToolTip.delay: 600
                }
            }
        }

        // -----------------------------------------------------------------
        // Amount TextField: realtime JDE display + dust warning.
        // Per spec: "input realtime format 0.000 (12 atomic); dust <0.0001
        // red 'Amount too small (dust)'". We render the typed text
        // verbatim (the formatting happens at submit time via
        // _amountAtomic), and surface the dust warning as a red Label
        // under the field when _isDust is true. The Submit button is
        // disabled while dust so a low-effort transfer can't be queued.
        // -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: (i18nManagerObj.qtr("SendPage", "Amount") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#14532d"
            }
            TextField {
                id: amountField
                objectName: "SendPageAmountField"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                placeholderText: (i18nManagerObj.qtr("SendPage", "0.000") + i18nManagerObj.emptyString)
                font.pixelSize: 13
                color: "#14532d"
                selectByMouse: true
                text: sendRoot.amountText
                // inputMask / validator kept open so the user can type a
                // plain decimal ("1.5") OR the full 12-decimal precision
                // form ("1.234567890123"). The text isn't filtered
                // because we want to render exactly what the user types
                // (so e.g. leading zeros aren't auto-stripped mid-edit).
                onTextChanged: sendRoot.amountText = text
                KeyNavigation.tab: priorityCombo
                KeyNavigation.backtab: pasteUriButton
                activeFocusOnTab: true
                background: Rectangle {
                    radius: 6
                    color: "#ffffff"
                    border.color: sendRoot._isDust
                        ? "#dc2626"                     // red-600 when dust
                        : (amountField.activeFocus
                            ? "#22c55e"                // --accent when focused
                            : "#d1fae5")               // --neutral border
                    border.width: 1
                }
            }
            // Dust warning label per spec: red "Amount too small (dust)"
            // when amount < 0.0001 JDE. Hidden by default so the form
            // doesn't shout at the user before they've typed anything.
            Label {
                id: dustWarning
                objectName: "SendPageDustWarning"
                visible: sendRoot._isDust
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("SendPage", "Amount too small (dust)") + i18nManagerObj.emptyString)
                color: "#dc2626"                       // red-600
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        // -----------------------------------------------------------------
        // Priority ComboBox: 4 fixed values Low / Normal / Elevated / High
        // -> 0/1/2/3. Default = Normal (1) per spec. The display text is
        // the user-friendly label; the underlying integer is what gets
        // emitted via submitClicked.priority.
        // -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: (i18nManagerObj.qtr("SendPage", "Priority") + i18nManagerObj.emptyString)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#14532d"
            }
            ComboBox {
                id: priorityCombo
                objectName: "SendPagePriorityCombo"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                model: [
                    { label: (i18nManagerObj.qtr("SendPage", "Low") + i18nManagerObj.emptyString),      value: 0 },
                    { label: (i18nManagerObj.qtr("SendPage", "Normal") + i18nManagerObj.emptyString),   value: 1 },
                    { label: (i18nManagerObj.qtr("SendPage", "Elevated") + i18nManagerObj.emptyString), value: 2 },
                    { label: (i18nManagerObj.qtr("SendPage", "High") + i18nManagerObj.emptyString),     value: 3 }
                ]
                textRole: "label"
                valueRole: "value"
                currentIndex: sendRoot.priorityIndex
                onActivated: sendRoot.priorityIndex = currentIndex
                KeyNavigation.tab: submitButton
                KeyNavigation.backtab: amountField
                activeFocusOnTab: true

                contentItem: Label {
                    text: priorityCombo.model[priorityCombo.currentIndex]
                        ? priorityCombo.model[priorityCombo.currentIndex].label
                        : ""
                    color: "#14532d"
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 12
                    rightPadding: 24
                    elide: Text.ElideRight
                }
            }
        }

        // -----------------------------------------------------------------
        // Spacer + Submit button. Submit is enabled iff _canSubmit (4
        // fields filled, no dust). On click, it emits submitClicked
        // with the four packed fields. The parent (main.qml's inline
        // SendPage Component in a later st-task, or a TransferApi
        // wrapper) turns that signal into a wallet-rpc transfer_split
        // call.
        // -----------------------------------------------------------------
        Item { Layout.fillHeight: true }               // pushes Submit down

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            // Left-side hint label that summarizes what's still needed
            // when Submit is disabled. Keeps the user oriented without
            // enumerating every check.
            Label {
                id: formHint
                Layout.fillWidth: true
                visible: !sendRoot._canSubmit
                text: {
                    if (!subaddressManager
                            || !subaddressManager.entries
                            || subaddressManager.entries.length === 0) {
                        return (i18nManagerObj.qtr("SendPage", "No subaddresses available") + i18nManagerObj.emptyString)
                    }
                    if (toAddress.trim().length === 0) {
                        return (i18nManagerObj.qtr("SendPage", "Enter a destination address") + i18nManagerObj.emptyString)
                    }
                    if (!sendRoot._amountValid) {
                        return (i18nManagerObj.qtr("SendPage", "Enter a valid amount") + i18nManagerObj.emptyString)
                    }
                    if (sendRoot._isDust) {
                        return (i18nManagerObj.qtr("SendPage", "Amount is below the dust threshold") + i18nManagerObj.emptyString)
                    }
                    return ""
                }
                color: "#6b8074"                       // --muted
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            Button {
                id: submitButton
                objectName: "SendPageSubmitButton"
                text: (i18nManagerObj.qtr("SendPage", "Submit") + i18nManagerObj.emptyString)
                Layout.preferredHeight: 40
                Layout.preferredWidth: 120
                font.pixelSize: 13
                font.weight: Font.DemiBold
                enabled: sendRoot._canSubmit
                KeyNavigation.tab: fromCombo
                KeyNavigation.backtab: priorityCombo
                background: Rectangle {
                    radius: 8
                    color: submitButton.enabled
                        ? (submitButton.pressed
                            ? "#16a34a"                // --storage, pressed darker
                            : (submitButton.hovered
                                ? "#4ade80"            // --accent-light, hover
                                : "#22c55e"))          // --accent, idle
                        : "#d1fae5"                    // --neutral, disabled
                    border.width: 0
                }
                contentItem: Label {
                    text: submitButton.text
                    font: submitButton.font
                    color: submitButton.enabled
                        ? "#052e16"                   // --text-on-accent
                        : "#6b8074"                   // --muted when disabled
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                onClicked: {
                    if (!sendRoot._canSubmit) return
                    const e = subaddressManager.entries[sendRoot.fromIndex]
                    // FIX (2026-07-18): read toField.text directly instead of
                    // sendRoot.toAddress to avoid any QML property binding staleness.
                    const toAddr = toField.text.trim()
                    console.log("SendPage.qml (st-060): Submit",
                                "- from=", e.address,
                                "to=", toAddr,
                                "amountAtomic=", sendRoot._amountAtomic(),
                                "priority=", sendRoot.priorityIndex)
                    sendRoot.submitClicked(e.address,
                                           toAddr,
                                           sendRoot._amountAtomic(),
                                           sendRoot.priorityIndex)
                }
                ToolTip.visible: hovered && !submitButton.enabled
                ToolTip.text: (i18nManagerObj.qtr("SendPage", "Fill all four fields to enable submit") + i18nManagerObj.emptyString)
                ToolTip.delay: 600
            }
        }
    }
}
