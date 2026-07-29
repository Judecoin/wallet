// =============================================================================
// Judecoin Wallet - AutoLockSettings.qml (st-080, explicit-save)
//
// Subpage of Settings.qml (Category::AutoLock). Lets the user pick how
// long the wallet can sit idle before it locks itself (5/15/30/60 min
// + Never). Uses explicit-save pattern matching LanguageSettings.qml.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: autoLockRoot
    objectName: "AutoLockSettings"

    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("AutoLockSettings", "Auto-lock settings") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("AutoLockSettings", "How long the wallet can sit idle before locking itself") + i18nManagerObj.emptyString)

    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null

    readonly property var lockModel: [
        { seconds: 300,  label: (i18nManagerObj.qtr("AutoLockSettings", "5 minutes") + i18nManagerObj.emptyString) },
        { seconds: 900,  label: (i18nManagerObj.qtr("AutoLockSettings", "15 minutes") + i18nManagerObj.emptyString) },
        { seconds: 1800, label: (i18nManagerObj.qtr("AutoLockSettings", "30 minutes") + i18nManagerObj.emptyString) },
        { seconds: 3600, label: (i18nManagerObj.qtr("AutoLockSettings", "1 hour") + i18nManagerObj.emptyString) },
        { seconds: 0,    label: (i18nManagerObj.qtr("AutoLockSettings", "Never (manual lock only)") + i18nManagerObj.emptyString) }
    ]

    // -- explicit-save state --
    property int  _baselineSeconds: 300
    property int  _pendingSeconds:  300
    property bool _hasChanges: false

    Component.onCompleted: _captureBaseline()

    function _captureBaseline() {
        if (settingsManager === null) {
            _baselineSeconds = 300
            _pendingSeconds  = 300
            _hasChanges = false
            return
        }
        _baselineSeconds = settingsManager.autoLockSeconds
        _pendingSeconds  = _baselineSeconds
        _hasChanges = false
    }

    function _commit() {
        if (settingsManager === null) return
        settingsManager.autoLockSeconds = _pendingSeconds
        _captureBaseline()
    }

    function _discard() {
        _captureBaseline()
    }

    readonly property int currentIndex: {
        for (var i = 0; i < autoLockRoot.lockModel.length; ++i) {
            if (autoLockRoot.lockModel[i].seconds === autoLockRoot._pendingSeconds) {
                return i
            }
        }
        return 0
    }

    ColumnLayout {
        width: parent.width
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: autoLockRoot._hasChanges
                    ? (i18nManagerObj.qtr("AutoLockSettings", "\u26a0 Unsaved changes") + i18nManagerObj.emptyString)
                    : ""
                color: autoLockRoot._hasChanges ? "#f59e0b" : "transparent"
                font.italic: true
            }

            Button {
                objectName: "autoLockDiscardBtn"
                text: (i18nManagerObj.qtr("AutoLockSettings", "Discard") + i18nManagerObj.emptyString)
                enabled: autoLockRoot._hasChanges
                onClicked: autoLockRoot._discard()
            }

            Button {
                objectName: "autoLockSaveBtn"
                text: (i18nManagerObj.qtr("AutoLockSettings", "Save") + i18nManagerObj.emptyString)
                enabled: autoLockRoot._hasChanges
                highlighted: true
                onClicked: autoLockRoot._commit()
            }
        }

        Label {
            text: qsTr("How long the wallet can sit idle before it locks " +
                       "itself and requires the password to resume.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ComboBox {
            id: lockCombo
            objectName: "autoLockCombo"
            Layout.fillWidth: true
            Layout.maximumWidth: 360
            model: autoLockRoot.lockModel
            textRole: "label"
            valueRole: "seconds"
            currentIndex: autoLockRoot.currentIndex
            Accessible.role: Accessible.ComboBox
            Accessible.name: (i18nManagerObj.qtr("AutoLockSettings", "Auto-lock timeout") + i18nManagerObj.emptyString)
            Accessible.description: (i18nManagerObj.qtr("AutoLockSettings", "Pick how long the wallet can sit idle before it locks; choose Never to lock manually only") + i18nManagerObj.emptyString)

            onActivated: function(index) {
                if (!autoLockRoot.lockModel[index]) return
                var sec = autoLockRoot.lockModel[index].seconds
                autoLockRoot._pendingSeconds = sec
                autoLockRoot._hasChanges = (sec !== autoLockRoot._baselineSeconds)
            }
        }

        Frame {
            Layout.fillWidth: true
            visible: autoLockRoot.settingsManager !== null
            ColumnLayout {
                anchors.fill: parent
                Label {
                    text: (i18nManagerObj.qtr("AutoLockSettings", "Current selection") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "autoLockCurrentLabel"
                    text: lockCombo.currentText
                    font.family: "monospace"
                }
            }
        }
    }
}
