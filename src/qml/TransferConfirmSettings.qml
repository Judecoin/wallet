// =============================================================================
// Judecoin Wallet - TransferConfirmSettings.qml (st-080, hardened st-094a)
//
// Subpage of Settings.qml (Category::TransferConfirm). Two independent
// switches per V5.4.4 decision C:
//   - transferConfirmDialog:    pop a confirmation dialog before every send
//   - transferConfirmPassword:  additionally require the wallet password
//
// st-094a hardening (v1-fix audit finding 2.7):
//   - Forest Green header accent bar (mirrors LanguageSettings.qml /
//     AutoLockSettings.qml hardening so all 3 group-A sub-pages share
//     the same visual chrome).
//   - Object names on the two Switches + status label so test harnesses
//     can target them by string regardless of QML id scoping.
//   - Per-switch "saved" micro-pill driven by the corresponding
//     SettingsManager signal so the user gets a glanceable confirmation
//     per toggle (rather than one global pill that races against the
//     second toggle).
//
// V5.4.9 compliance: pure QML, no Widgets; qsTr() for every visible label.
// V5.4.7 compliance: the password itself never enters this component.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: transferConfirmRoot
    objectName: "TransferConfirmSettings"

    // a11y (st-095): Transfer Confirm sub-page is a landmark Pane; the
    // two Switches carry their own Accessible.* roles below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("TransferConfirmSettings", "Transfer confirmation settings") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("TransferConfirmSettings", "Safety prompts before every send (confirmation dialog and password requirement)") + i18nManagerObj.emptyString)

    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null

    // -- save toast state ---------------------------------------------
    // Each toggle drives its own per-switch pill. _savedDialog and
    // _savedPassword are independent so toggling one does not stomp the
    // other's visual feedback.
    property bool _savedDialog:   false
    property bool _savedPassword: false
    Timer {
        id: dialogSavedTimer
        interval: 1600
        onTriggered: transferConfirmRoot._savedDialog = false
    }
    Timer {
        id: passwordSavedTimer
        interval: 1600
        onTriggered: transferConfirmRoot._savedPassword = false
    }
    Connections {
        target: transferConfirmRoot.settingsManager
        ignoreUnknownSignals: true
        function onTransferConfirmDialogChanged(on) {
            transferConfirmRoot._savedDialog = true
            dialogSavedTimer.restart()
        }
        function onTransferConfirmPasswordChanged(on) {
            transferConfirmRoot._savedPassword = true
            passwordSavedTimer.restart()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // -- Forest Green header accent -----------------------------
        Rectangle {
            objectName: "transferConfirmHeaderAccent"
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            color: "#16a34a"   // --storage / --accent-mid (theme.qss V5.4.15)
            radius: 2
        }

        Label {
            text: (i18nManagerObj.qtr("TransferConfirmSettings", "Transfer Confirmation") + i18nManagerObj.emptyString)
            font.bold: true
            font.pixelSize: 16
        }

        Label {
            text: qsTr("Pick the safety prompts you want before every send. " +
                       "Both can be enabled independently.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // -- Switch 1: show confirmation dialog -----------------------
        Frame {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("TransferConfirmSettings", "Show confirmation dialog before sending") + i18nManagerObj.emptyString)
                    wrapMode: Text.WordWrap
                }
                Switch {
                    id: dialogSwitch
                    objectName: "transferConfirmDialogSwitch"
                    enabled: transferConfirmRoot.settingsManager !== null
                    checked: transferConfirmRoot.settingsManager !== null
                        ? transferConfirmRoot.settingsManager.transferConfirmDialog
                        : true
                    // a11y (st-095): expose the dialog-confirmation switch
                    // as a CheckBox (binary on/off); name mirrors the visible
                    // label so screen readers announce it consistently.
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: (i18nManagerObj.qtr("TransferConfirmSettings", "Show confirmation dialog before sending") + i18nManagerObj.emptyString)
                    Accessible.checkable: true
                    Accessible.checked: checked
                    Accessible.description: (i18nManagerObj.qtr("TransferConfirmSettings", "Pop a confirmation dialog every time you send funds") + i18nManagerObj.emptyString)
                    onToggled: {
                        if (transferConfirmRoot.settingsManager === null) { return }
                        transferConfirmRoot.settingsManager.transferConfirmDialog = checked
                    }
                }
                Rectangle {
                    objectName: "transferConfirmDialogSavedPill"
                    visible: transferConfirmRoot._savedDialog
                    radius: 10
                    color: "#16a34a"
                    implicitHeight: dlgLabel.implicitHeight + 6
                    implicitWidth:  dlgLabel.implicitWidth  + 16
                    Label {
                        id: dlgLabel
                        anchors.centerIn: parent
                        text: (i18nManagerObj.qtr("TransferConfirmSettings", "Saved") + i18nManagerObj.emptyString)
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
        }

        // -- Switch 2: require wallet password -------------------------
        Frame {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                Label {
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("TransferConfirmSettings", "Require wallet password before sending") + i18nManagerObj.emptyString)
                    wrapMode: Text.WordWrap
                }
                Switch {
                    id: passwordSwitch
                    objectName: "transferConfirmPasswordSwitch"
                    enabled: transferConfirmRoot.settingsManager !== null
                    checked: transferConfirmRoot.settingsManager !== null
                        ? transferConfirmRoot.settingsManager.transferConfirmPassword
                        : true
                    // a11y (st-095): the password-required switch carries
                    // a CheckBox role; the name echoes the visible label.
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: (i18nManagerObj.qtr("TransferConfirmSettings", "Require wallet password before sending") + i18nManagerObj.emptyString)
                    Accessible.checkable: true
                    Accessible.checked: checked
                    Accessible.description: (i18nManagerObj.qtr("TransferConfirmSettings", "Require the wallet password every time you send funds") + i18nManagerObj.emptyString)
                    onToggled: {
                        if (transferConfirmRoot.settingsManager === null) { return }
                        transferConfirmRoot.settingsManager.transferConfirmPassword = checked
                    }
                }
                Rectangle {
                    objectName: "transferConfirmPasswordSavedPill"
                    visible: transferConfirmRoot._savedPassword
                    radius: 10
                    color: "#16a34a"
                    implicitHeight: pwdLabel.implicitHeight + 6
                    implicitWidth:  pwdLabel.implicitWidth  + 16
                    Label {
                        id: pwdLabel
                        anchors.centerIn: parent
                        text: (i18nManagerObj.qtr("TransferConfirmSettings", "Saved") + i18nManagerObj.emptyString)
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
        }
    }
}
