// =============================================================================
// Judecoin Wallet - SecuritySettings.qml (st-022 key export)
//
// Security settings sub-page. Provides:
//   - Export wallet keys: writes a JSON file containing spendkey, viewkey,
//     address, and restore_height. This file can be imported via Case4
//     (Backup Restore) to recover the wallet.
//
// The exported JSON format is:
//   {
//     "address":        "J...",
//     "spendkey":       "64-char hex",
//     "viewkey":        "64-char hex",
//     "restore_height": 1234567,
//     "date":           "2026-07-18T15:30:00"
//   }
//
// V5.4.7: secrets stay in WalletManager — we only pass them from the
// RPC to the export file, never to QML log or on-screen display.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: securityRoot

    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("SecuritySettings", "Security settings") + i18nManagerObj.emptyString)

    property var walletManager: typeof walletManagerObj !== "undefined"
        ? walletManagerObj : null

    property string exportStatus: ""
    property bool exporting: false

    // File save dialog for key export
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        title: (i18nManagerObj.qtr("SecuritySettings", "Export wallet keys") + i18nManagerObj.emptyString)
        nameFilters: [ (i18nManagerObj.qtr("SecuritySettings", "Key backup files (*.json)") + i18nManagerObj.emptyString),
                       (i18nManagerObj.qtr("SecuritySettings", "All files (*)") + i18nManagerObj.emptyString) ]
        defaultSuffix: "json"
        onAccepted: {
            doExport(selectedFile)
        }
    }

    function doExport(fileUrl) {
        if (securityRoot.walletManager === null) {
            exportStatus = (i18nManagerObj.qtr("SecuritySettings", "WalletManager not available.") + i18nManagerObj.emptyString)
            return
        }
        exporting = true
        exportStatus = (i18nManagerObj.qtr("SecuritySettings", "Querying wallet keys...") + i18nManagerObj.emptyString)

        // Ensure .json extension
        var finalUrl = fileUrl
        if (typeof finalUrl === "string" && !finalUrl.endsWith(".json")) {
            finalUrl = finalUrl + ".json"
        }

        try {
            const spendkey = securityRoot.walletManager.getSpendKey()
            const viewkey  = securityRoot.walletManager.getViewKey()
            const address  = securityRoot.walletManager.getWalletAddress()
            const height   = securityRoot.walletManager.getWalletHeight()
            const date     = new Date().toISOString()

            const exportObj = {
                address:        address,
                spendkey:       spendkey,
                viewkey:        viewkey,
                restore_height: height,
                date:           date
            }
            const jsonStr = JSON.stringify(exportObj, null, 2)

            // Write file via BackupParser readFile's inverse.
            const backupParser = typeof backupParserObj !== "undefined"
                ? backupParserObj : null
            if (backupParser !== null && typeof backupParser.writeFile === "function") {
                backupParser.writeFile(finalUrl, jsonStr)
            } else {
                exportStatus = (i18nManagerObj.qtr("SecuritySettings", "Export: file writer not available. Keys obtained successfully, but could not write to disk.") + i18nManagerObj.emptyString)
                exporting = false
                return
            }

            exportStatus = (i18nManagerObj.qtr("SecuritySettings", "Keys exported successfully to %1.").arg(finalUrl) + i18nManagerObj.emptyString)} catch (e) {
            exportStatus = (i18nManagerObj.qtr("SecuritySettings", "Export failed: %1").arg(e) + i18nManagerObj.emptyString)}
        exporting = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: (i18nManagerObj.qtr("SecuritySettings", "Security") + i18nManagerObj.emptyString)
            font.pixelSize: 18
            font.bold: true
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Label {
            text: (i18nManagerObj.qtr("SecuritySettings", "Key Export — Save your wallet keys to a backup file.\nThis file can be used in Case 4 (Restore from backup) to recover your wallet on another device or after reinstall.\n\nWarning: Anyone with this file can access your funds. Keep it safe.") + i18nManagerObj.emptyString)
            color: "#666"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 12
        }

        Button {
            text: exporting ? (i18nManagerObj.qtr("SecuritySettings", "Exporting...") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("SecuritySettings", "Export Wallet Keys") + i18nManagerObj.emptyString)
            enabled: !exporting && securityRoot.walletManager !== null
            onClicked: saveDialog.open()
            Accessible.role: Accessible.Button
            Accessible.name: text
        }

        Label {
            Layout.fillWidth: true
            visible: exportStatus.length > 0
            text: exportStatus
            color: exportStatus.indexOf((i18nManagerObj.qtr("SecuritySettings", "failed") + i18nManagerObj.emptyString)) >= 0 ? "#b1361b" : "#1a7f37"
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }

        Item { Layout.fillHeight: true }
    }
}
