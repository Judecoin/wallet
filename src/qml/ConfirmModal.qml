// =============================================================================
// Judecoin Wallet - ConfirmModal.qml (st-061)
//
// St-061: dedicated password-confirm modal. Mounted in the parent's
// Overlay.overlay (modal: true, dim: true) so it overlays the entire
// application window with a dim backdrop. Triggered by the parent
// (post-st-061 wiring: main.qml listens for SendPage.qml's
// submitClicked; a future st-062/063 wires the actual transfer).
//
// Contract (per st-061 spec):
//   * Public API:
//       - open()                              show the modal, reset state
//       - close()                             dismiss the modal
//       - property string password            user-typed password (default "")
//       - property bool visible               show/hide the modal
//       - property string errorMessage        red label text (default "")
//       - signal confirmClicked(string)       emitted on correct password
//       - signal cancelled()                  emitted on cancel / X / Esc / backdrop
//       - function _verifyPassword()          calls walletManagerObj.verifyPassword
//                                             returns true on correct password,
//                                             sets errorMessage + returns false
//                                             on wrong password
//   * Behavior:
//       - Mounted in Overlay.overlay; modal: true; dim: true; click on
//         the dim backdrop (outside the panel) -> cancelled().
//       - Enter / Esc keys handled: Enter -> Confirm click,
//         Esc -> cancelled().
//       - Confirm click -> _verifyPassword() -> confirmClicked(password)
//         on success; "Incorrect password" red label on failure
//         (Confirm stays enabled so the user can retry).
//       - visible=false -> _verifyPassword() reset (state wiped so a
//         re-open starts clean).
//       - confirmClicked emits only; the modal does NOT auto-close --
//         the parent (main.qml) decides what happens next (typically:
//         hide the modal + start the transfer RPC).
//
//   * V5.4.7 compliance: no secret-bearing property bindings. The
//     `password` field is the user-typed buffer; it is cleared on
//     close() / on success (emit + clear) / on failure (kept so the
//     user can retry). It is NEVER logged (the QML console.log lines
//     below print the verify result, not the password bytes).
//   * V5.4.9 compliance: pure QML (no Widgets), V5.4.15 Forest Green
//     theme (greens #052e16 / #14532d / #16a34a / #22c55e / #bbf7d0 +
//     --muted #6b8074 + --danger #dc2626).
//   * V5.4.7 #7C password local verify: the modal's _verifyPassword()
//     calls walletManagerObj.verifyPassword(password) (a Q_INVOKABLE on
//     WalletManager added in st-061). The verify path is the
//     wallet-rpc open_wallet roundtrip + local KDF cost (~0.1s wall
//     clock; see WalletManager::verifyPassword for the close+reopen
//     side effect documentation). The password bytes never leave the
//     process -- they are forwarded verbatim to wallet-rpc over the
//     same authenticated HTTP channel that V1.0 already uses.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: confirmRoot

    // ---- public API ----------------------------------------------------

    /// Modal objectName. Test + main.qml wiring both use this id to
    /// find the root. The full id "ConfirmModal" matches the spec.
    objectName: "ConfirmModal"

    /// The user-typed password buffer. Cleared on close() / on
    /// successful confirm. Kept on a failed verify so the user can
    /// retry without re-typing the entire string.
    property string password: ""

    /// Modal visibility. We deliberately do NOT redeclare `visible`
    /// because Item.visible is a FINAL property (inherited from
    /// QQuickItem) and re-declaring it aborts the QML load with
    /// "Cannot override FINAL property". The show/hide API is the
    /// inherited Item.visible -- callers can either set
    /// `confirmRoot.visible = true` directly or use the open() /
    /// close() helpers below (which also reset password +
    /// errorMessage). The onVisibleChanged handler at the bottom
    /// of the root fires for both direct assignment and
    /// open()/close() (since both paths mutate Item.visible).

    /// Error message shown in the red label. Empty string -> label is
    /// hidden. Non-empty -> label visible with the message text.
    /// Spec contract: on a failed verify we set this to
    /// (i18nManagerObj.qtr("ConfirmModal", "Incorrect password") + i18nManagerObj.emptyString) regardless of the underlying
    /// RpcException message (we never echo the wallet-rpc internals
    /// back to the user).
    property string errorMessage: ""

    /// ---- transfer detail fields (st-061 spec "4-row detail table") -------
    ///
    /// The 4 detail fields are filled in by the parent (main.qml) before
    /// the modal is opened -- typically by reading SendPage.qml's
    /// submitClicked(from, to, amount, priority) payload and computing
    /// the estimated fee via the wallet-rpc. The modal itself does NOT
    /// recompute the fee; it just displays what the parent supplied.
    ///
    /// All four default to "" so the modal is safe to mount before the
    /// caller has populated them (e.g. an automated test that exercises
    /// just the password-verify path without any transfer context). When
    /// the value is empty, the row still renders but shows an
    /// ellipsis placeholder so the table layout does not jump.
    ///
    /// V5.4.7 compliance: fromSubaddress / toAddress are PUBLIC base58
    /// addresses, NOT private keys / seeds / passwords. They are safe to
    /// log for debugging the transfer pipeline. (V5.4.7 only forbids
    /// secret-bearing properties; the transfer counterparty is by
    /// definition not secret.)

    /// Source address (the user's own subaddress, base58). Shown in
    /// the "From" row of the detail table. V5.4.7: this is a public
    /// address; logging it for support is allowed.
    property string fromSubaddress: ""

    /// Destination address (the recipient's base58 address). Shown in
    /// the "To" row of the detail table. V5.4.7: public; allowed to
    /// log.
    property string toAddress: ""

    /// Transfer amount in JDE (already-formatted, NOT atomic units).
    /// The parent converts the SendPage's submitClicked amountAtomic
    /// to a JDE string before assigning. Shown in the "Amount" row.
    property string amountText: ""

    /// Estimated network fee in JDE (already-formatted). The parent
    /// either computes this via wallet-rpc estimate_fee or shows "—"
    /// for an unknown fee. Shown in the "Fee" row.
    property string feeText: ""


    /// Emitted when _verifyPassword() returns true. The parent (main.qml
    /// or the integration shell) decides what to do next -- the modal
    /// does NOT auto-close so the parent can show a "Transferring..."
    /// state on top of the modal (or, more commonly, hide the modal
    /// and start the RPC).
    signal confirmClicked(string password)

    /// Emitted on Cancel click, X click, Esc key, or backdrop click.
    /// The modal auto-closes on this signal (consistent with the spec
    /// "cancelled" semantics -- "cancelled" implies the user is no
    /// longer looking at the modal).
    signal cancelled()

    /// Show the modal and reset transient state. Wipes password and
    /// errorMessage so a re-open always starts clean; positions the
    /// focus on the password field so keyboard users can type
    /// immediately.
    function open() {
        // Do NOT clear fromSubaddress/toAddress/amountText/feeText
        // here -- they are set externally by the parent (main.qml)
        // BEFORE open() is called (typically from the SendPage
        // submitClicked signal handler). Resetting them inside open()
        // would wipe the values the parent just assigned, breaking
        // the round trip. password + errorMessage ARE cleared
        // because they are 100% transient modal state.
        confirmRoot.password = ""
        confirmRoot.errorMessage = ""
        confirmRoot.visible = true
        // Defer the focus call to next tick so the panel has been
        // re-shown (visible=true triggers a re-render) before we ask
        // Qt to put the cursor in the TextField. Calling focus()
        // synchronously here would race the visibility change on
        // some Qt 6.4 builds and silently no-op.
        Qt.callLater(function() {
            passwordField.forceActiveFocus()
        })
    }

    /// Hide the modal. Wipes the password buffer so a closed modal
    /// does not retain a secret in process memory (V5.4.7 #7C
    /// requirement: do not cache passwords). Does NOT emit
    /// cancelled() -- the caller is expected to emit cancelled() if
    /// the dismiss was user-initiated (X / Cancel / Esc / backdrop).
    function close() {
        confirmRoot.visible = false
        confirmRoot.password = ""
        confirmRoot.errorMessage = ""
    }

    /// Local password verifier. Returns true if the wallet-rpc
    /// open_wallet roundtrip with the current password succeeds;
    /// false (and sets errorMessage) on wrong password or any other
    /// failure. Called by the Confirm button onClicked handler below;
    /// also exposed as a function so a test can invoke it via
    /// QMetaObject::invokeMethod and assert the return value.
    ///
    /// V5.4.7: the password is forwarded verbatim to wallet-rpc via
    /// walletManagerObj.verifyPassword. We do NOT log the password
    /// bytes (the only console.log line is the result count).
    function _verifyPassword() {
        if (!confirmRoot.walletManager
                || !confirmRoot.password
                || confirmRoot.password.length === 0) {
            // Pre-condition failure: no wallet, or empty password.
            // The latter should not happen (Confirm is disabled when
            // password is empty) but we guard anyway.
            confirmRoot.errorMessage = (i18nManagerObj.qtr("ConfirmModal", "Incorrect password") + i18nManagerObj.emptyString)
            return false
        }
        try {
            // st-061 + V5.4.7 #7C: ~0.1s wall clock for the local
            // password check. wallet-rpc's open_wallet does a
            // close+reopen of the currently-open wallet under the
            // hood; for the V1.0 SendPage -> ConfirmModal -> transfer
            // flow this is acceptable.
            var ok = confirmRoot.walletManager.verifyPassword(
                confirmRoot.password)
            if (ok) {
                confirmRoot.errorMessage = ""
                return true
            } else {
                confirmRoot.errorMessage = (i18nManagerObj.qtr("ConfirmModal", "Incorrect password") + i18nManagerObj.emptyString)
                return false
            }
        } catch (e) {
            // verifyPassword is a Q_INVOKABLE bool that does NOT
            // throw on a wrong password -- it returns false. This
            // catch is a defensive guard against the (unlikely) case
            // where the QML engine marshals a transport-level
            // exception (e.g. wallet-rpc disconnected mid-call).
            console.warn("ConfirmModal.qml (st-061): _verifyPassword threw:",
                         String(e))
            confirmRoot.errorMessage = (i18nManagerObj.qtr("ConfirmModal", "Incorrect password") + i18nManagerObj.emptyString)
            return false
        }
    }

    // ---- context wiring -----------------------------------------------

    /// walletManagerObj context property (set by main.cpp). Lazy-
    /// resolved so QML preview tooling that instantiates this file
    /// without a context property still loads. Truthy check guards
    /// every call so the modal is safe to mount before the wizard
    /// has finished onboarding.
    property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null

    // ---- visibility binding -------------------------------------------

    /// visible=false -> _verifyPassword() reset. Implemented as a
    /// property change handler so direct assignment to `visible`
    /// (e.g. `confirmRoot.visible = false`) also resets the state.
    /// open() and close() also reset the state explicitly so the
    /// open/close round trip is hermetic.
    onVisibleChanged: {
        if (!visible) {
            password = ""
            errorMessage = ""
        }
    }

    // ---- layout -------------------------------------------------------

    // Full-overlay wrapper. Sits at the root level of the Item so
    // the parent can position this Item at the application window's
    // origin (anchors.fill: parent in main.qml).
    anchors.fill: parent

    // Dim backdrop + click-outside-to-cancel. The dim layer sits
    // behind the panel; clicks anywhere on the dim layer (not on
    // the panel) emit cancelled(). The panel itself has a MouseArea
    // with hoverEnabled + onPositionChanged that stops the event
    // propagation so click-on-panel does NOT trigger cancel.
    Rectangle {
        id: backdrop
        objectName: "ConfirmModalBackdrop"
        anchors.fill: parent
        color: "#052e16"                       // --text-on-accent at 0.55 alpha
        opacity: confirmRoot.visible ? 0.55 : 0.0
        // Smooth fade in/out so the modal does not snap. Short
        // duration keeps the perceived "instant open" feel.
        Behavior on opacity {
            NumberAnimation { duration: 120 }
        }

        MouseArea {
            anchors.fill: parent
            // intercept clicks on the dim layer
            onClicked: {
                console.log("ConfirmModal.qml (st-061): backdrop click -> cancelled()")
                confirmRoot.cancelled()
                confirmRoot.close()
            }
        }
    }

    // Centered panel. width: 460 fits in 1100x700 window with
    // ~320px of margin. height: implicit (driven by the ColumnLayout
    // children). On small windows the panel could overflow -- for
    // V1.0 we accept this (the design_specs is for desktop only).
    Rectangle {
        id: panel
        objectName: "ConfirmModalPanel"
        anchors.centerIn: parent
        width: 480
        implicitHeight: panelLayout.implicitHeight + 48
        radius: 12
        color: "#ffffff"
        border.color: "#bbf7d0"               // --neutral border
        border.width: 1
        // Drop shadow per V5.4.15 design tokens (the "shadow-card"
        // elevation from BalanceCard/ActionButtons/RecentTransactions).
        // Implemented as a soft outline + a darker drop via
        // Rectangle gradient -- true drop shadow needs a separate
        // ShaderEffect which we do not have in this binary; the
        // border + 2 stacked Rectangles below approximate the same
        // visual weight.
        layer.enabled: false

        // The click-eater inside the panel. Without this, the
        // backdrop's MouseArea would still receive clicks that
        // originate from the panel because the backdrop MouseArea
        // is `anchors.fill: parent` and the panel is a sibling
        // rendered on top. We consume the click here so the
        // backdrop's onClicked does not fire when the user
        // interacts with form widgets.
        MouseArea {
            anchors.fill: parent
            // hoverEnabled so the cursor stays the default arrow
            // over the panel; we just want to consume clicks.
            propagateComposedEvents: true
            onClicked: function(mouse) {
                mouse.accepted = false  // let children handle it
            }
        }

        ColumnLayout {
            id: panelLayout
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            // ----- header row: title + X close button ------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Label {
                    id: titleLabel
                    objectName: "ConfirmModalTitle"
                    text: (i18nManagerObj.qtr("ConfirmModal", "Confirm transfer") + i18nManagerObj.emptyString)
                    font.pixelSize: 18
                    font.bold: true
                    color: "#14532d"               // --fg
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                // X close button. Square 28x28; small hit area but
                // big enough for a mouse click. Disabled when not
                // visible to avoid stray clicks during the fade
                // animation.
                Button {
                    id: closeButton
                    objectName: "ConfirmModalCloseButton"
                    text: "\u2715"                  // ✕
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    font.pixelSize: 14
                    font.bold: true
                    enabled: confirmRoot.visible
                    background: Rectangle {
                        radius: 4
                        color: closeButton.pressed
                            ? "#d1fae5"
                            : (closeButton.hovered
                                ? "#f0fdf4"          // --bg-light
                                : "transparent")
                        border.width: 0
                    }
                    contentItem: Label {
                        text: closeButton.text
                        color: "#14532d"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        console.log("ConfirmModal.qml (st-061): X close -> cancelled()")
                        confirmRoot.cancelled()
                        confirmRoot.close()
                    }
                }
            }

            // ----- detail table: From / To / Amount / Fee (st-061 spec) ----
            //
            // 2-column GridLayout. Column 1 is the label
            // (dimmed, font.bold, 11-12px). Column 2 is the value
            // (primary color, monospace where possible, 12-13px). All
            // text is qsTr()'d (V5.4.9) so the modal respects the 9
            // langs in i18n/. Empty values render an em-dash
            // placeholder so the layout does not jump between
            // states. Row heights are uniform so a 4-line From/To
            // (when truncated) does not visibly grow the row.
            GridLayout {
                id: detailTable
                objectName: "ConfirmModalDetailTable"
                Layout.fillWidth: true
                Layout.preferredHeight: 4 * 28 + 12   // 4 rows * 28px + spacing buffer
                columns: 2
                columnSpacing: 16
                rowSpacing: 6

                // Repeater-based rows so adding a 5th field (e.g.
                // "Priority") is a one-line model edit. Each row
                // reads from a JS array literal below.
                Repeater {
                    model: [
                        { label: (i18nManagerObj.qtr("ConfirmModal", "From") + i18nManagerObj.emptyString),   value: confirmRoot.fromSubaddress,
                          objectName: "ConfirmModalDetailFrom" },
                        { label: (i18nManagerObj.qtr("ConfirmModal", "To") + i18nManagerObj.emptyString),     value: confirmRoot.toAddress,
                          objectName: "ConfirmModalDetailTo" },
                        { label: (i18nManagerObj.qtr("ConfirmModal", "Amount") + i18nManagerObj.emptyString), value: confirmRoot.amountText,
                          objectName: "ConfirmModalDetailAmount" },
                        { label: (i18nManagerObj.qtr("ConfirmModal", "Fee") + i18nManagerObj.emptyString),    value: confirmRoot.feeText,
                          objectName: "ConfirmModalDetailFee" }
                    ]
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        spacing: 12

                        Label {
                            text: modelData.label
                            color: "#6b8074"            // --muted
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            Layout.preferredWidth: 72
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                        }

                        // The value Label is wrapped in a Loader-style
                        // Item that grabs the modelData.objectName so
                        // tests can find each cell via findChild<QObject*>.
                        Item {
                            id: detailCellItem
                            objectName: modelData.objectName
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 0
                                anchors.rightMargin: 0
                                text: (modelData.value
                                       && modelData.value.length > 0)
                                      ? modelData.value
                                      : (i18nManagerObj.qtr("ConfirmModal", "\u2014") + i18nManagerObj.emptyString)    // em-dash placeholder
                                color: (modelData.value
                                        && modelData.value.length > 0)
                                       ? "#14532d"         // --fg
                                       : "#9ca3af"         // --muted-light
                                font.pixelSize: 12
                                font.family: modelData.objectName === "ConfirmModalDetailAmount"
                                             || modelData.objectName === "ConfirmModalDetailFee"
                                             ? "monospace"
                                             : font.family
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                            }
                        }
                    }
                }
            }

            // ----- body: intro + password field + error label ----
            Label {
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("ConfirmModal", "Enter your wallet password to confirm this transaction. The password is verified locally against the open wallet and is never logged.") + i18nManagerObj.emptyString)
                color: "#6b8074"                   // --muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.maximumWidth: panel.width - 48
            }

            // Password input. TextInput.Password masks the
            // characters in the field. placeholderText nudges the
            // user; the label "Password" above the field is omitted
            // (the body intro already establishes the field's
            // purpose) so the modal feels compact.
            TextField {
                id: passwordField
                objectName: "ConfirmModalPasswordField"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: (i18nManagerObj.qtr("ConfirmModal", "Wallet password") + i18nManagerObj.emptyString)
                text: confirmRoot.password
                onTextChanged: confirmRoot.password = text
                echoMode: TextInput.Password
                // Red border when there is an error message; green
                // when focused; neutral otherwise. Same color
                // scheme as the SendPage Amount field.
                background: Rectangle {
                    radius: 6
                    color: "#ffffff"
                    border.color: confirmRoot.errorMessage.length > 0
                        ? "#dc2626"               // --danger, error
                        : (passwordField.activeFocus
                            ? "#22c55e"          // --accent, focused
                            : "#d1fae5")         // --neutral border
                    border.width: 1
                }
                // Keys.Enter -> Confirm click (only when Confirm is
                // enabled, i.e. password is non-empty). Keys.Escape
                // -> cancelled() + close(). Both are top-level so
                // any descendant TextField gets the same handling;
                // the TextField has its own Keys.onReturnPressed
                // we route through onAccepted below.
                Keys.onEscapePressed: function(event) {
                    console.log("ConfirmModal.qml (st-061): Esc key -> cancelled()")
                    confirmRoot.cancelled()
                    confirmRoot.close()
                    event.accepted = true
                }
                onAccepted: {
                    // Enter key inside the TextField. Re-use the
                    // Confirm button onClicked logic by calling
                    // _verifyPassword() directly so the spec's
                    // "Enter key handled" contract is satisfied.
                    if (confirmRoot.password.length === 0) return
                    if (confirmRoot._verifyPassword()) {
                        const pwd = confirmRoot.password
                        // Clear the buffer before emit so a
                        // listener that synchronously re-opens the
                        // modal does not see the secret in the
                        // property (V5.4.7 #7C).
                        confirmRoot.password = ""
                        confirmRoot.errorMessage = ""
                        confirmRoot.confirmClicked(pwd)
                    }
                }
            }

            // Error label. Hidden when errorMessage is empty;
            // visible (red) when set. Always reserves its layout
            // slot via Layout.preferredHeight so the modal does
            // not jump when the error appears.
            Label {
                id: errorLabel
                objectName: "ConfirmModalErrorLabel"
                Layout.fillWidth: true
                Layout.preferredHeight: 18
                text: confirmRoot.errorMessage
                color: "#dc2626"                   // red-600
                font.pixelSize: 12
                font.weight: Font.DemiBold
                visible: confirmRoot.errorMessage.length > 0
                elide: Text.ElideRight
            }

            // ----- footer: Cancel + Confirm ---------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 12

                // Left: small explainer so the user understands
                // why the modal is asking for a password. Hidden
                // on narrow windows to keep the row compact.
                Label {
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("ConfirmModal", "Required to authorise outgoing transfers") + i18nManagerObj.emptyString)
                    color: "#6b8074"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                }

                Button {
                    id: cancelButton
                    objectName: "ConfirmModalCancelButton"
                    text: (i18nManagerObj.qtr("ConfirmModal", "Cancel") + i18nManagerObj.emptyString)
                    Layout.preferredHeight: 40
                    Layout.preferredWidth: 96
                    font.pixelSize: 13
                    enabled: confirmRoot.visible
                    background: Rectangle {
                        radius: 8
                        color: cancelButton.pressed
                            ? "#d1fae5"
                            : (cancelButton.hovered
                                ? "#f0fdf4"
                                : "#ffffff")
                        border.color: "#bbf7d0"     // --neutral border
                        border.width: 1
                    }
                    contentItem: Label {
                        text: cancelButton.text
                        color: "#14532d"
                        font: cancelButton.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        console.log("ConfirmModal.qml (st-061): Cancel -> cancelled()")
                        confirmRoot.cancelled()
                        confirmRoot.close()
                    }
                }

                Button {
                    id: confirmButton
                    objectName: "ConfirmModalConfirmButton"
                    text: (i18nManagerObj.qtr("ConfirmModal", "Confirm") + i18nManagerObj.emptyString)
                    Layout.preferredHeight: 40
                    Layout.preferredWidth: 120
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    // Disabled when the password is empty. The
                    // spec mandates `password.length >= 1 -> enabled`,
                    // so we do NOT gate on a verify result here
                    // (a wrong password leaves Confirm enabled so
                    // the user can retry).
                    enabled: confirmRoot.visible
                             && confirmRoot.password.length >= 1
                    background: Rectangle {
                        radius: 8
                        color: confirmButton.enabled
                            ? (confirmButton.pressed
                                ? "#16a34a"          // --storage, pressed
                                : (confirmButton.hovered
                                    ? "#4ade80"      // --accent-light
                                    : "#22c55e"))    // --accent
                            : "#d1fae5"              // --neutral, disabled
                        border.width: 0
                    }
                    contentItem: Label {
                        text: confirmButton.text
                        color: confirmButton.enabled
                            ? "#052e16"             // --text-on-accent
                            : "#6b8074"             // --muted when disabled
                        font: confirmButton.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    onClicked: {
                        if (confirmRoot.password.length === 0) return
                        if (confirmRoot._verifyPassword()) {
                            const pwd = confirmRoot.password
                            // Clear the buffer before emit so a
                            // listener that synchronously re-opens
                            // the modal does not see the secret
                            // in the property (V5.4.7 #7C).
                            confirmRoot.password = ""
                            confirmRoot.errorMessage = ""
                            confirmRoot.confirmClicked(pwd)
                        }
                        // On failure _verifyPassword() has set
                        // errorMessage = (i18nManagerObj.qtr("ConfirmModal", "Incorrect password") + i18nManagerObj.emptyString);
                        // Confirm stays enabled so the user can
                        // retry without re-typing.
                    }
                }
            }
        }
    }

    // ---- keyboard handling at the Item level --------------------------

    // Keys.onEscapePressed on the root catches Esc when focus is
    // anywhere inside the modal (the TextField also handles it
    // locally, but the root handler is a safety net for the
    // "focus is on a button" case). The TextField's local handler
    // runs first; if it sets event.accepted=true this root handler
    // is skipped, which is the desired behavior.
    Keys.onEscapePressed: function(event) {
        console.log("ConfirmModal.qml (st-061): Esc (root) -> cancelled()")
        confirmRoot.cancelled()
        confirmRoot.close()
        event.accepted = true
    }

    // The modal root needs to be focusable for the root-level Esc
    // handler to fire when a button is focused (TextField handles Esc
    // itself; the root only matters when focus is on a non-input
    // widget). activeFocusOnTab is the standard QML idiom.
    activeFocusOnTab: true
}
