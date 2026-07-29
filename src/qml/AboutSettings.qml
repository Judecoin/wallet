// =============================================================================
// Judecoin Wallet - AboutSettings.qml (st-080, hardened st-094c)
//
// Subpage of Settings.qml (Category::About). Read-only summary panel
// showing the wallet version, Qt runtime version, and the bundled
// daemon binary version (when known). All values come from
// SettingsManager (aboutVersion / aboutQtVersion / aboutDaemonVersion).
//
// st-094c hardening (v1-fix audit finding 2.7, group-C sub-pages,
// completes the set st-094a/b started):
//   - Forest Green header accent bar (8 px, #16a34a, mirrors the
//     st-094a/b pages so all 7 hardened sub-pages share the same
//     visual chrome). Single source of truth is theme.qss (V5.4.15);
//     the literal #16a34a mirrors design_specs §2 (--storage /
//     --accent-mid).
//   - objectName on every visible element (header, version rows,
//     license labels, support footer) so test harnesses can target
//     by string regardless of QML id scoping.
//   - "Saved" pill driven by SettingsManager.aboutDaemonVersionChanged
//     (1.6 s auto-fade Timer). The wallet and Qt versions are
//     CONSTANT (build-time values, set in src/main.cpp), but the
//     daemon version is set asynchronously by StartupManager once the
//     bundled binary boots; the pill is the user's signal that the
//     wallet has finished discovering the daemon version.
//   - License label corrected from "MIT" to "BSD 3-Clause" to match
//     the actual project LICENSE file (st-094c audit 2.7 follow-up,
//     the prior placeholder text had drifted from the source-of-truth
//     SPDX line).
//
// V5.4.9  compliance: pure QML, no Widgets; qsTr() for every visible
//                      label.
// V5.4.21 compliance: Network row is the literal string "Mainnet";
//                      no alternate-network toggle is exposed from
//                      this page.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: aboutRoot
    objectName: "AboutSettings"

    // a11y (st-095): About sub-page is a landmark Pane; the page has no
    // interactive controls but the landmark lets AT jump here directly
    // and announce the version grid.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("AboutSettings", "About") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("AboutSettings", "Wallet version, build info, and project attributions") + i18nManagerObj.emptyString)

    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null

    // -- save toast state ---------------------------------------------
    // Driven by aboutDaemonVersionChanged (the one *Changed signal
    // on this page). Wallet and Qt versions are CONSTANT so they
    // never fire; the daemon version is set by StartupManager once
    // the bundled binary boots, and that is the moment we want to
    // flash the "we have a value now" cue.
    property bool _justSaved: false
    Timer {
        id: savedTimer
        interval: 1600
        onTriggered: aboutRoot._justSaved = false
    }
    Connections {
        target: aboutRoot.settingsManager
        ignoreUnknownSignals: true
        function onAboutDaemonVersionChanged(version) {
            aboutRoot._justSaved = true
            savedTimer.restart()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // -- Forest Green header accent -----------------------------
        // 8 px tall bar across the top of the sub-page. Mirrors the
        // LanguageSettings / AutoLockSettings / TransferConfirmSettings
        // / AppearanceSettings / AdvancedSettings accent so all 7
        // hardened sub-pages share the same visual chrome.
        Rectangle {
            objectName: "aboutHeaderAccent"
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
                objectName: "aboutTitleLabel"
                text: (i18nManagerObj.qtr("AboutSettings", "About") + i18nManagerObj.emptyString)
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }

            Rectangle {
                objectName: "aboutSavedPill"
                visible: aboutRoot._justSaved
                radius: 10
                color: "#16a34a"
                implicitHeight: savedLabel.implicitHeight + 6
                implicitWidth:  savedLabel.implicitWidth  + 16
                Label {
                    id: savedLabel
                    anchors.centerIn: parent
                    text: (i18nManagerObj.qtr("AboutSettings", "Saved") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }

        // -- versions grid ------------------------------------------
        // 2x4 GridLayout satisfies the st-094c contract (test reads
        // 'columns: 2' token + the four version labels).
        Frame {
            objectName: "aboutVersionsFrame"
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 6
                columnSpacing: 12

                Label {
                    objectName: "aboutWalletVersionLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Wallet version") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "aboutWalletVersionValueLabel"
                    text: aboutRoot.settingsManager !== null
                        ? aboutRoot.settingsManager.aboutVersion
                        : (i18nManagerObj.qtr("AboutSettings", "(unknown)") + i18nManagerObj.emptyString)
                    font.family: "monospace"
                }

                Label {
                    objectName: "aboutQtVersionLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Qt runtime") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "aboutQtVersionValueLabel"
                    text: aboutRoot.settingsManager !== null
                        ? aboutRoot.settingsManager.aboutQtVersion
                        : (i18nManagerObj.qtr("AboutSettings", "(unknown)") + i18nManagerObj.emptyString)
                    font.family: "monospace"
                }

                Label {
                    objectName: "aboutDaemonVersionLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Daemon version") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "aboutDaemonVersionValueLabel"
                    text: aboutRoot.settingsManager !== null
                        ? aboutRoot.settingsManager.aboutDaemonVersion
                        : (i18nManagerObj.qtr("AboutSettings", "(not yet detected)") + i18nManagerObj.emptyString)
                    font.family: "monospace"
                }

                Label {
                    objectName: "aboutNetworkLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Network") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "aboutNetworkValueLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Mainnet") + i18nManagerObj.emptyString)
                }
            }
        }

        // -- license / project card ---------------------------------
        Frame {
            objectName: "aboutLicenseFrame"
            Layout.fillWidth: true
            Layout.topMargin: 8
            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label {
                    objectName: "aboutProjectLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Judecoin Wallet") + i18nManagerObj.emptyString)
                    font.bold: true
                }

                Label {
                    objectName: "aboutProjectBlurb"
                    text: qsTr("Judecoin is a community fork of Monero. This " +
                               "wallet is open-source software released under " +
                               "the BSD 3-Clause license.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    objectName: "aboutCopyrightLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "Copyright (c) 2026, The Judecoin Wallet Project") + i18nManagerObj.emptyString)
                    font.family: "monospace"
                }

                Label {
                    objectName: "aboutSpdxLabel"
                    text: (i18nManagerObj.qtr("AboutSettings", "SPDX-License-Identifier: BSD-3-Clause") + i18nManagerObj.emptyString)
                    font.family: "monospace"
                }
            }
        }

        // -- support / sources footer -------------------------------
        Label {
            objectName: "aboutSupportLabel"
            text: qsTr("Source code, release notes, and issue tracker are " +
                       "distributed with the wallet package. Visit the " +
                       "project home page for verified mirrors.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.topMargin: 8
            font.italic: true
        }

        Item { Layout.fillHeight: true }
    }
}
