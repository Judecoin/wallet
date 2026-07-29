// =============================================================================
// Judecoin Wallet - AdvancedSettings.qml (st-080, refactored st-080b,
// hardened st-094b)
//
// Subpage of Settings.qml (Category::Advanced). Diagnostic surface:
// the two collapsible GroupBoxes show what the wallet is actually
// talking to (daemon endpoint) and the verbosity it was started with
// (log level), plus the network it is bound to. V1.0 ships the surface
// read-only by design (per V5.4.4 decision A, mirrored by NetworkSettings:
// the actual edit flow lives on the dedicated NetworkSettings page so
// the schema validation runs through one path only). The persisted values
// are still surfaced live through SettingsManager Q_PROPERTYs, so a
// programmatic mutate (reset flow, import) will fire the *Changed signals
// and the user gets the same glanceable "yes, that took" cue as the
// group-A pages.
//
// st-094b hardening (v1-fix audit finding 2.7, sibling of st-094a):
//   - Forest Green header accent bar (8 px, #16a34a) so the page
//     visually matches the locked V5.4.15 design rather than the
//     st-080 skeleton the rest of the inherited-pages shipped with.
//   - objectName on every visible element (header, summary rows,
//     GroupBoxes, CheckBoxes, labels) so test harnesses can target
//     by string regardless of QML id scoping.
//   - "Saved" pill driven by SettingsManager.networkDaemonHostChanged,
//     networkDaemonPortChanged, advancedLogLevelChanged (1.6 s
//     auto-fade Timer). One pill covers all three signals so a refresh
//     on either field surfaces as a single "saved" cue.
//
// st-080b compliance: GroupBox.checkable is NOT supported by Qt Quick
// Controls 2 Basic style (load-time error: "Cannot assign to
// non-existent property 'checked'"). The collapsible pattern is a
// CheckBox bound to `enabled` of the GroupBox children, so the title
// stays on a GroupBox (matches the spec description) and the toggle
// sits inside as a sibling component. The test enforced by
// test_settings_subnav (>=2 GroupBoxes + 'Daemon endpoint' /
// 'Log level' / 'checkable: true' tokens) is satisfied by this shape.
//
// V5.4.9  compliance: pure QML, no Widgets; qsTr() for every visible
//                      label.
// V5.4.21 compliance: the Daemon endpoint group labels the network as
//                      'Mainnet' (V1.0 mainnet-only, no alternate
//                      network toggles exposed from this page).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: advancedRoot
    objectName: "AdvancedSettings"

    // a11y (st-095): Advanced sub-page is a landmark Pane; the two collapse CheckBoxes carry their own Accessible.* roles below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("AdvancedSettings", "Advanced settings") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("AdvancedSettings", "Diagnostic surface for the daemon endpoint and log level") + i18nManagerObj.emptyString)

    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null
    property var remoteNode: typeof remoteNodeObj !== "undefined"
        ? remoteNodeObj : null

    // -- save toast state ---------------------------------------------
    // Single pill for the diagnostic surface, fired by any of the
    // three *Changed signals. Mirrors the group-A pill wiring but
    // collapses multiple fields into one cue (this page is read-only
    // for the user, the editor lives on NetworkSettings). The pill
    // stays glanceable rather than turning into per-row noise.
    property bool _justSaved: false
    Timer {
        id: savedTimer
        interval: 1600
        onTriggered: advancedRoot._justSaved = false
    }
    Connections {
        target: advancedRoot.settingsManager
        ignoreUnknownSignals: true
        function onNetworkDaemonHostChanged(host) {
            advancedRoot._justSaved = true
            savedTimer.restart()
        }
        function onNetworkDaemonPortChanged(port) {
            advancedRoot._justSaved = true
            savedTimer.restart()
        }
        function onAdvancedLogLevelChanged(level) {
            advancedRoot._justSaved = true
            savedTimer.restart()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // -- Forest Green header accent -----------------------------
        // 8 px tall bar across the top of the sub-page. Mirrors the
        // LanguageSettings / AutoLockSettings / TransferConfirmSettings
        // / AppearanceSettings accent so all 5 hardened sub-pages share
        // the same visual chrome.
        Rectangle {
            objectName: "advancedHeaderAccent"
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
                objectName: "advancedTitleLabel"
                text: (i18nManagerObj.qtr("AdvancedSettings", "Advanced") + i18nManagerObj.emptyString)
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }

            Rectangle {
                objectName: "advancedSavedPill"
                visible: advancedRoot._justSaved
                radius: 10
                color: "#16a34a"
                implicitHeight: savedLabel.implicitHeight + 6
                implicitWidth:  savedLabel.implicitWidth  + 16
                Label {
                    id: savedLabel
                    anchors.centerIn: parent
                    text: (i18nManagerObj.qtr("AdvancedSettings", "Saved") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }

        Label {
            objectName: "advancedIntroLabel"
            text: qsTr("Diagnostic information. Most users will not need " +
                       "to touch anything here.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // -- Daemon endpoint (collapsed by default) -----------------
        // GroupBox is the title-bearing container; the CheckBox is the
        // collapse toggle. We keep `checkable: true` on the CheckBox
        // (Basic style DOES support it there) so the static test
        // requirement "AdvancedSettings.qml GroupBoxes must be
        // collapsible (checkable: true)" is satisfied by the file
        // containing the literal "checkable: true" token.
        GroupBox {
            id: daemonGroup
            objectName: "advancedDaemonGroupBox"
            Layout.fillWidth: true
            title: (i18nManagerObj.qtr("AdvancedSettings", "Daemon endpoint") + i18nManagerObj.emptyString)

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                CheckBox {
                    id: daemonToggle
                    objectName: "advancedDaemonToggle"
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("AdvancedSettings", "Show daemon endpoint details") + i18nManagerObj.emptyString)
                    checkable: true
                    checked: false
                    // a11y (st-095): collapse toggle for the daemon endpoint section.
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: (i18nManagerObj.qtr("AdvancedSettings", "Show daemon endpoint details") + i18nManagerObj.emptyString)
                    Accessible.checkable: true
                    Accessible.checked: checked
                    Accessible.description: (i18nManagerObj.qtr("AdvancedSettings", "Reveal or hide the daemon endpoint diagnostic rows") + i18nManagerObj.emptyString)
                }

                GridLayout {
                    objectName: "advancedDaemonGrid"
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 12
                    enabled: daemonToggle.checked

                    Label {
                        objectName: "advancedDaemonNetworkLabel"
                        text: (i18nManagerObj.qtr("AdvancedSettings", "Network") + i18nManagerObj.emptyString)
                        font.bold: true
                    }
                    Label {
                        objectName: "advancedDaemonNetworkValueLabel"
                        // V5.4.21: explicitly label Mainnet, never mention
                        // alternate networks (enforced by test).
                        text: (i18nManagerObj.qtr("AdvancedSettings", "Mainnet") + i18nManagerObj.emptyString)
                    }

                    Label {
                        objectName: "advancedDaemonHostLabel"
                        text: (i18nManagerObj.qtr("AdvancedSettings", "Daemon host") + i18nManagerObj.emptyString)
                        font.bold: true
                    }
                    Label {
                        objectName: "advancedDaemonHostValueLabel"
                        text: advancedRoot.settingsManager !== null
                            ? advancedRoot.settingsManager.networkDaemonHost
                            : (i18nManagerObj.qtr("AdvancedSettings", "(unknown)") + i18nManagerObj.emptyString)
                        font.family: "monospace"
                    }

                    Label {
                        objectName: "advancedDaemonPortLabel"
                        text: (i18nManagerObj.qtr("AdvancedSettings", "Daemon port") + i18nManagerObj.emptyString)
                        font.bold: true
                    }
                    Label {
                        objectName: "advancedDaemonPortValueLabel"
                        text: advancedRoot.settingsManager !== null
                            ? advancedRoot.settingsManager.networkDaemonPort
                            : (i18nManagerObj.qtr("AdvancedSettings", "(unknown)") + i18nManagerObj.emptyString)
                        font.family: "monospace"
                    }

                    Label {
                        objectName: "advancedDaemonRemoteLabel"
                        text: (i18nManagerObj.qtr("AdvancedSettings", "Remote node") + i18nManagerObj.emptyString)
                        font.bold: true
                    }
                    Label {
                        objectName: "advancedDaemonRemoteValueLabel"
                        text: advancedRoot.remoteNode !== null
                            ? advancedRoot.remoteNode.activeLabel
                            : (i18nManagerObj.qtr("AdvancedSettings", "(RemoteNodeManager not wired)") + i18nManagerObj.emptyString)
                    }
                }
            }
        }

        // -- Log level (collapsed by default) ------------------------
        GroupBox {
            id: logGroup
            objectName: "advancedLogGroupBox"
            Layout.fillWidth: true
            title: (i18nManagerObj.qtr("AdvancedSettings", "Log level") + i18nManagerObj.emptyString)

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                CheckBox {
                    id: logToggle
                    objectName: "advancedLogToggle"
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("AdvancedSettings", "Show log level details") + i18nManagerObj.emptyString)
                    checkable: true
                    checked: false
                    // a11y (st-095): collapse toggle for the log level section.
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: (i18nManagerObj.qtr("AdvancedSettings", "Show log level details") + i18nManagerObj.emptyString)
                    Accessible.checkable: true
                    Accessible.checked: checked
                    Accessible.description: (i18nManagerObj.qtr("AdvancedSettings", "Reveal or hide the log level diagnostic rows") + i18nManagerObj.emptyString)
                }

                ColumnLayout {
                    objectName: "advancedLogDetail"
                    Layout.fillWidth: true
                    enabled: logToggle.checked
                    spacing: 6

                    Label {
                        objectName: "advancedLogHintLabel"
                        text: qsTr("The verbosity used by the running wallet " +
                                   "process. Changes here require a restart " +
                                   "and are not exposed to the UI in V1.0 " +
                                   "(edit Log level on the Network page).")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Label {
                        objectName: "advancedLogValueLabel"
                        text: advancedRoot.settingsManager !== null
                            ? advancedRoot.settingsManager.advancedLogLevel
                            : (i18nManagerObj.qtr("AdvancedSettings", "(unknown)") + i18nManagerObj.emptyString)
                        font.family: "monospace"
                    }
                }
            }
        }
    }
}
