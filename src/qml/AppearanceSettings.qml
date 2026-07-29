// =============================================================================
// Judecoin Wallet - AppearanceSettings.qml (st-080, hardened st-094b)
//
// Subpage of Settings.qml (Category::Appearance). V1.0 ships a single
// theme (Forest Green) per V5.4.4 decision A / V5.4.15: no theme
// switcher in the UI. The page is read-only - it explains the locked
// look so the user can confirm "yes, this is the design you get" and
// shows the underlying Qt style (Fusion), the persisted theme token
// (SettingsManager.appearanceTheme), and the accent badge as a
// diagnostic surface. Per the v1-fix audit finding 2.7 (group-B
// sub-pages inherited only the st-080 skeleton), this card brings
// the page to the same V5.4.15 visual contract as the rest of the
// Settings sub-pages.
//
// st-094b hardening (v1-fix audit finding 2.7, sibling of st-094a):
//   - Forest Green header accent bar (8 px, #16a34a, mirrors the
//     st-094a group-A pages so all 5 hardened sub-pages share the
//     same visual chrome). Single source of truth is theme.qss
//     (V5.4.15); the literal #16a34a mirrors design_specs §2
//     (--storage / --accent-mid).
//   - objectName on every visible element (header, summary rows,
//     note) so test harnesses can target by string regardless of
//     QML id scoping.
//   - "Saved" pill driven by SettingsManager.appearanceThemeChanged
//     AND appearanceStyleChanged (1.6 s auto-fade Timer). Appearance
//     is read-only on the user side, but the C++ side can still
//     mutate the persisted theme / style (reset flow, future
//     import-theme) and the user should get the same glanceable
//     "yes, that took" cue as the group-A pages.
//
// V5.4.9 compliance: pure QML, no Widgets; qsTr() for every visible
// label (incl. the locked-theme strings the test substring-matches
// on - 'Forest Green', 'Fusion').
// V5.4.15 compliance: locked theme, no ComboBox with a model (the
// test enforces no theme switcher; we satisfy by not using any
// ComboBox in this file).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: appearanceRoot
    objectName: "AppearanceSettings"

    // a11y (st-095): Appearance sub-page is a landmark Pane; the page is
    // intentionally read-only in V1.0 (theme is locked per V5.4.15),
    // but the landmark still helps AT structure the Settings tree.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("AppearanceSettings", "Appearance") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("AppearanceSettings", "Read-only summary of the locked V5.4.15 Forest Green theme") + i18nManagerObj.emptyString)

    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null

    // -- save toast state ---------------------------------------------
    // Driven by both appearanceThemeChanged and appearanceStyleChanged
    // so the user gets the same glanceable confirmation as the group-A
    // pages even though the user can't trigger the save themselves
    // (a reset, theme reset, or future import-theme flow can fire it).
    property bool _justSaved: false
    Timer {
        id: savedTimer
        interval: 1600
        onTriggered: appearanceRoot._justSaved = false
    }
    Connections {
        target: appearanceRoot.settingsManager
        ignoreUnknownSignals: true
        function onAppearanceThemeChanged(theme) {
            appearanceRoot._justSaved = true
            savedTimer.restart()
        }
        function onAppearanceStyleChanged(style) {
            appearanceRoot._justSaved = true
            savedTimer.restart()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // -- Forest Green header accent -----------------------------
        // 8 px tall bar across the top of the sub-page. Mirrors the
        // LanguageSettings / AutoLockSettings / TransferConfirmSettings
        // accent so the locked V5.4.15 visual contract stays unified.
        Rectangle {
            objectName: "appearanceHeaderAccent"
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
                objectName: "appearanceTitleLabel"
                text: (i18nManagerObj.qtr("AppearanceSettings", "Appearance") + i18nManagerObj.emptyString)
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
            }

            Rectangle {
                objectName: "appearanceSavedPill"
                visible: appearanceRoot._justSaved
                radius: 10
                color: "#16a34a"
                implicitHeight: savedLabel.implicitHeight + 6
                implicitWidth:  savedLabel.implicitWidth  + 16
                Label {
                    id: savedLabel
                    anchors.centerIn: parent
                    text: (i18nManagerObj.qtr("AppearanceSettings", "Saved") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }

        Label {
            objectName: "appearanceIntroLabel"
            text: qsTr("The wallet ships with a single theme. The visual " +
                       "design is locked to a calm green palette tuned for " +
                       "long reading sessions.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // -- read-only summary --------------------------------------
        // The persisted theme token comes from SettingsManager (so the
        // value matches what is actually in QSettings); the Qt style
        // and accent are visual constants from theme.qss.
        Frame {
            objectName: "appearanceSummaryFrame"
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label {
                    objectName: "appearanceThemeLabel"
                    text: (i18nManagerObj.qtr("AppearanceSettings", "Theme") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "appearanceThemeValueLabel"
                    // Surface the actual persisted token, default to the
                    // human-readable name when the manager isn't wired
                    // (unit-test null-guard pattern, mirrors the
                    // group-A pages).
                    text: appearanceRoot.settingsManager !== null
                        ? appearanceRoot.settingsManager.appearanceTheme
                        : (i18nManagerObj.qtr("AppearanceSettings", "Forest Green (locked)") + i18nManagerObj.emptyString)
                }

                Label {
                    objectName: "appearanceStyleLabel"
                    text: (i18nManagerObj.qtr("AppearanceSettings", "Qt Style") + i18nManagerObj.emptyString)
                    font.bold: true
                    Layout.topMargin: 8
                }
                Label {
                    objectName: "appearanceStyleValueLabel"
                    text: (i18nManagerObj.qtr("AppearanceSettings", "Fusion (default)") + i18nManagerObj.emptyString)
                }

                Label {
                    objectName: "appearanceAccentLabel"
                    text: (i18nManagerObj.qtr("AppearanceSettings", "Accent") + i18nManagerObj.emptyString)
                    font.bold: true
                    Layout.topMargin: 8
                }
                RowLayout {
                    objectName: "appearanceAccentRow"
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        objectName: "appearanceAccentSwatch"
                        width: 32; height: 32; radius: 6
                        color: "#22c55e"   // accent / 600 - design_specs §5.2
                        border.color: "#166534"
                        border.width: 1
                    }
                    Label {
                        objectName: "appearanceAccentHexLabel"
                        text: "#22c55e"
                        font.family: "monospace"
                    }
                }
            }
        }

        Label {
            objectName: "appearanceNoteLabel"
            text: qsTr("Note: V1.0 ships Forest Green only. " +
                       "More themes coming in a future release.")
            font.italic: true
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
