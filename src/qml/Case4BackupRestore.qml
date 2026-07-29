// =============================================================================
// Judecoin Wallet - Case4BackupRestore.qml (st-022, P0 #12)
//
// Case 4 — Restore wallet from exported key file. Flow:
//
//   1. User picks a key-backup file (.json / text) exported from Settings → Security
//   2. BackupParserFacade::parseBackupFile() reads the file, parses it, and
//      stores the sensitive fields (spendkey, viewkey) in a C++ token store.
//      QML receives only an opaque token id + non-sensitive metadata:
//      { address, restoreHeight, date }.
//   3. User enters new wallet name + password and clicks Restore.
//   4. WalletManager::restoreFromBackupByToken() consumes the token, builds the
//      generate_from_keys request, and scrubs every local copy of the keys.
//
// Key export counterpart is in SecuritySettings.qml (Settings → Security).
//
// V5.4.7 + P0 #12: JS never holds spendkey / viewkey. Password is cleared after
// use. The token is released on every exit path (back, cancel, success, failure,
// component destruction).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: page

    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "Case 4 - Restore from backup") + i18nManagerObj.emptyString)

    // ---- injected from context properties ---------------------------
    property var backupParser: typeof backupParserObj !== "undefined"
        ? backupParserObj : null
    property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null

    // ---- local UI state --------------------------------------------
    // Metadata-only result from BackupParserFacade::parseBackupFile().
    // Sensitive fields are never present here.
    property var metadata: ({ ok: false, tokenId: -1, format: 0,
                              address: "", restoreHeight: 0,
                              date: "", reason: "" })
    property string parserSummary: ""
    property string filename: "restored-wallet"
    property string password: ""
    property string passwordConfirm: ""
    property string statusMessage: ""
    property string errorMessage: ""
    property bool inFlight: false

    signal back()
    signal walletReady(string filename)

    // ---- file dialog -----------------------------------------------
    FileDialog {
        id: fileDialog
        title: (i18nManagerObj.qtr("Case4BackupRestore", "Open key backup file") + i18nManagerObj.emptyString)
        nameFilters: [ (i18nManagerObj.qtr("Case4BackupRestore", "Key backup files (*.json)") + i18nManagerObj.emptyString),
                       (i18nManagerObj.qtr("Case4BackupRestore", "All files (*)") + i18nManagerObj.emptyString) ]
        onAccepted: {
            errorMessage = ""
            statusMessage = (i18nManagerObj.qtr("Case4BackupRestore", "Reading file...") + i18nManagerObj.emptyString)
            page.loadBackupFile(selectedFile)
        }
    }

    // =====================================================================
    // UI — aligned with Case2Open.qml chrome (Forest Green top bar +
    // step indicator + centered white card + light-green bottom bar).
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
                    objectName: "Case4BackButton"
                    text: (i18nManagerObj.qtr("Case4BackupRestore", "\u2190 Back") + i18nManagerObj.emptyString)
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "Back to wizard home") + i18nManagerObj.emptyString)
                    onClicked: {
                        console.log("Case4BackupRestore (st-022): top back -> wizard home")
                        page._wipeSecrets()
                        page.back()
                    }
                }
                Label {
                    text: (i18nManagerObj.qtr("Case4BackupRestore", "Case 4 - Restore from backup") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Label {
                    text: (i18nManagerObj.qtr("Case4BackupRestore", "Step 1 / 1") + i18nManagerObj.emptyString)
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

        // ---- Centered card (matches Case2Open.qml) -------------------
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
                        text: (i18nManagerObj.qtr("Case4BackupRestore", "Restore from key backup") + i18nManagerObj.emptyString)
                        font.pixelSize: 22
                        font.bold: true
                        color: "#14532d"
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Label {
                        text: qsTr(
                            "Pick a key backup file exported from Settings \u2192 Security. " +
                            "We'll parse the saved address, viewkey, spendkey and restore " +
                            "height, then let you choose a new wallet name and password.")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        color: "#6b8074"
                        font.pixelSize: 12
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    // ---- Browse button (replaces the old gray box) ---
                    Button {
                        objectName: "Case4BrowseButton"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        text: (i18nManagerObj.qtr("Case4BackupRestore", "Browse for key backup file...") + i18nManagerObj.emptyString)
                        onClicked: fileDialog.open()
                        Accessible.role: Accessible.Button
                        Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "Browse for key backup file") + i18nManagerObj.emptyString)
                    }

                    // ---- Parser summary / error ----------------------
                    Label {
                        Layout.fillWidth: true
                        visible: page.parserSummary.length > 0 || page.errorMessage.length > 0
                        text: page.errorMessage.length > 0
                            ? page.errorMessage : page.parserSummary
                        color: page.metadata.ok ? "#1a7f37"
                               : (page.errorMessage.length > 0 ? "#b1361b" : "#6b8074")
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                    }

                    // ---- Parsed-key summary (address / height / date) -
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: page.metadata.ok

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: (i18nManagerObj.qtr("Case4BackupRestore", "Address") + i18nManagerObj.emptyString)
                                font.pixelSize: 12
                                color: "#6b8074"
                            }
                            Label {
                                text: page.metadata.address !== undefined
                                    ? page.metadata.address : ""
                                font.family: "monospace"
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                color: "#14532d"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: (i18nManagerObj.qtr("Case4BackupRestore", "Restore height") + i18nManagerObj.emptyString)
                                    font.pixelSize: 12
                                    color: "#6b8074"
                                }
                                Label {
                                    text: page.metadata.restoreHeight !== undefined
                                        ? page.metadata.restoreHeight.toString()
                                        : (i18nManagerObj.qtr("Case4BackupRestore", "0 (full scan)") + i18nManagerObj.emptyString)
                                    color: "#14532d"
                                    font.pixelSize: 12
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: (i18nManagerObj.qtr("Case4BackupRestore", "Backup date") + i18nManagerObj.emptyString)
                                    font.pixelSize: 12
                                    color: "#6b8074"
                                }
                                Label {
                                    text: page.metadata.date !== undefined && page.metadata.date.length > 0
                                        ? page.metadata.date : (i18nManagerObj.qtr("Case4BackupRestore", "(not provided)") + i18nManagerObj.emptyString)
                                    color: page.metadata.date !== undefined && page.metadata.date.length > 0
                                        ? "#14532d" : "#6b8074"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }

                    // ---- New-wallet form (gated by metadata.ok) -----
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        enabled: page.metadata.ok

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: (i18nManagerObj.qtr("Case4BackupRestore", "New wallet name") + i18nManagerObj.emptyString)
                                font.pixelSize: 12
                                color: "#6b8074"
                            }
                            TextField {
                                id: filenameField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                text: page.filename
                                onTextChanged: page.filename = text
                                placeholderText: (i18nManagerObj.qtr("Case4BackupRestore", "e.g. my-restored-wallet") + i18nManagerObj.emptyString)
                                Accessible.role: Accessible.EditableText
                                Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "New wallet name") + i18nManagerObj.emptyString)
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: (i18nManagerObj.qtr("Case4BackupRestore", "New wallet password") + i18nManagerObj.emptyString)
                                font.pixelSize: 12
                                color: "#6b8074"
                            }
                            TextField {
                                id: passwordField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                echoMode: TextInput.Password
                                text: page.password
                                onTextChanged: page.password = text
                                Accessible.role: Accessible.EditableText
                                Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "New wallet password") + i18nManagerObj.emptyString)
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Label {
                                text: (i18nManagerObj.qtr("Case4BackupRestore", "Confirm new password") + i18nManagerObj.emptyString)
                                font.pixelSize: 12
                                color: "#6b8074"
                            }
                            TextField {
                                id: passwordConfirmField
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                echoMode: TextInput.Password
                                text: page.passwordConfirm
                                onTextChanged: page.passwordConfirm = text
                                Accessible.role: Accessible.EditableText
                                Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "Confirm new wallet password") + i18nManagerObj.emptyString)
                            }
                        }
                    }

                    // ---- Status / error row ------------------------
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        objectName: "Case4ErrorLabel"
                        text: page.errorMessage
                        visible: page.errorMessage.length > 0
                        color: "#dc2626"
                        font.pixelSize: 13
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: page.statusMessage
                        visible: page.statusMessage.length > 0
                        color: "#14532d"
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    // ---- Restore button -------------------------------
                    Button {
                        objectName: "Case4SubmitButton"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        text: page.inFlight ? (i18nManagerObj.qtr("Case4BackupRestore", "Working...") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("Case4BackupRestore", "Restore") + i18nManagerObj.emptyString)
                        enabled: !page.inFlight && page.metadata.ok &&
                                 page.metadata.tokenId !== -1 &&
                                 page.password.length > 0 &&
                                 page.password === page.passwordConfirm &&
                                 page.walletManager !== null
                        onClicked: page.runRestore()
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                    }
                }
            }
        }

        // ---- Forest Green bottom bar (matches Case2Open.qml) --------
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
                    text: (i18nManagerObj.qtr("Case4BackupRestore", "\u2190 Back to wizard") + i18nManagerObj.emptyString)
                    enabled: !page.inFlight
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case4BackupRestore", "Back to wizard") + i18nManagerObj.emptyString)
                    onClicked: {
                        console.log("Case4BackupRestore (st-022): bottom back -> wizard home")
                        page._wipeSecrets()
                        page.back()
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: (i18nManagerObj.qtr("Case4BackupRestore", "V5.4.7 \u00b7 keys never leave the device") + i18nManagerObj.emptyString)
                    color: "#6b8074"
                    font.pixelSize: 11
                }
            }
        }
    }

    // ---- handlers ------------------------------------------------

    function loadBackupFile(path) {
        errorMessage = ""
        statusMessage = ""

        // Release any previously loaded token before replacing it.
        page.releaseBackupToken()

        if (page.backupParser === null) {
            errorMessage = (i18nManagerObj.qtr("Case4BackupRestore", "BackupParser not available.") + i18nManagerObj.emptyString)
            statusMessage = ""
            return
        }

        const r = page.backupParser.parseBackupFile(path)
        page.metadata = r

        if (r.ok && r.tokenId !== undefined && r.tokenId !== -1) {
            parserSummary = (i18nManagerObj.qtr("Case4BackupRestore", "Key file loaded. Address: %1, Height: %2")
                .arg(r.address.substring(0, 12) + "...")
                .arg(r.restoreHeight || 0))
            statusMessage = (i18nManagerObj.qtr("Case4BackupRestore", "Key file validated. Choose a wallet name + password to restore.") + i18nManagerObj.emptyString)
        } else {
            errorMessage = r.reason || (i18nManagerObj.qtr("Case4BackupRestore", "Invalid key file.") + i18nManagerObj.emptyString)
            parserSummary = ""
        }
    }

    function runRestore() {
        if (page.walletManager === null) {
            errorMessage = (i18nManagerObj.qtr("Case4BackupRestore", "WalletManager not wired.") + i18nManagerObj.emptyString)
            return
        }
        if (page.metadata.tokenId === -1) {
            errorMessage = (i18nManagerObj.qtr("Case4BackupRestore", "No key backup loaded.") + i18nManagerObj.emptyString)
            return
        }
        if (page.password !== page.passwordConfirm) {
            errorMessage = (i18nManagerObj.qtr("Case4BackupRestore", "Passwords do not match.") + i18nManagerObj.emptyString)
            return
        }

        const _filename = page.filename
        const _password = page.password

        errorMessage = ""
        statusMessage = (i18nManagerObj.qtr("Case4BackupRestore", "Calling wallet-rpc generate_from_keys...") + i18nManagerObj.emptyString)
        inFlight = true

        const result = page.walletManager.restoreFromBackupByToken(
            page.metadata.tokenId, _password, _filename)

        inFlight = false

        // The token is consumed by restoreFromBackupByToken regardless of
        // success or failure; make sure the UI does not try to release it again.
        page.metadata.tokenId = -1

        if (result.ok) {
            statusMessage = (i18nManagerObj.qtr("Case4BackupRestore", "Wallet restored successfully.") + i18nManagerObj.emptyString)
            page.password = ""
            page.passwordConfirm = ""
            page.metadata = ({ ok: false, tokenId: -1, format: 0,
                               address: "", restoreHeight: 0,
                               date: "", reason: "" })
            page.parserSummary = (i18nManagerObj.qtr("Case4BackupRestore", "Restore complete.") + i18nManagerObj.emptyString)
            page.walletReady(_filename)
        } else {
            errorMessage = result.error || (i18nManagerObj.qtr("Case4BackupRestore", "Restore failed.") + i18nManagerObj.emptyString)
            page.password = ""
            page.passwordConfirm = ""
            page.metadata = ({ ok: false, tokenId: -1, format: 0,
                               address: "", restoreHeight: 0,
                               date: "", reason: "" })
        }
    }

    function releaseBackupToken() {
        if (page.metadata.tokenId !== -1 && page.backupParser !== null) {
            page.backupParser.releaseToken(page.metadata.tokenId)
        }
        page.metadata = ({ ok: false, tokenId: -1, format: 0,
                           address: "", restoreHeight: 0,
                           date: "", reason: "" })
    }

    function clearSensitiveData() {
        page.releaseBackupToken()
    }

    // =====================================================================
    // V5.4.7 + P0 #12: scrub transient secret material on every exit path.
    // Mirrors Case2Open.qml._wipeSecrets() so the back / forgot-password
    // navigation paths all clear password + the token before bubbling.
    // =====================================================================
    function _wipeSecrets() {
        page.password = ""
        page.passwordConfirm = ""
        page.releaseBackupToken()
        page.errorMessage = ""
        page.statusMessage = ""
        page.inFlight = false
    }

    Component.onDestruction: {
        page._wipeSecrets()
    }
}
