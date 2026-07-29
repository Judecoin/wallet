// =============================================================================
// Judecoin Wallet - Case1Create.qml (st-152)
//
// Case 1 (Create new wallet) wizard. Two-step wizard:
//
//   step 0 (IntroPage)     - welcome text, "Next" CTA
//   step 1 (PasswordPage)  - password + confirm + strength hint, "Create" CTA
//   Post-create result     - seed display (from getMnemonicSeed) with Copy +
//                            "Enter Wallet" button, emits walletReady
//
// Design (st-152, 2026-07-18):
//   * Removed client-side BIP39 word generation — the old flow showed
//     random placeholder words that didn't match the server-generated
//     monero seed, making recovery impossible.
//   * New flow: createWallet() → getMnemonicSeed() (via query_key RPC) →
//     display the REAL server-generated seed with Copy button.
//   * No retype confirmation needed (server-generated seed is trustworthy).
//   * Two-step simplification: intro → password → create → show seed.
//
// V5.4.7 compliance: password + seed live only as transient QML properties.
// V5.4.9 compliance: every visible string is qsTr() wrapped.
// V5.4.15 compliance: single Forest Green theme baked in (#16a34a).
// V5.4.21 compliance: mainnet-only.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "Case1Create"

    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Case 1 - Create new wallet") + i18nManagerObj.emptyString)

    // ---- public API -----------------------------------------------------
    readonly property string pageName: "Case1Create"
    signal back()
    signal walletReady(string filename)

    // ---- seed readiness gate (P0 #11 fix, 2026-07-23) --------------
    // seedReady is true ONLY when we have exactly 25 words from the
    // server. This prevents advancement to step 2 (seed display) and
    // disables the "Enter Wallet" button when the seed wasn't properly
    // fetched (e.g. getMnemonicSeed() threw, returned empty, or returned
    // fewer than 25 words).
    readonly property bool seedReady: page.seedWords.length === page.seedWordCount

    // ---- injected from context properties (set in main.cpp) ---------
    readonly property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null

    // ---- seed state (st-152: populated AFTER creation by getMnemonicSeed())
    property var seedWords: []
    readonly property int seedWordCount: 25
    property string seedText: ""  // joined seed for display + copy

    // ---- clipboard sink (hidden TextEdit for copy()) ------------------
    TextEdit {
        id: clipSink
        visible: false
        width: 0
        height: 0
    }
    property string lastCopyStatus: ""
    Timer {
        id: copyStatusTimer
        interval: 2000
        onTriggered: page.lastCopyStatus = ""
    }

    function copySeedToClipboard() {
        clipSink.text = page.seedText
        clipSink.selectAll()
        clipSink.copy()
        page.lastCopyStatus = (i18nManagerObj.qtr("Case1Create", "Copied %1 words to clipboard").arg(page.seedWords.length) + i18nManagerObj.emptyString)
                    copyStatusTimer.restart()
    }

    // ---- wizard navigation ---------------------------------------------
    property int currentStep: 0  // 0=intro, 1=password; 2=result (post-create)
    readonly property int stepCount: 2

    // ---- password form state -------------------------------------------
    property string password: ""
    property string passwordConfirm: ""

    readonly property int passwordStrength: {
        var p = page.password || ""
        if (p.length === 0) return 0
        if (p.length < 8) return 0
        var hasDigit = /\d/.test(p)
        var hasUpper = /[A-Z]/.test(p)
        var hasLower = /[a-z]/.test(p)
        var hasSymbol = /[^A-Za-z0-9]/.test(p)
        var distinct = (hasDigit ? 1 : 0) + (hasUpper ? 1 : 0)
                     + (hasLower ? 1 : 0) + (hasSymbol ? 1 : 0)
        if (distinct === 4) return 3
        if (distinct >= 2) return 2
        return 1
    }

    // st-152: submit enabled = just password gate (no retype, no seed grid)
    readonly property bool submitEnabled: {
        return page.password.length >= 8
            && page.passwordConfirm.length >= 8
            && page.password === page.passwordConfirm
            && !page.inFlight
    }

    property bool inFlight: false
    property string statusMessage: ""
    property string errorMessage: ""

    readonly property string generatedFilename:
        "wallet-" + Math.floor(Date.now() / 1000).toString()

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
                    text: (i18nManagerObj.qtr("Case1Create", "\u2190 Back") + i18nManagerObj.emptyString)
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Back to wizard home") + i18nManagerObj.emptyString)
                    onClicked: {
                        page.password = ""
                        page.passwordConfirm = ""
                        page.errorMessage = ""
                        page.seedWords = []
                        page.seedText = ""
                        page.currentStep = 0
                        page.back()
                    }
                }
                Label {
                    text: (i18nManagerObj.qtr("Case1Create", "Case 1 - Create new wallet") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Label {
                    text: (i18nManagerObj.qtr("Case1Create", "Step %1 / 2").arg(Math.min(page.currentStep + 1, 2) + i18nManagerObj.emptyString))
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    color: "#dcfce7"
                    font.pixelSize: 12
                }
            }
        }

        // ---- Forest Green step indicator (2 steps) --------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#f0fdf4"

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                Repeater {
                    model: 2
                    delegate: Rectangle {
                        required property int index
                        width: 28
                        height: 6
                        radius: 3
                        color: index <= page.currentStep ? "#16a34a" : "#bbf7d0"
                    }
                }
            }
        }

        // ---- Content area: StackLayout or result page ------------------
        StackLayout {
            id: steps
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.currentStep < 2 ? page.currentStep : 2

            // Step 0: Intro page — centered card matching Case2Open.qml chrome
            Item {
                Item {
                    anchors.fill: parent
                    Item {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 520)
                        height: introCardLayout.implicitHeight + 48

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            border.color: "#bbf7d0"
                            border.width: 1
                            color: "#ffffff"
                        }

                        ColumnLayout {
                            id: introCardLayout
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 14

                            Label {
                                text: (i18nManagerObj.qtr("Case1Create", "Create a new wallet") + i18nManagerObj.emptyString)
                                font.pixelSize: 22
                                font.bold: true
                                color: "#14532d"
                                Accessible.role: Accessible.Heading
                                Accessible.name: text
                            }
                            Label {
                                text: qsTr(
                                    "This wizard will create a new wallet for you. " +
                                    "After creation you will be shown a 25-word recovery " +
                                    "phrase — write it down and keep it safe.\n\n" +
                                    "Anyone with the recovery phrase can spend your funds.")
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                color: "#6b8074"
                                font.pixelSize: 12
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }
                        }
                    }
                }
            }

            // Step 1: Password page — centered card matching Case2Open.qml chrome
            Item {
                Item {
                    anchors.fill: parent
                    Item {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 520)
                        height: passwordCardLayout.implicitHeight + 48

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            border.color: page.errorMessage.length > 0 ? "#dc2626" : "#bbf7d0"
                            border.width: 1
                            color: "#ffffff"
                        }

                        ColumnLayout {
                            id: passwordCardLayout
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 14

                            Label {
                                text: (i18nManagerObj.qtr("Case1Create", "Create a wallet password") + i18nManagerObj.emptyString)
                                font.pixelSize: 22
                                font.bold: true
                                color: "#14532d"
                                Accessible.role: Accessible.Heading
                                Accessible.name: text
                            }
                            Label {
                                text: qsTr(
                                    "Choose a password to encrypt your wallet. " +
                                    "You will need this password every time you open the wallet. " +
                                    "There is no password reset — if you lose it you cannot recover " +
                                    "the wallet (even with the recovery phrase).")
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                color: "#6b8074"
                                font.pixelSize: 12
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: (i18nManagerObj.qtr("Case1Create", "Wallet password") + i18nManagerObj.emptyString)
                                    font.pixelSize: 12
                                    color: "#6b8074"
                                }
                                TextField {
                                    id: passwordField
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    placeholderText: (i18nManagerObj.qtr("Case1Create", "at least 8 characters") + i18nManagerObj.emptyString)
                                    echoMode: TextInput.Password
                                    text: page.password
                                    onTextChanged: page.password = text
                                    Accessible.role: Accessible.EditableText
                                    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Wallet password") + i18nManagerObj.emptyString)
                                }
                                Label {
                                    text: {
                                        switch (page.passwordStrength) {
                                            case 3: return (i18nManagerObj.qtr("Case1Create", "Strength: strong") + i18nManagerObj.emptyString)
                                            case 2: return (i18nManagerObj.qtr("Case1Create", "Strength: medium") + i18nManagerObj.emptyString)
                                            case 1: return (i18nManagerObj.qtr("Case1Create", "Strength: weak") + i18nManagerObj.emptyString)
                                            default: return (i18nManagerObj.qtr("Case1Create", "Strength: too short") + i18nManagerObj.emptyString)
                                        }
                                    }
                                    font.pixelSize: 11
                                    color: {
                                        switch (page.passwordStrength) {
                                            case 3: return "#16a34a"
                                            case 2: return "#ca8a04"
                                            case 1: return "#dc2626"
                                            default: return "#6b8074"
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: (i18nManagerObj.qtr("Case1Create", "Confirm password") + i18nManagerObj.emptyString)
                                    font.pixelSize: 12
                                    color: "#6b8074"
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    placeholderText: (i18nManagerObj.qtr("Case1Create", "retype password") + i18nManagerObj.emptyString)
                                    echoMode: TextInput.Password
                                    text: page.passwordConfirm
                                    onTextChanged: page.passwordConfirm = text
                                    Accessible.role: Accessible.EditableText
                                    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Confirm wallet password") + i18nManagerObj.emptyString)
                                }
                                Label {
                                    text: page.passwordConfirm.length === 0
                                        ? (i18nManagerObj.qtr("Case1Create", " ") + i18nManagerObj.emptyString)
                                        : (page.password === page.passwordConfirm
                                            ? (i18nManagerObj.qtr("Case1Create", "Passwords match") + i18nManagerObj.emptyString)
                                            : (i18nManagerObj.qtr("Case1Create", "Passwords do not match") + i18nManagerObj.emptyString))
                                    font.pixelSize: 11
                                    color: page.passwordConfirm.length === 0
                                        ? "#6b8074"
                                        : (page.password === page.passwordConfirm ? "#16a34a" : "#dc2626")
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: page.errorMessage
                                color: "#dc2626"
                                visible: page.errorMessage.length > 0
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: page.statusMessage
                                color: "#14532d"
                                visible: page.statusMessage.length > 0
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }

            // Step 2: Post-create result page (seed display) — centered card
            // st-160 (2026-07-18): card widened from 520 to 760
            // so the 5x5 seed grid has room for the longest BIP39 words
            // (e.g. "acoustic", "wisdom", "zero" at 14px monospace) without
            // eliding. Content area is ~920px (window 1100 - sidebar 180)
            // so 760 leaves 80px of breathing room on each side.
            Item {
                Item {
                    anchors.fill: parent
                    ScrollView {
                        anchors.fill: parent
                        clip: true
                        Item {
                            anchors.fill: parent
                            Item {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 48, 760)
                                height: seedCardLayout.implicitHeight + 48

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 10
                                    border.color: "#bbf7d0"
                                    border.width: 1
                                    color: "#ffffff"
                                }

                                ColumnLayout {
                                    id: seedCardLayout
                                    anchors.fill: parent
                                    anchors.margins: 24
                                    spacing: 14

                                    Label {
                                        text: (i18nManagerObj.qtr("Case1Create", "Your recovery phrase") + i18nManagerObj.emptyString)
                                        font.pixelSize: 22
                                        font.bold: true
                                        color: "#14532d"
                                        Accessible.role: Accessible.Heading
                                        Accessible.name: text
                                    }
                                    Label {
                                        text: qsTr(
                                            "Write these 25 words down on paper and keep them safe. " +
                                            "Anyone with these words can spend your funds. " +
                                            "Do not store them digitally.")
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                        color: "#6b8074"
                                        font.pixelSize: 12
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: text
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 230
                                        radius: 6
                                        border.color: "#bbf7d0"
                                        border.width: 1
                                        color: "#f7fdf9"

                                        GridLayout {
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            columns: 5
                                            columnSpacing: 8
                                            rowSpacing: 6

                                            Repeater {
                                                model: page.seedWords.length
                                                delegate: Rectangle {
                                                    required property int index
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 32
                                                    radius: 4
                                                    color: index % 2 === 0 ? "#ffffff" : "#ecfdf5"

                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 6
                                                        anchors.rightMargin: 6
                                                        spacing: 4

                                                        Label {
                                                            text: (index + 1).toString() + "."
                                                            font.pixelSize: 12
                                                            color: "#6b8074"
                                                            Layout.preferredWidth: 18
                                                        }
                                                        Label {
                                                            text: page.seedWords[index] || ""
                                                            font.family: "monospace"
                                                            font.pixelSize: 14
                                                            color: "#14532d"
                                                            Layout.fillWidth: true
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Button {
                                            text: (i18nManagerObj.qtr("Case1Create", "Copy to clipboard") + i18nManagerObj.emptyString)
                                            enabled: page.seedWords.length > 0
                                            Accessible.role: Accessible.Button
                                            Accessible.name: (i18nManagerObj.qtr("Case1Create", "Copy recovery phrase to clipboard") + i18nManagerObj.emptyString)
                                            onClicked: page.copySeedToClipboard()
                                        }
                                        Label {
                                            text: page.lastCopyStatus
                                            color: "#16a34a"
                                            font.pixelSize: 12
                                            visible: page.lastCopyStatus.length > 0
                                            Accessible.role: Accessible.StaticText
                                            Accessible.name: text
                                        }
                                        Item { Layout.fillWidth: true }
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: page.statusMessage
                                        color: "#14532d"
                                        visible: page.statusMessage.length > 0
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: page.errorMessage
                                        color: "#dc2626"
                                        visible: page.errorMessage.length > 0
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Bottom navigation bar (matches Case2Open.qml chrome) ----
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
                    text: (i18nManagerObj.qtr("Case1Create", "\u2190 Back") + i18nManagerObj.emptyString)
                    enabled: page.currentStep === 1 && !page.inFlight
                    visible: page.currentStep !== 2
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Back") + i18nManagerObj.emptyString)
                    onClicked: {
                        if (page.currentStep > 0) page.currentStep -= 1
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: (i18nManagerObj.qtr("Case1Create", "V5.4.7 \u00b7 password never leaves the device") + i18nManagerObj.emptyString)
                    color: "#6b8074"
                    font.pixelSize: 11
                    visible: page.currentStep !== 2
                }
                Button {
                    text: (i18nManagerObj.qtr("Case1Create", "Next \u2192") + i18nManagerObj.emptyString)
                    visible: page.currentStep === 0
                    enabled: !page.inFlight
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Next step") + i18nManagerObj.emptyString)
                    onClicked: page.currentStep = 1
                }
                Button {
                    objectName: "Case1SubmitButton"
                    text: page.inFlight ? (i18nManagerObj.qtr("Case1Create", "Creating...") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("Case1Create", "Create wallet") + i18nManagerObj.emptyString)
                    visible: page.currentStep === 1
                    enabled: page.submitEnabled && !page.inFlight
                    onClicked: page._submit()
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                }
                Button {
                    objectName: "Case1EnterWalletButton"
                    text: (i18nManagerObj.qtr("Case1Create", "Enter Wallet \u2192") + i18nManagerObj.emptyString)
                    visible: page.currentStep === 2
                    enabled: page.seedReady
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case1Create", "Enter wallet") + i18nManagerObj.emptyString)
                    onClicked: {
                        var fn = page.generatedFilename
                        page.walletReady(fn)
                    }
                }
            }
        }
    }

    // =====================================================================
    // Submit handler (st-152): createWallet → getMnemonicSeed → show result
    // =====================================================================
    function _submit() {
        if (!page.submitEnabled) return
        if (page.walletManager === null) {
            page.errorMessage = (i18nManagerObj.qtr("Case1Create", "WalletManager is not available - cannot create wallet.") + i18nManagerObj.emptyString)
            return
        }

        page.inFlight = true
        page.errorMessage = ""
        page.statusMessage = (i18nManagerObj.qtr("Case1Create", "Creating wallet...") + i18nManagerObj.emptyString)
        console.log("Case1Create (P0#11 v1): submit filename=", page.generatedFilename)

        var passwordCopy = page.password
        page.password = ""
        page.passwordConfirm = ""

        try {
            // Step A: create wallet (server-side seed)
            // P0 #11 fix: if this succeeds but getMnemonicSeed() later
            // fails, we MUST NOT advance to step 2 — the wallet is
            // permanently unrecoverable without the seed.
            var fname = page.walletManager.createWallet(
                page.generatedFilename, passwordCopy, "English")

            // st-151: fetch real server-generated seed
            page.statusMessage = (i18nManagerObj.qtr("Case1Create", "Fetching recovery phrase...") + i18nManagerObj.emptyString)
            try {
                var realSeed = page.walletManager.getMnemonicSeed()
                if (realSeed && realSeed.length > 0) {
                    var words = realSeed.split(/[\s,]+/)
                        .filter(function(w) { return w.length > 0 })
                    // P0 #11 fix: validate word count (must be exactly 25).
                    // A partial seed is as dangerous as no seed — the
                    // user would think they have a recovery phrase when
                    // it's actually truncated.
                    if (words.length === page.seedWordCount) {
                        page.seedText = realSeed
                        page.seedWords = words
                        clipSink.text = realSeed
                        console.log("Case1Create (P0#11 v1): got real seed, " +
                                    words.length + " words")
                    } else {
                        console.warn("Case1Create (P0#11 v1): seed word count mismatch, got " +
                                     words.length + " words, expected " + page.seedWordCount)
                    }
                }
            } catch (e2) {
                console.warn("Case1Create (P0#11 v1): getMnemonicSeed failed:",
                             String(e2 && e2.message ? e2.message : e2))
            }

            page.inFlight = false
            passwordCopy = ""

            // P0 #11 fix: only advance to seed display if we actually
            // have all 25 words. Without this gate, a failed
            // getMnemonicSeed() would still land the user on step 2
            // with an empty (or incomplete) seed grid, making the
            // wallet permanently unrecoverable if the .keys file is lost.
            if (page.seedReady) {
                page.currentStep = 2  // show seed page
                page.statusMessage = (i18nManagerObj.qtr("Case1Create", "Wallet created. Write down the recovery phrase.") + i18nManagerObj.emptyString)
            } else {
                page.currentStep = 1  // stay on password page
                page.statusMessage = ""
                page.errorMessage = (i18nManagerObj.qtr("Case1Create", "create_wallet: Could not retrieve the recovery phrase from the daemon. The wallet was created but without the phrase you cannot recover your funds if the file is lost. Please go back, delete the wallet file, and try creating again.") + i18nManagerObj.emptyString)
            }
        } catch (e) {
            passwordCopy = ""
            page.inFlight = false
            page.currentStep = 1
            var msg = (e && e.message) ? String(e.message) : String(e)
            page.errorMessage = (i18nManagerObj.qtr("Case1Create", "create_wallet failed: %1").arg(msg) + i18nManagerObj.emptyString)
                    page.statusMessage = ""
        }
    }

    // =====================================================================
    // V5.4.7: scrub transient secret material on teardown
    // =====================================================================
    Component.onDestruction: {
        page.password = ""
        page.passwordConfirm = ""
        page.seedWords = []
        page.seedText = ""
        page.errorMessage = ""
        page.statusMessage = ""
    }
}
