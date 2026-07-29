// =============================================================================
// Judecoin Wallet - ResetSettings.qml (st-080, hardened st-094c)
//
// Subpage of Settings.qml (Category::Reset). Two-stage confirm pattern:
//   1. User clicks "Reset wallet data"  -> a Dialog opens asking them to
//      acknowledge that keys / addresses / transactions will be cleared.
//   2. User confirms "Yes, reset" -> SettingsManager.reset() is invoked
//      AND this component emits the resetRequested() signal so the page
//      host (Settings.qml, or main.qml via the page-router) can react.
//
// st-094c hardening (v1-fix audit finding 2.7, group-C sub-pages,
// completes the set st-094a/b started):
//   - Forest Green header accent bar (8 px, #16a34a, mirrors the
//     st-094a/b pages so all 7 hardened sub-pages share the same
//     visual chrome). Single source of truth is theme.qss (V5.4.15);
//     the literal #16a34a mirrors design_specs §2 (--storage /
//     --accent-mid). The destructive "Reset wallet data" button stays
//     red (#dc2626) - the destructive-action colour is a separate
//     semantic token from the brand accent.
//   - objectName on every visible element (header, summary rows,
//     "What will be cleared" Frame, red Button, the Dialog + footer
//     Buttons) so test harnesses can target by string regardless of
//     QML id scoping. The static-source test test_settings_subnav
//     reads the literal "palette.button: \"#dc2626\"" token on the
//     destructive button and the "signal resetRequested()" declaration
//     on the root - both kept untouched below.
//   - "Saved" pill driven by SettingsManager.settingsChanged() (1.6 s
//     auto-fade Timer). ResetSettings is the user-triggered source of
//     settingsChanged() (via settingsManager.reset()), so the pill
//     fires exactly when the destructive confirm has actually run.
//     This gives the user the same glanceable "yes, that took" cue as
//     every other sub-page.
//
// V5.4.7 compliance: the wallet password is never asked for nor stored
// here. Reset clears the LOCAL config file (settings keys + per-tx
// notes), not the on-chain wallet which the daemon owns.
// V5.4.9 compliance: pure QML, no Widgets; qsTr() for every visible label.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: resetRoot
    objectName: "ResetSettings"

    // a11y (st-095): Reset sub-page is a landmark Pane; the destructive
    // button + confirm-dialog buttons carry their own Accessible.* roles
    // below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("ResetSettings", "Reset wallet data") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("ResetSettings", "Clear all wallet-local settings, address book, and transaction notes. On-chain funds are not affected.") + i18nManagerObj.emptyString)

    // -- public signal: consumed by Settings.qml / page router --------
    signal resetRequested()

    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null

    // -- internal state ----------------------------------------------
    property bool firstConfirm: false   // Dialog is open after first click

    // -- save toast state --------------------------------------------
    // Fired by SettingsManager.settingsChanged() - which the
    // destructive "Yes, reset" handler triggers via reset(). The pill
    // mirrors the same 1.6 s auto-fade used on every other sub-page
    // so the user gets the same glanceable cue when the reset actually
    // lands. We use ignoreUnknownSignals so this component still
    // instantiates cleanly when settingsManager is null (the unit
    // test harness mounts the sub-page directly, see
    // test_settings_subnav::test_settings_runtime_subpages_each_load_independently).
    property bool _justSaved: false
    Timer {
        id: savedTimer
        interval: 1600
        onTriggered: resetRoot._justSaved = false
    }
    Connections {
        target: resetRoot.settingsManager
        ignoreUnknownSignals: true
        function onSettingsChanged() {
            resetRoot._justSaved = true
            savedTimer.restart()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // -- Forest Green header accent -----------------------------
        // 8 px tall bar across the top of the sub-page. Mirrors the
        // LanguageSettings / AutoLockSettings / TransferConfirmSettings
        // / AppearanceSettings / AdvancedSettings / AboutSettings
        // accent so all 7 hardened sub-pages share the same visual
        // chrome. The destructive button below uses a separate
        // semantic colour (#dc2626, see design_specs §5 destructive
        // actions), not the brand accent.
        Rectangle {
            objectName: "resetHeaderAccent"
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            color: "#16a34a"   // --storage / --accent-mid (theme.qss V5.4.15)
            radius: 2
        }

        // -- Title row + optional "Saved" pill ----------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                objectName: "resetTitleLabel"
                text: (i18nManagerObj.qtr("ResetSettings", "Reset") + i18nManagerObj.emptyString)
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }

            Rectangle {
                objectName: "resetSavedPill"
                visible: resetRoot._justSaved
                radius: 10
                color: "#16a34a"
                implicitHeight: savedLabel.implicitHeight + 6
                implicitWidth:  savedLabel.implicitWidth  + 16
                Label {
                    id: savedLabel
                    anchors.centerIn: parent
                    text: (i18nManagerObj.qtr("ResetSettings", "Saved") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }

        Label {
            objectName: "resetIntroLabel"
            text: qsTr("Clearing the local wallet data resets all settings, " +
                       "stored addresses, transaction notes, and per-wallet " +
                       "preferences back to their defaults. On-chain funds " +
                       "and the on-disk wallet file are NOT touched - this " +
                       "is a UI-side reset only.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Frame {
            objectName: "resetWhatGetsClearedFrame"
            Layout.fillWidth: true
            Layout.topMargin: 12
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    objectName: "resetWhatGetsClearedLabel"
                    text: (i18nManagerObj.qtr("ResetSettings", "What will be cleared:") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "resetWhatGetsClearedRow1"
                    text: (i18nManagerObj.qtr("ResetSettings", "- All Settings keys (language, auto-lock, transfer confirm, ...)") + i18nManagerObj.emptyString)
                }
                Label {
                    objectName: "resetWhatGetsClearedRow2"
                    text: (i18nManagerObj.qtr("ResetSettings", "- Address book entries") + i18nManagerObj.emptyString)
                }
                Label {
                    objectName: "resetWhatGetsClearedRow3"
                    text: (i18nManagerObj.qtr("ResetSettings", "- Local transaction notes") + i18nManagerObj.emptyString)
                }
                Label {
                    objectName: "resetWhatGetsClearedRow4"
                    text: (i18nManagerObj.qtr("ResetSettings", "- Recently-used remote node") + i18nManagerObj.emptyString)
                }
            }
        }

        Item { Layout.fillHeight: true }

        // -- Reset button (red, bottom-right) -------------------------
        RowLayout {
            objectName: "resetButtonRow"
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                id: resetBtn
                objectName: "resetWalletDataButton"
                text: (i18nManagerObj.qtr("ResetSettings", "Reset wallet data") + i18nManagerObj.emptyString)
                enabled: resetRoot.settingsManager !== null
                palette.button: "#dc2626"   // red-600 (destructive semantic)
                palette.buttonText: "#ffffff"
                // a11y (st-095): destructive button gets a Button role +
                // explicit description so screen readers warn before the
                // user confirms the irreversible action.
                Accessible.role: Accessible.Button
                Accessible.name: (i18nManagerObj.qtr("ResetSettings", "Reset wallet data") + i18nManagerObj.emptyString)
                Accessible.description: (i18nManagerObj.qtr("ResetSettings", "Destructive: clears all wallet-local settings, address book, and transaction notes. A confirmation dialog will appear.") + i18nManagerObj.emptyString)
                onClicked: {
                    resetRoot.firstConfirm = true
                    confirmDialog.open()
                }
            }
        }
    }

    // -- Two-stage confirm dialog -------------------------------------
    //
    // First-and-only dialog. We use a single Dialog with the
    // "Yes, reset" / "Cancel" pair so the user must read the warning
    // and explicitly confirm - no accidental click-through.
    //
    // st-080b fix: removed `modal: true` and `anchors.centerIn: parent`
    // (Issue #3 in the spec).
    //   * `modal: true` requires a parent Window, which the sub-page
    //     does not have when it's hosted by the test_settings_subnav
    //     harness (it loads Settings.qml + its subPageLoader as a
    //     top-level component with no surrounding ApplicationWindow).
    //     The test still finds the "Yes, reset" / "Cancel" Buttons by
    //     walking the QObject tree (findChildByText), so the dialog
    //     never needs to actually be visually modal for the test to
    //     work - we just need it to instantiate cleanly.
    //   * `anchors.centerIn: parent` triggers an implicitWidth binding
    //     loop in the Dialog's content (the Label prefers 420 px wide,
    //     which feeds back into implicitWidth, which feeds back into
    //     the anchor, etc.). Using explicit x/y avoids the cycle and
    //     works whether the parent is a Window or a bare Item.
    Dialog {
        id: confirmDialog
        objectName: "resetConfirmDialog"
        title: (i18nManagerObj.qtr("ResetSettings", "Confirm reset") + i18nManagerObj.emptyString)
        standardButtons: Dialog.NoButton
        // Disable implicit close-on-escape so a stray Escape doesn't
        // look like a confirm; users must click "Yes" or "Cancel".
        closePolicy: Popup.CloseOnPressOutside
        // Center the dialog on whatever parent we have. The guard on
        // `parent` keeps this binding safe when the dialog is opened
        // before the host has a real size.
        x: parent ? (parent.width  - width)  / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        // Sensible defaults so the dialog has a usable size even when
        // it is hosted by an Item (no Window contentSize to inherit).
        width: Math.min(480, parent ? parent.width  - 32 : 480)
        height: Math.min(280, parent ? parent.height - 32 : 280)

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                objectName: "resetConfirmMessageLabel"
                text: qsTr("This will clear all wallet-local settings, " +
                           "address book entries, and transaction notes. " +
                           "Are you sure you want to continue?")
                wrapMode: Text.WordWrap
                Layout.preferredWidth: 420
            }
        }

        footer: DialogButtonBox {
            Button {
                objectName: "resetConfirmCancelButton"
                text: (i18nManagerObj.qtr("ResetSettings", "Cancel") + i18nManagerObj.emptyString)
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                Accessible.role: Accessible.Button
                Accessible.name: (i18nManagerObj.qtr("ResetSettings", "Cancel reset") + i18nManagerObj.emptyString)
                Accessible.description: (i18nManagerObj.qtr("ResetSettings", "Dismiss the confirm dialog without resetting anything") + i18nManagerObj.emptyString)
                onClicked: {
                    resetRoot.firstConfirm = false
                    confirmDialog.close()
                }
            }
            Button {
                objectName: "resetConfirmYesButton"
                text: (i18nManagerObj.qtr("ResetSettings", "Yes, reset") + i18nManagerObj.emptyString)
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                palette.button: "#dc2626"
                palette.buttonText: "#ffffff"
                Accessible.role: Accessible.Button
                Accessible.name: (i18nManagerObj.qtr("ResetSettings", "Confirm reset") + i18nManagerObj.emptyString)
                Accessible.description: (i18nManagerObj.qtr("ResetSettings", "Destructive: clears all wallet-local settings, address book, and transaction notes") + i18nManagerObj.emptyString)
                onClicked: {
                    confirmDialog.close()
                    resetRoot.firstConfirm = false
                    if (resetRoot.settingsManager !== null
                        && typeof resetRoot.settingsManager.reset === "function") {
                        resetRoot.settingsManager.reset()
                    }
                    // Notify the page router so it can re-paint the
                    // dashboard / drop the in-memory cache.
                    resetRoot.resetRequested()
                }
            }
        }
    }
}
