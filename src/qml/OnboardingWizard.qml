// =============================================================================
// Judecoin Wallet - OnboardingWizard.qml (st-022, st-119, st-120)
//
// Top-level wizard container. Hosts the 4 case-selection cards and routes
// the user into the matching detail page:
//   Case 1 - Create new wallet     (st-023 / Case1Create.qml)
//   Case 2 - Open existing wallet  (st-024 / Case2Open.qml)
//   Case 3 - Recover from seed     (st-012 / RecoveryManager / Case3Recover.qml)
//   Case 4 - Restore from backup   (st-022 / Case4BackupRestore + BackupParser)
//
// st-119 (2026-07-16): paint the wizard background with a Forest Green
// gradient (matches the Dashboard concept art in
// docs/image/ui-mockup-forest-green.jpg). Welcome subtitle rendered in
// mint text on the deep-green surface. 4 case cards restyled to large
// white cards (first option-A pick).
//
// st-120 (2026-07-16): refined st-119 — 4 case cards must be
// SMALL GREEN-TRANSPARENT BUTTONS (not large white cards), the
// top-left corner needs the Judecoin logo, and a top-right settings
// gear must pop up a compact advanced-settings dialog (remote node +
// language). Per-case-card labels are shortened to fit a 1x4 row:
//   "Create new wallet"     -> "Create"     (subtitle: "New wallet")
//   "Open existing wallet"  -> "Open"       (subtitle: "Existing wallet")
//   "Recover from seed"     -> "Recover"    (subtitle: "From seed")
//   "Restore from backup"   -> "Restore"    (subtitle: "From backup")
// The 2x2 GridLayout becomes a 1x4 RowLayout; the bottom
// "Item { Layout.fillHeight: true }" filler is removed (was the cause
// of the "recursive rearrange" warning).
//
// V5.4.9 compliance: pure QML, no Widgets.
// V5.4.7 compliance: no secret material is stored in any QML property;
// every page that handles keys forwards them straight to the C++ side
// (Case4BackupRestore forwards to wallet-rpc.generate_from_keys via
// WalletManager). The wizard never writes .keys files and never
// re-encrypts.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: wizard

    // a11y (st-095): the onboarding wizard is a landmark Pane; the
    // welcome label is a Heading, the 4 case cards are Buttons (see
    // CaseCard component), and the Case 4 loader is hidden from AT
    // (its child controls carry their own roles once mounted).
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("OnboardingWizard", "Onboarding wizard") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("OnboardingWizard", "Choose how to bring a wallet into this device: create, open, recover, or restore from backup") + i18nManagerObj.emptyString)

    // ---- public properties (set by main.qml context) -----------------
    // WalletManager (Q_INVOKABLE methods for restore + open + create).
    property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null
    // RecoveryManager (startRecovery / startRecoveryAsync).
    property var recoveryManager: typeof recoveryManagerObj !== "undefined"
        ? recoveryManagerObj : null
    // Optional descriptive startup state string from StartupManager.
    property string startupHint: typeof startupManager !== "undefined"
        ? startupManager.state : ""
    // st-120: Settings popup pulls the same context properties
    // Settings.qml uses — settingsManagerObj (QSettings-backed)
    // + remoteNodeObj (RemoteNodeManager for the host:port picker).
    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null
    property var remoteNode: typeof remoteNodeObj !== "undefined"
        ? remoteNodeObj : null

    // ---- internal state ----------------------------------------------
    property int selectedCase: 0  // 1..4
    property string currentPage: "home"   // st-022 / st-119 — 4-card home is the default entry point

    signal walletReady(string filename)

    // ---- bg: Forest Green gradient + background image (st-119, st-160) --
    // st-119: Forest Green gradient base, matching Dashboard concept art.
    // st-160 (2026-07-18): layer the custom homepage background
    // image (qrc:/homepage-bg.png) on top of the gradient via an Image
    // element with a translucent overlay. The gradient remains as a
    // fallback color so the page never reads as bare black if the image
    // fails to load. The image is anchored to fill the wizard and is
    // kept at z: -1 so it sits behind every other child.
    Rectangle {
        id: pageBg
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: "#1a4d2e" } // Forest Green top
            GradientStop { position: 1.0; color: "#0d2818" } // deep base
        }
    }
    // Background image painted on top of the gradient (slightly dimmed
    // so the text on top stays readable). Smooth scaling + aspect-fit
    // cropping via PreserveAspectCrop so the image fills the full
    // wizard area regardless of window size. Hidden if the resource
    // fails to load so the gradient remains as the visible backdrop.
    Image {
        id: bgImage
        anchors.fill: parent
        z: -1
        source: "qrc:/homepage-bg.png"
        fillMode: Image.PreserveAspectCrop
        smooth: true
        asynchronous: false
        cache: true
        opacity: 0.85
        // Dim overlay to keep the welcome text + cards readable.
        Rectangle {
            anchors.fill: parent
            color: "#0d2818"
            opacity: 0.35
        }
    }

    // ---- header bar (st-120) -----------------------------------------
    // Brand row: logo (qrc:/judecoin-logo.png, registered in
    // src/CMakeLists.txt via qt_add_resources brand_assets) +
    // "Judecoin" wordmark + settings gear (opens settingsPopup).
    // Anchored to the wizard's top so it stays put when homePage /
    // case4 swap in/out. z: 2 keeps the gear above any detail-page
    // overlay (Case 4's loader is anchored below the header).
    RowLayout {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 24
        spacing: 12
        z: 2

        Image {
            id: logoImage
            source: "qrc:/judecoin-logo.png"
            sourceSize.width: 40
            sourceSize.height: 40
            fillMode: Image.PreserveAspectFit
            smooth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: (i18nManagerObj.qtr("OnboardingWizard", "Judecoin logo") + i18nManagerObj.emptyString)
        }
        Label {
            text: (i18nManagerObj.qtr("OnboardingWizard", "Judecoin") + i18nManagerObj.emptyString)
            color: "#ffffff"
            font.pixelSize: 18
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        ToolButton {
            id: settingsBtn
            text: "\u2699"   // gear (⚙)
            implicitWidth: 40
            implicitHeight: 40
            onClicked: settingsPopup.open()
            contentItem: Label {
                text: parent.text
                color: "#ffffff"
                font.pixelSize: 22
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: parent.hovered ? Qt.rgba(0.133, 0.773, 0.369, 0.25) : "transparent"
                radius: 8
            }
            Accessible.role: Accessible.Button
            Accessible.name: (i18nManagerObj.qtr("OnboardingWizard", "Settings") + i18nManagerObj.emptyString)
            Accessible.description: (i18nManagerObj.qtr("OnboardingWizard", "Open advanced settings: remote node and language") + i18nManagerObj.emptyString)
        }
    }

    // ---- home: 4 case cards (st-119/st-120 compact green buttons) ----
    ColumnLayout {
        id: homePage
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        spacing: 20
        visible: wizard.currentPage === "home"

        // st-160 (2026-07-18): welcome title enlarged 2.5x
        // (22 -> 55) per homepage redesign request.
        Label {
            text: {
                let _ = i18nManagerObj.retranslateStamp;
                let result = i18nManagerObj.qtr("OnboardingWizard", "Welcome to Judecoin Wallet");
                console.log("Onboarding Welcome qtr result:", result, "stamp:", _);
                return result
            }
            font.pixelSize: 55
            font.weight: Font.DemiBold
            color: "#ffffff"
            // a11y (st-095): welcome title is a page Heading so AT
            // announces it as the section start.
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        // st-160 (2026-07-18): daemon status display removed
        // per homepage redesign request. Show only the static
        // helper line under the title.
        Label {
            text: (i18nManagerObj.qtr("OnboardingWizard", "Pick a path to continue.") + i18nManagerObj.emptyString)
            font.pixelSize: 14
            color: "#a8d5ba"  // mint text on the Forest Green bg
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            // a11y (st-095): static helper line under the title.
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // 4 case compact buttons in a single 1x4 RowLayout (st-120).
        // Pre-st-120 was a 2x2 grid of large white cards; per request
        // the cards must be small + green-transparent
        // buttons instead. CaseCard itself was re-skinned to match
        // (radius 10, rgba green fill, 1.5px #22c55e border).
        //
        // st-120d (2026-07-16 17:55): requested "smaller buttons,
        // border height a bit high; text bigger, no wrap". Each
        // CaseCard now has a fixed preferredHeight of 160 px
        // (was Layout.fillHeight -> the buttons were stretching to
        // 530 px each with 500 px of empty space). The RowLayout
        // stops fillHeight so the remaining vertical space sits below
        // the row (rather than inflating the buttons).
        //
        // st-160 (2026-07-18): Layout.topMargin set to -50 per
        // homepage redesign request - shift the cards row up
        // 50 px so it sits closer to the welcome title. Negative
        // topMargin collapses the spacing between the subtitle and
        // the cards row, lifting the entire row upward.
        RowLayout {
            spacing: 12
            Layout.fillWidth: true
            Layout.preferredHeight: 160
            Layout.topMargin: -50

            CaseCard {
                title: (i18nManagerObj.qtr("OnboardingWizard", "Create") + i18nManagerObj.emptyString)
                subtitle: (i18nManagerObj.qtr("OnboardingWizard", "New wallet") + i18nManagerObj.emptyString)
                badge: "1"
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                enabled: true
                onClicked: {
                    wizard.selectedCase = 1
                    wizard.currentPage = "case1"
                }
            }
            // st-160 (2026-07-18): highlight moved from Case 4
            // (Restore) to Case 2 (Open) per homepage redesign.
            CaseCard {
                title: (i18nManagerObj.qtr("OnboardingWizard", "Open") + i18nManagerObj.emptyString)
                subtitle: (i18nManagerObj.qtr("OnboardingWizard", "Existing wallet") + i18nManagerObj.emptyString)
                badge: "2"
                highlight: true   // st-160: highlight the 2nd card
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                enabled: true
                onClicked: {
                    wizard.selectedCase = 2
                    wizard.currentPage = "case2"
                }
            }
            CaseCard {
                title: (i18nManagerObj.qtr("OnboardingWizard", "Recover") + i18nManagerObj.emptyString)
                subtitle: (i18nManagerObj.qtr("OnboardingWizard", "From seed") + i18nManagerObj.emptyString)
                badge: "3"
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                enabled: true
                onClicked: {
                    wizard.selectedCase = 3
                    wizard.currentPage = "case3"
                    if (wizard.recoveryManager !== null) {
                        console.log("OnboardingWizard: Case 3 - delegate to RecoveryManager")
                    }
                }
            }
            // st-160 (2026-07-18): highlight removed from
            // Case 4 (Restore) and moved to Case 2 (Open).
            CaseCard {
                title: (i18nManagerObj.qtr("OnboardingWizard", "Restore") + i18nManagerObj.emptyString)
                subtitle: (i18nManagerObj.qtr("OnboardingWizard", "From backup") + i18nManagerObj.emptyString)
                badge: "4"
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                enabled: true
                onClicked: {
                    wizard.selectedCase = 4
                    wizard.currentPage = "case4"
                }
            }
        }
    }

    // ---- Case 1 detail page (anchored below header) -----------------
    // Case1Create is a 4-step internal wizard (WelcomePage ->
    // CreateOrRestorePage -> MnemonicPage -> SubmitPage) and self-handles
    // step navigation. We only need to forward walletReady(string) up to
    // main.qml via the existing notifyWalletReady() helper. The back()
    // signal is also wired to goHome() so the page-level "Back" button
    // returns to the wizard's 4-card home grid.
    Loader {
        id: case1Loader
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: wizard.currentPage === "case1"
        active: visible
        source: "qrc:/qml/Case1Create.qml"
        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("OnboardingWizard: Case1Create.qml failed to load:", sourceComponent.errorString())
            }
        }
        onLoaded: {
            if (item) {
                if (item.back) item.back.connect(wizard.goHome)
                if (item.walletReady) item.walletReady.connect(wizard.notifyWalletReady)
            }
        }
    }

    // ---- Case 2 detail page (anchored below header) -----------------
    // Case2Open is a single-step page (pick .keys file + password). Three
    // signals bubble up:
    //   back()                       -> wizard home (4-card grid)
    //   walletReady(string filename) -> main.qml mounts the sidebar shell
    //   switchToRecoverClicked()     -> jump to Case 3 (recovery flow)
    Loader {
        id: case2Loader
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: wizard.currentPage === "case2"
        active: visible
        source: "qrc:/qml/Case2Open.qml"
        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("OnboardingWizard: Case2Open.qml failed to load:", sourceComponent.errorString())
            }
        }
        onLoaded: {
            if (item) {
                if (item.back) item.back.connect(wizard.goHome)
                if (item.walletReady) item.walletReady.connect(wizard.notifyWalletReady)
                if (item.switchToRecoverClicked) {
                    item.switchToRecoverClicked.connect(function() {
                        wizard.selectedCase = 3
                        wizard.currentPage = "case3"
                    })
                }
            }
        }
    }

    // ---- Case 3 detail page (anchored below header) -----------------
    // Case3Recover is the seed-entry surface; the actual restore is driven
    // by RecoveryManager (wizard.recoveryManager context property). Three
    // signals bubble up:
    //   back()                       -> wizard home (4-card grid)
    //   walletReady(string filename) -> main.qml mounts the sidebar shell
    //   switchToOpenClicked()        -> jump to Case 2 (open existing)
    Loader {
        id: case3Loader
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: wizard.currentPage === "case3"
        active: visible
        source: "qrc:/qml/Case3Recover.qml"
        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("OnboardingWizard: Case3Recover.qml failed to load:", sourceComponent.errorString())
            }
        }
        onLoaded: {
            if (item) {
                if (item.back) item.back.connect(wizard.goHome)
                if (item.walletReady) item.walletReady.connect(wizard.notifyWalletReady)
                if (item.switchToOpenClicked) {
                    item.switchToOpenClicked.connect(function() {
                        wizard.selectedCase = 2
                        wizard.currentPage = "case2"
                    })
                }
            }
        }
    }

    // ---- Case 4 detail page (anchored below header) -----------------
    Loader {
        id: case4Loader
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: wizard.currentPage === "case4"
        active: visible
        source: "qrc:/qml/Case4BackupRestore.qml"
        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("OnboardingWizard: Case4BackupRestore.qml failed to load:", sourceComponent.errorString())
            }
        }
        onLoaded: {
            if (item) {
                if (item.back) item.back.connect(wizard.goHome)
                if (item.walletReady) item.walletReady.connect(wizard.notifyWalletReady)
            }
        }
    }

    // ---- settings popup (st-120, st-175 rewrite v3) -----------------
    // st-175 (2026-07-19): final clean rewrite. No ScrollView, no
    // dynamic height bindings — just anchors.fill + fixed heights on
    // each sub-section. Reliable, predictable.
    Popup {
        id: settingsPopup
        anchors.centerIn: Overlay.overlay
        width: Math.min(parent.width * 0.85, 640)
        height: Math.min(720, parent.height * 0.9)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#ffffff"
            radius: 16
            border.color: "#16a34a"
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // Header row: title + close (✕)
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: (i18nManagerObj.qtr("OnboardingWizard", "Settings") + i18nManagerObj.emptyString)
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#0d2818"
                    Layout.fillWidth: true
                }
                ToolButton {
                    text: "\u2715"
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: settingsPopup.close()
                    contentItem: Label {
                        text: parent.text
                        color: "#64748b"
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? "#f1f5f9" : "transparent"
                        radius: 6
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#e2e8f0"
            }

            Label {
                text: (i18nManagerObj.qtr("OnboardingWizard", "Remote Node") + i18nManagerObj.emptyString)
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: "#0d2818"
            }
            NetworkSettings {
                objectName: "OnboardingNetworkSettings"
                Layout.fillWidth: true
                Layout.preferredHeight: 380
            }

            Label {
                text: (i18nManagerObj.qtr("OnboardingWizard", "Language") + i18nManagerObj.emptyString)
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: "#0d2818"
            }
            LanguageSettings {
                objectName: "OnboardingLanguageSettings"
                Layout.fillWidth: true
                Layout.preferredHeight: 120
            }
        }
    }

    // ---- navigation helpers (called by the loaded page) -------------
    function goHome() {
        currentPage = "home"
        selectedCase = 0
    }
    function notifyWalletReady(filename) {
        walletReady(filename)
    }
}

// CaseCard definition moved to its own file: src/qml/CaseCard.qml.
// (Inline `component CaseCard : Rectangle` was rejected by the Qt 6.6.3
//  qml engine bundled in the AppImage; a top-level type in its own
//  .qml file works fine and is the QML-idiomatic way to share shapes
//  across the project.)
