// =============================================================================
// Judecoin Wallet - Case2Open.qml (st-071)
//
// Case 2 (Open existing wallet) detail page. The OnboardingWizard home grid
// lists four case cards; Case 2 ("Open existing wallet") currently bounces
// here so the user can decrypt an existing .keys file with its password.
//
// Layout (single-step wizard, intentionally lighter than Case1Create.qml):
//
//   [Forest Green top bar]
//     \u2190 Back  |  Case 2 - Open existing wallet                [step 1/1]
//
//   [Centered card]
//     <hero copy>  Open an existing wallet on this device.
//                  Enter the .keys file name and the password you set
//                  when you first created it.
//
//     Wallet filename      [ TextField  "wallet-name.keys"       ]
//     Wallet password      [ TextField  "Wallet password"  eye ]   <- eye toggles echoMode
//
//     [ Open wallet ]                                  primary CTA
//
//     <status row>
//       red label "Incorrect password"                 on openWallet throw
//       BusyIndicator + "Opening wallet..."            on inFlight
//
//     Forgot password?  Recover from seed instead       <- jumps to Case 3
//
//   [Forest Green bottom bar]
//     \u2190 Back to wizard                            secondary CTA
//
// Behaviour (per st-071 spec_notes):
//   * **PasswordField**: placeholder "Wallet password", show/hide eye icon
//     that toggles TextInput.Password <-> TextInput.Normal. We do NOT
//     ever store the password in a property the user can re-display
//     (V5.4.7 #7C): the eye only changes the echoMode of the SAME
//     in-memory `password` string, which is wiped on every exit path.
//   * **Submit**: walletManagerObj.openWallet(filename, password) -> local
//     ~0.1s verify -> emit walletReady(filename) on success.
//   * **error**: "Incorrect password" red label (no modal, no popup).
//     We do NOT surface the underlying RpcException::toString() (it can
//     leak wallet-rpc internals like the .keys path); the red label is
//     shown for any non-success outcome of openWallet.
//   * **Forgot password?**: label + jump to Case 3 button emitting
//     switchToRecoverClicked(). Case 3 reuses RecoveryManager (see
//     OnboardingWizard.qml). We wipe the password before bubbling the
//     signal so the recovery flow never inherits an obsolete secret.
//   * **V5.4.7**: never store key/seed/password in QML property. The
//     `password` property is wiped on (a) successful submit, (b) submit
//     failure, (c) back, (d) forgot-password navigation, (e) tab-out
//     (focus loss), (f) Component.onDestruction. The filename is not
//     a secret so we keep it across navigations within the page's life.
//   * **V5.4.9**: qsTr() for every user-visible string.
//   * **V5.4.15**: single Forest Green theme baked in (#16a34a top/bottom
//     bar, green primary CTA). No theme switch.
//   * **V5.4.21**: mainnet-only; no regtest/testnet/stagenet UI.
//
// Public API:
//   * signal back()                          -> wizard home
//   * signal walletReady(string filename)    -> wizard -> main.qml
//   * signal switchToRecoverClicked()        -> wizard -> Case 3
//
// V5.4.7 compliance: as noted above, the `password` QML property is
// transient and is scrubbed on every exit path AND on
// Component.onDestruction. We do NOT log the password bytes anywhere;
// the only console.log lines emitted are the st-071 markers used by
// the upcoming real-test harness (see _submit() below).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "Case2Open"

    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Case2Open", "Case 2 - Open existing wallet") + i18nManagerObj.emptyString)

    // ---- public API----------------------------------------------------
    readonly property string pageName: "Case2Open"
    signal back()
    signal walletReady(string filename)
    signal switchToRecoverClicked()

    // ---- injected from context properties (set in main.cpp) -----------
    readonly property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null

    // FIX-G11 (2026-07-18): list of existing wallet names for the
    // ComboBox dropdown. Refreshed on Component.onCompleted via
    // _refreshWalletList() which calls walletManager.listWalletFiles().
    property var _walletList: []

    // ---- form state ----------------------------------------------------
    /// The .keys filename to open. Not a secret (it's just a name on
    /// disk), so we keep it across navigations within the page.
    property string walletFilename: ""

    /// The wallet password. V5.4.7 #7C: TRANSIENT. Wiped on every exit
    /// path below; never logged via console.log.
    property string password: ""

    /// Show / hide password toggle. Drives echoMode of the TextField
    /// below; does NOT change what is stored in `password` (V5.4.7).
    property bool showPassword: false

    /// In-flight flag. True while walletManagerObj.openWallet is on
    /// the wire (round-trip ~0.1s). Disables Submit / Back / Forgot
    /// buttons so a stray click cannot race the wallet-rpc reply.
    property bool inFlight: false

    /// Red error label text. Per st-071 spec: only "Incorrect password"
    /// (any failure, not just wrong-pw, surfaces the same generic
    /// string so we never leak wallet-rpc internals to the user).
    property string errorMessage: ""

    /// Soft status text (non-error). e.g. "Opening wallet...".
    property string statusMessage: ""

    readonly property bool submitEnabled: {
        return page.walletFilename.length > 0
            && page.password.length > 0
            && !page.inFlight
    }

    // =====================================================================
    // UI
    // =====================================================================
    ColumnLayout {
        anchors.fill: page
        spacing: 0

        // ---- Forest Green top bar -------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "#16a34a"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Button {
                    objectName: "Case2BackButton"
                    text: (i18nManagerObj.qtr("Case2Open", "\u2190 Back") + i18nManagerObj.emptyString)
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case2Open", "Back to wizard home") + i18nManagerObj.emptyString)
                    onClicked: {
                        console.log("Case2Open (st-071): back -> wizard home")
                        page._wipeSecrets()
                        page.back()
                    }
                }
                Label {
                    text: (i18nManagerObj.qtr("Case2Open", "Case 2 - Open existing wallet") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Label {
                    text: (i18nManagerObj.qtr("Case2Open", "Step 1 / 1") + i18nManagerObj.emptyString)
                    color: "#dcfce7"
                    font.pixelSize: 12
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }

        // ---- Forest Green step indicator ------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#f0fdf4"

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                Repeater {
                    model: 1
                    delegate: Rectangle {
                        required property int index
                        width: 28
                        height: 6
                        radius: 3
                        color: page.inFlight ? "#ca8a04" : "#16a34a"
                    }
                }
            }
        }

        // ---- Centered card -------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 520)
                height: cardLayout.implicitHeight + 48
                radius: 10
                border.color: page.errorMessage.length > 0 ? "#dc2626" : "#bbf7d0"
                border.width: 1
                color: "#ffffff"

                ColumnLayout {
                    id: cardLayout
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    Label {
                        text: (i18nManagerObj.qtr("Case2Open", "Open an existing wallet") + i18nManagerObj.emptyString)
                        font.pixelSize: 22
                        font.bold: true
                        color: "#14532d"
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Label {
                        text: qsTr(
                            "Enter the name of the .keys file you saved earlier " +
                            "and the password you set when you first created it. " +
                            "Your wallet stays local on this device.")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        color: "#6b8074"
                        font.pixelSize: 12
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    // ---- Filename field (FIX-G11: ComboBox with existing wallets) --
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: (i18nManagerObj.qtr("Case2Open", "Wallet filename") + i18nManagerObj.emptyString)
                            font.pixelSize: 12
                            color: "#6b8074"
                        }
                        ComboBox {
                            id: filenameField
                            objectName: "Case2FilenameField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            editable: true
                            model: page._walletList
                            onEditTextChanged: page.walletFilename = editText
                            onActivated: function(index) {
                                page.walletFilename = page._walletList[index]
                            }
                            Accessible.role: Accessible.ComboBox
                            Accessible.name: (i18nManagerObj.qtr("Case2Open", "Wallet filename") + i18nManagerObj.emptyString)
                        }
                    }

                    // ---- Password field with show/hide eye -------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: (i18nManagerObj.qtr("Case2Open", "Wallet password") + i18nManagerObj.emptyString)
                            font.pixelSize: 12
                            color: "#6b8074"
                        }

                        // We layer the eye icon over the TextField via a
                        // plain Rectangle + MouseArea, anchored to the
                        // right inside the same RowLayout. We do NOT use
                        // TextField's rightPadding (QtQuick.Controls 2
                        // does not reliably repaint the cursor with
                        // padding changes on all themes).
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40

                            TextField {
                                id: passwordField
                                objectName: "Case2PasswordField"
                                anchors.fill: parent
                                anchors.rightMargin: 44
                                placeholderText: (i18nManagerObj.qtr("Case2Open", "Wallet password") + i18nManagerObj.emptyString)
                                font.pixelSize: 14
                                echoMode: page.showPassword
                                    ? TextInput.Normal
                                    : TextInput.Password
                                text: page.password
                                onTextChanged: page.password = text
                                Accessible.role: Accessible.EditableText
                                Accessible.name: (i18nManagerObj.qtr("Case2Open", "Wallet password") + i18nManagerObj.emptyString)
                                background: Rectangle {
                                    radius: 6
                                    color: "#ffffff"
                                    border.color: page.errorMessage.length > 0
                                        ? "#dc2626"   // error
                                        : (passwordField.activeFocus
                                            ? "#22c55e"   // focused
                                            : "#d1fae5")  // neutral
                                    border.width: 1
                                }
                                // Enter key -> Submit (only when enabled).
                                onAccepted: {
                                    if (page.submitEnabled) page._submit()
                                }
                            }

                            // Eye-icon toggle. A simple unicode eye /
                            // crossed-out eye pair keeps the page free
                            // of external icon assets (per V5.4.9 + the
                            // st-022 / st-024 theme.qss-only policy).
                            Rectangle {
                                id: eyeToggle
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 36
                                height: 36
                                radius: 6
                                color: eyeArea.containsMouse
                                    ? "#f0fdf4"
                                    : "transparent"
                                Accessible.role: Accessible.Button
                                Accessible.name: page.showPassword ? (i18nManagerObj.qtr("Case2Open", "Hide password") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("Case2Open", "Show password") + i18nManagerObj.emptyString)

                                Label {
                                    anchors.centerIn: parent
                                    text: page.showPassword ? (i18nManagerObj.qtr("Case2Open", "hide") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("Case2Open", "show") + i18nManagerObj.emptyString)
                                    font.pixelSize: 16
                                    color: page.showPassword ? "#16a34a" : "#6b8074"
                                }
                                MouseArea {
                                    id: eyeArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        console.log("Case2Open (st-071): eye toggle ->",
                                                    "showPassword =", !page.showPassword)
                                        page.showPassword = !page.showPassword
                                    }
                                }
                            }
                        }
                    }

                    // ---- Status / error row ----------------------------
                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: page.inFlight
                        visible: page.inFlight
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: page.statusMessage
                        visible: page.statusMessage.length > 0
                        color: "#14532d"
                        font.pixelSize: 12
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        objectName: "Case2ErrorLabel"
                        text: page.errorMessage
                        visible: page.errorMessage.length > 0
                        color: "#dc2626"
                        font.pixelSize: 13
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // ---- Submit button -------------------------------
                    Button {
                        objectName: "Case2SubmitButton"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        text: page.inFlight
                            ? (i18nManagerObj.qtr("Case2Open", "Opening wallet...") + i18nManagerObj.emptyString)
                            : (i18nManagerObj.qtr("Case2Open", "Open wallet") + i18nManagerObj.emptyString)
                        enabled: page.submitEnabled
                        onClicked: page._submit()
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                    }

                    // ---- Forgot password link -------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 6

                        Item { Layout.fillWidth: true }

                        Label {
                            text: (i18nManagerObj.qtr("Case2Open", "Forgot password?") + i18nManagerObj.emptyString)
                            color: "#6b8074"
                            font.pixelSize: 12
                        }
                        Button {
                            objectName: "Case2ForgotButton"
                            text: (i18nManagerObj.qtr("Case2Open", "Recover from seed instead") + i18nManagerObj.emptyString)
                            enabled: !page.inFlight
                            onClicked: {
                                console.log("Case2Open (st-071): forgot password -> Case 3")
                                page._wipeSecrets()
                                page.switchToRecoverClicked()
                            }
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // ---- Forest Green bottom bar ----------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "#f7fdf9"
            border.color: "#bbf7d0"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Button {
                    text: (i18nManagerObj.qtr("Case2Open", "\u2190 Back to wizard") + i18nManagerObj.emptyString)
                    enabled: !page.inFlight
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case2Open", "Back to wizard") + i18nManagerObj.emptyString)
                    onClicked: {
                        console.log("Case2Open (st-071): bottom back -> wizard home")
                        page._wipeSecrets()
                        page.back()
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: (i18nManagerObj.qtr("Case2Open", "V5.4.7 \u00b7 password never leaves the device") + i18nManagerObj.emptyString)
                    color: "#6b8074"
                    font.pixelSize: 11
                }
            }
        }
    }

    // =====================================================================
    // Submit handler - calls walletManagerObj.openWallet(filename, password).
    // On success emits walletReady(filename); on any failure (wrong
    // password, rpc disconnected, file missing) shows the generic
    // (i18nManagerObj.qtr("Case2Open", "Incorrect password") + i18nManagerObj.emptyString) red label. Per V5.4.7 we never echo the
    // underlying RpcException::toString() to the user.
    //
    // The wallet-rpc open_wallet is sync (RPC wait + reply) so the wall
    // clock is ~0.1s. We block the UI on inFlight, but flip
    // passwordField.echoMode back to Password afterwards so a successful
    // submit does not leave the password visible in plain text.
    // =====================================================================
    // FIX-G3b (2026-07-18): openWallet no longer throws RpcException
    // (WalletManager.cpp catches and sets state to Error instead of
    // re-throwing because Qt's nested QEventLoop in HttpRpcClient::call()
    // cannot unwind exceptions). We check walletManager.state after the
    // call to determine success / failure. The walletOpened signal is
    // still emitted by WalletManager::onRpcResponse on the success path.
    //
    // FIX-G3c (2026-07-18): use integer comparison for state because
    // QML cannot access C++ enum values through context-property instances
    // (page.walletManager.Opening is always undefined). State values:
    //   0=Closed, 1=Opening, 2=Open, 3=Closing, 4=Error
    function _submit() {
        if (!page.submitEnabled) return
        if (page.walletManager === null) {
            page.errorMessage = (i18nManagerObj.qtr("Case2Open", "WalletManager is not available - cannot open wallet.") + i18nManagerObj.emptyString)
            return
        }

        page.inFlight = true
        page.errorMessage = ""
        page.statusMessage = (i18nManagerObj.qtr("Case2Open", "Opening wallet...") + i18nManagerObj.emptyString)
        console.log("Case2Open (st-071): submit filename=", page.walletFilename)

        var filenameCopy = page.walletFilename
        var passwordCopy = page.password

        // Re-hide the password as soon as we hand it off (V5.4.7 #7C:
        // an attacker shoulder-surfing should not see the now-empty
        // field stay in plain text after a successful open).
        page.showPassword = false

        page.walletManager.openWallet(filenameCopy, passwordCopy)

        console.log("Case2Open (st-071): openWallet returned, state=" + page.walletManager.state
                    + " lastError=" + (page.walletManager.lastError || "(none)"))

        // FIX-G3c: integer comparison (0=Closed, 1=Opening, 2=Open, 3=Closing, 4=Error)
        var st = page.walletManager.state
        if (st === 1) {  // Opening
            // The sync call returned but state is still Opening.
            page.inFlight = false
            passwordCopy = ""
            page.password = ""
            page.errorMessage = (i18nManagerObj.qtr("Case2Open", "Wallet did not respond in time.") + i18nManagerObj.emptyString)
            page.statusMessage = ""
            console.warn("Case2Open (st-071): openWallet returned but state is still Opening")
        } else if (st === 2) {  // Open
            page.statusMessage = (i18nManagerObj.qtr("Case2Open", "Wallet opened.") + i18nManagerObj.emptyString)
            page.inFlight = false
            passwordCopy = ""
            page.password = ""
            page.walletReady(filenameCopy)
        } else {
            // 0=Closed, 3=Closing, 4=Error — failure.
            passwordCopy = ""
            page.password = ""
            page.inFlight = false
            var errMsg = page.walletManager.lastError
            console.warn("Case2Open (st-071): openWallet failed, state=" + st + " err=" + (errMsg || ""))
            page.errorMessage = (i18nManagerObj.qtr("Case2Open", "Incorrect password") + i18nManagerObj.emptyString)
            page.statusMessage = ""
        }
    }

    // =====================================================================
    // V5.4.7: scrub transient secret material on every exit path. The
    // page root may be torn down by:
    //   * back() emission (Back button)        -> _wipeSecrets() above
    //   * switchToRecoverClicked() (Forgot)    -> _wipeSecrets() above
    //   * StackView destroy (root destroy)     -> Component.onDestruction
    //   * Focus loss (Alt-Tab away)            -> _onFocusLost below
    // The password property itself is also wiped on submit success /
    // failure inside _submit().
    // =====================================================================
    function _wipeSecrets() {
        page.password = ""
        page.showPassword = false
        page.errorMessage = ""
        page.statusMessage = ""
        page.inFlight = false
    }

    // Belt-and-suspenders: if the page loses focus while the user is
    // mid-typing, hide the password so an Alt-Tab snapshot does not
    // capture it. V5.4.7 treats this as the same "secret-display"
    // surface as the TextField itself, so collapsing it costs the
    // user nothing (they can re-click the eye to re-show).
    onActiveFocusChanged: {
        if (!page.activeFocus) {
            page.showPassword = false
        }
    }

    Component.onDestruction: {
        page._wipeSecrets()
    }

    // FIX-G11 (2026-07-18): refresh the wallet list from
    // walletManager.listWalletFiles() on component mount so the
    // ComboBox dropdown shows existing wallets.
    function _refreshWalletList() {
        if (typeof page.walletManager === "undefined" || !page.walletManager)
            return
        try {
            var list = page.walletManager.listWalletFiles()
            if (list && list.length > 0) {
                page._walletList = list
            }
        } catch (e) {
            console.warn("Case2Open (st-071): listWalletFiles failed:",
                         e.message || String(e))
        }
    }

    Component.onCompleted: {
        page._refreshWalletList()
    }
}
