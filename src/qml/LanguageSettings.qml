// =============================================================================
// Judecoin Wallet - LanguageSettings.qml (st-080, st-094a, st-132 explicit-save)
//
// Subpage of Settings.qml (Category::Language). Lets the user pick one of
// the 9 supported V1.0 languages (per SettingsSchema::supportedLanguages()),
// or "Follow system" (empty string in QSettings, per V5.4.4 decision A).
//
// st-132 (2026-07-16 21:59): explicit-save mode. The ComboBox
// change goes into _pendingLocale; clicking Save writes it to
// SettingsManager (which fires languageLocaleChanged, which now wires
// I18nManager::setCurrentLanguage from main.cpp). The previous auto-save
// flow + "Saved" pill are removed - explicit save is the new contract.
//
// V5.4.9 compliance: pure QML, no Widgets; qsTr() for every visible label.
// V5.4.7 compliance: nothing secret-bearing is stored in this component.
// V5.4.12 compliance: dropdown lists the canonical 9 locales ordered to
// match SettingsSchema (en / zh_TW / de / ja / ko / it / nl / ru / es).
// zh_CN is deliberately omitted (not authored for V1.0).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: languageRoot
    objectName: "LanguageSettings"

    // a11y (st-095): Language sub-page is a landmark Pane; the language
    // ComboBox below carries its own Accessible.* role + name.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("LanguageSettings", "Language settings") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("LanguageSettings", "Choose the display language for the wallet") + i18nManagerObj.emptyString)

    // -- public properties (inherited from Settings.qml via Loader) ----
    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null

    // -- explicit-save state (st-132) -----------------------------------
    // _baselineLocale = saved value (from settingsManager.languageLocale).
    // _pendingLocale  = user's draft value (ComboBox selection).
    // _hasChanges = true iff _pendingLocale !== _baselineLocale.
    property string _baselineLocale: ""
    property string _pendingLocale:  ""
    property bool   _hasChanges: false

    Component.onCompleted: _captureBaseline()

    function _captureBaseline() {
        if (settingsManager === null) {
            _baselineLocale = ""
            _pendingLocale  = ""
            _hasChanges = false
            return
        }
        _baselineLocale = settingsManager.languageLocale || ""
        _pendingLocale  = _baselineLocale
        _hasChanges = false
    }

    function _commit() {
        if (settingsManager === null) return
        // settingsManager::setLanguageLocale fires languageLocaleChanged,
        // which main.cpp now wires to I18nManager::setCurrentLanguage
        // (st-132). The QTranslator swap triggers LanguageChange, which
        // makes every qsTr() binding re-evaluate, so the UI text
        // switches language live without an app restart.
        settingsManager.languageLocale = _pendingLocale
        _captureBaseline()
    }

    function _discard() {
        _captureBaseline()
    }

    // -- internal model: 9 supported + "auto" --------------------------
    // Stored in QSettings as empty string ("auto" sentinel, V5.4.4 A).
    // The ComboBox `currentText` is the locale code, or "" for auto.
    readonly property var languageModel: [
        { code: "",     label: (i18nManagerObj.qtr("LanguageSettings", "Follow system (auto)") + i18nManagerObj.emptyString) },
        { code: "en",   label: (i18nManagerObj.qtr("LanguageSettings", "English") + i18nManagerObj.emptyString) },
        { code: "zh_TW",label: (i18nManagerObj.qtr("LanguageSettings", "Chinese (Traditional)") + i18nManagerObj.emptyString) },
        { code: "de",   label: (i18nManagerObj.qtr("LanguageSettings", "German") + i18nManagerObj.emptyString) },
        { code: "ja",   label: (i18nManagerObj.qtr("LanguageSettings", "Japanese") + i18nManagerObj.emptyString) },
        { code: "ko",   label: (i18nManagerObj.qtr("LanguageSettings", "Korean") + i18nManagerObj.emptyString) },
        { code: "it",   label: (i18nManagerObj.qtr("LanguageSettings", "Italian") + i18nManagerObj.emptyString) },
        { code: "nl",   label: (i18nManagerObj.qtr("LanguageSettings", "Dutch") + i18nManagerObj.emptyString) },
        { code: "ru",   label: (i18nManagerObj.qtr("LanguageSettings", "Russian") + i18nManagerObj.emptyString) },
        { code: "es",   label: (i18nManagerObj.qtr("LanguageSettings", "Spanish") + i18nManagerObj.emptyString) }
    ]

    // Index derived from _pendingLocale so the ComboBox shows the
    // DRAFT value (the saved value is only seen via the "Current
    // selection" status frame below).
    readonly property int currentIndex: {
        for (var i = 0; i < languageRoot.languageModel.length; ++i) {
            if (languageRoot.languageModel[i].code === languageRoot._pendingLocale) {
                return i
            }
        }
        return 0   // fall back to "auto"
    }

    ColumnLayout {
        width: parent.width
        spacing: 4

        // -- Save / Discard bar (st-132 explicit-save) ----------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: languageRoot._hasChanges
                    ? (i18nManagerObj.qtr("LanguageSettings", "\u26a0 Unsaved changes") + i18nManagerObj.emptyString)
                    : ""
                color: languageRoot._hasChanges ? "#f59e0b" : "transparent"
                font.italic: true
            }

            Button {
                objectName: "languageDiscardBtn"
                text: (i18nManagerObj.qtr("LanguageSettings", "Discard") + i18nManagerObj.emptyString)
                enabled: languageRoot._hasChanges
                onClicked: languageRoot._discard()
            }

            Button {
                objectName: "languageSaveBtn"
                text: (i18nManagerObj.qtr("LanguageSettings", "Save") + i18nManagerObj.emptyString)
                enabled: languageRoot._hasChanges
                highlighted: true
                onClicked: languageRoot._commit()
            }
        }

        Label {
            text: (i18nManagerObj.qtr("LanguageSettings", "Choose the language used throughout the wallet. \"Follow system\" will use the language configured by your OS.") + i18nManagerObj.emptyString)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ComboBox {
            id: langCombo
            objectName: "languageCombo"
            Layout.fillWidth: true
            Layout.maximumWidth: 360
            model: languageRoot.languageModel
            textRole: "label"
            valueRole: "code"
            currentIndex: languageRoot.currentIndex
            // a11y (st-095): the language picker is a combo box; the name
            // and description echo the surrounding label text so a screen
            // reader announces "Display language, ..." consistently with
            // the visible label.
            Accessible.role: Accessible.ComboBox
            Accessible.name: (i18nManagerObj.qtr("LanguageSettings", "Display language") + i18nManagerObj.emptyString)
            Accessible.description: (i18nManagerObj.qtr("LanguageSettings", "Pick the wallet UI language; choose \"Follow system\" to inherit the OS locale") + i18nManagerObj.emptyString)

            // st-132: write to _pendingLocale; Save commits via
            // _commit() -> settingsManager.languageLocale ->
            // languageLocaleChanged -> I18nManager::setCurrentLanguage
            // (wired in main.cpp st-132 block).
            onActivated: function(index) {
                if (!languageRoot.languageModel[index]) return
                var code = languageRoot.languageModel[index].code
                languageRoot._pendingLocale = code
                languageRoot._hasChanges = (code !== languageRoot._baselineLocale)
            }
        }

        // Live status: shows the locale that will be persisted on Save.
        Frame {
            Layout.fillWidth: true
            visible: languageRoot.settingsManager !== null
            ColumnLayout {
                anchors.fill: parent
                Label {
                    text: (i18nManagerObj.qtr("LanguageSettings", "Current selection") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    objectName: "languageCurrentLabel"
                    text: langCombo.currentText
                    font.family: "monospace"
                }
            }
        }
    }
}
