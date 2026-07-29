// =============================================================================
// Judecoin Wallet - AddressBookPage.qml (st-063)
//
// Named-addresses page reachable from the Sidebar tab (future) and
// from --page AddressBook on the CLI. Backs onto the
// `localDataManagerObj` Q_INVOKABLE surface added in st-063:
//   * localDataManagerObj.addressBookEntries() -> QVariantList
//   * localDataManagerObj.addressBookSize()    -> int
//   * localDataManagerObj.addAddressEntry(address, label, description, paymentId)
//       -> qint64 (assigned id; -1 on validation failure)
//   * localDataManagerObj.updateAddressEntry(id, label, description, paymentId)
//       -> bool
//   * localDataManagerObj.removeAddressEntry(id) -> bool
//   * localDataManagerObj.importAddressBook(path) -> bool
//   * localDataManagerObj.exportAddressBook(path) -> bool
//
// The C++ side (LocalDataManager st-017 + st-063 bridge) is committed
// in 1a3fc0d + eb5d0fc. The QML side owns the row layout, edit dialog
// plumbing, import/export path-input dialog wiring, and delete
// confirmation.
//
// Layout (per design_specs.md section 5.7 + st-063 spec_notes):
//
//   TopBar (page title "Address Book" + back button)    56px
//   toolbar (Add / Import / Export)                     48px
//   status bar (transient success/error)                0-32px
//   ListView: rows of Label + address + Edit + Delete
//   (empty)  - "No saved addresses. Tap + Add."
//
// V1.0 file-picker note: this page does NOT import QtQuick.Dialogs.
// The optional QtQuick.Dialogs module is not installed in every
// deployment target (V1.0 ships on minimal Linux distros and a
// snap / flatpak sandbox where the GUI file picker may be
// unavailable). The Import / Export actions therefore route through
// a small inline PathInputDialog that asks the user to type the
// absolute path. The "Browse..." button next to the field is a
// stretch goal for V1.1; V1.0 users paste the path from their
// terminal or type it directly. This keeps the address-book feature
// usable on every supported target without taking on a Dialogs
// dependency.
//
// V5.4.7 compliance: no key/seed/password/private-key material is
//                    ever bound. The address book holds public-chain
//                    addresses + user-supplied labels only.
// V5.4.9 compliance: every visible string is qsTr() wrapped.
// V5.4.15 compliance: single Forest Green theme baked in.
// V5.4.21 compliance: mainnet-only.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "AddressBookPage"

    // ---- public API -----------------------------------------------------
    /// Emitted when the user clicks the back button.
    signal backClicked()

    /// Logical page name (matches the Sidebar tab label, future).
    readonly property string pageName: "AddressBook"

    // ---- a11y v2 (st-100) ------------------------------------------------
    // Keyboard navigation chain: Back -> Add -> Import -> Export, with the
    // Tab cycle returning from Export -> Back so a keyboard-only user can
    // roll forward and back through the four primary affordances without
    // reaching for the mouse. Each focusable item also exposes
    // Accessible.role + Accessible.name + activeFocusOnTab so screen readers
    // announce "Back, button" / "Add, button" / "Import, button" / "Export,
    // button" as focus lands on them. Style mirrors SendPage.qml's
    // KeyNavigation.priority chain (no KeyNavigation.forward).

    // ---- data layer -----------------------------------------------------
    readonly property var _mgr:
        typeof localDataManagerObj !== "undefined" && localDataManagerObj
            ? localDataManagerObj
            : null

    /// Snapshot of address book entries (QVariantList of QVariantMap).
    /// Refreshed whenever addressBookChanged() fires.
    property var _entries: _mgr ? _mgr.addressBookEntries() : []
    readonly property int _count: _entries ? _entries.length : 0

    // ---- status messages (transient) ------------------------------------
    property string _statusMessage: ""
    property bool   _statusIsError: false

    Timer {
        id: _statusClearTimer
        interval: 4000
        repeat:   false
        onTriggered: {
            root._statusMessage = ""
            root._statusIsError = false
        }
    }
    function _setStatus(text, isError) {
        _statusMessage = text
        _statusIsError = isError
        _statusClearTimer.restart()
    }

    // ---- dialog visibility state ---------------------------------------
    property var _editing: null
    property bool _confirmingDelete: false
    property int  _deleteTargetId: 0

    // Path-input dialog mode: "" = closed, "import" = ask for path to
    // import, "export" = ask for path to write. We use a string mode
    // so the same dialog instance handles both cases.
    property string _pathDialogMode: ""
    property string _pathDialogPath: ""

    // ---- event wiring: refresh on addressBookChanged() -----------------
    Component.onCompleted: {
        if (root._mgr) {
            root._mgr.addressBookChanged.connect(function() {
                root._entries = root._mgr.addressBookEntries()
            })
        }
        // a11y v2 (st-100): kick focus to the back button so the Tab
        // chain starts predictably when the user lands on this page
        // via the Sidebar / --page AddressBook CLI. The call is wrapped
        // in Qt.callLater so the StackView finish()ing the push happens
        // first, otherwise the focus call can land on the still-being-
        // attached tree and silently no-op.
        Qt.callLater(function() {
            backButton.forceActiveFocus()
        })
    }

    // ---- UI -------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- top bar (page title + back) --------------------------------
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
                    id: backButton
                    objectName: "AddressBookBackButton"
                    text: (i18nManagerObj.qtr("AddressBookPage", "<- Back") + i18nManagerObj.emptyString)
                    // a11y (st-095): the back affordance is a regular
                    // Button; name mirrors the visible label so AT
                    // announces "Back, button" on focus.
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.description: (i18nManagerObj.qtr("AddressBookPage", "Return to the previous page in the main stack") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): Tab chain entry. Back -> Add
                    // (forward), cycle back from Export.
                    KeyNavigation.tab: addButton
                    KeyNavigation.backtab: exportButton
                    activeFocusOnTab: true
                    onClicked: {
                        console.log("AddressBookPage (st-063): backClicked")
                        root.backClicked()
                    }
                }
                Label {
                    text: (i18nManagerObj.qtr("AddressBookPage", "Address Book") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                }
                Label {
                    text: (i18nManagerObj.qtr("AddressBookPage", "%1 entries").arg(root._count) + i18nManagerObj.emptyString)
                    color: "#dcfce7"
                    font.pixelSize: 12
                }
            }
        }

        // ---- toolbar (Add / Import / Export) ----------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "#f0fdf4"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Button {
                    id: addButton
                    objectName: "addressBookAddButton"
                    text: (i18nManagerObj.qtr("AddressBookPage", "+ Add") + i18nManagerObj.emptyString)
                    // a11y (st-095): button role with semantic name so AT
                    // announces "Add, button" when focus lands.
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("AddressBookPage", "Add address") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("AddressBookPage", "Open the editor to add a new address entry") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): Add -> Import (forward),
                    // Back -> Add (backward, cycle).
                    KeyNavigation.tab: importButton
                    KeyNavigation.backtab: backButton
                    activeFocusOnTab: true
                    onClicked: {
                        console.log("AddressBookPage (st-063): addClicked")
                        root._editing = { id: 0, address: "", label: "",
                                          description: "", paymentId: "" }
                    }
                }
                Button {
                    id: importButton
                    objectName: "addressBookImportButton"
                    text: (i18nManagerObj.qtr("AddressBookPage", "Import") + i18nManagerObj.emptyString)
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("AddressBookPage", "Import address book") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("AddressBookPage", "Import address book entries from a JSON file") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): Import -> Export (forward),
                    // Add -> Import (backward).
                    KeyNavigation.tab: exportButton
                    KeyNavigation.backtab: addButton
                    activeFocusOnTab: true
                    onClicked: {
                        console.log("AddressBookPage (st-063): importClicked")
                        root._pathDialogMode = "import"
                        root._pathDialogPath = ""
                    }
                }
                Button {
                    id: exportButton
                    objectName: "addressBookExportButton"
                    text: (i18nManagerObj.qtr("AddressBookPage", "Export") + i18nManagerObj.emptyString)
                    enabled: root._count > 0
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("AddressBookPage", "Export address book") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("AddressBookPage", "Export the current address book entries to a JSON file") + i18nManagerObj.emptyString)
                    // a11y v2 (st-100): Export -> Back (cycle forward),
                    // Import -> Export (backward).
                    KeyNavigation.tab: backButton
                    KeyNavigation.backtab: importButton
                    activeFocusOnTab: true
                    onClicked: {
                        console.log("AddressBookPage (st-063): exportClicked")
                        root._pathDialogMode = "export"
                        root._pathDialogPath = "address_book.json"
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        // ---- status bar (transient) -------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root._statusMessage.length > 0 ? 32 : 0
            visible: root._statusMessage.length > 0
            color: root._statusIsError ? "#fee2e2" : "#dcfce7"
            Label {
                anchors.fill: parent
                anchors.leftMargin: 16
                verticalAlignment: Text.AlignVCenter
                text: root._statusMessage
                color: root._statusIsError ? "#991b1b" : "#166534"
                elide: Text.ElideRight
            }
        }

        // ---- list --------------------------------------------------------
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root._entries

            // Empty state.
            Label {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: (i18nManagerObj.qtr("AddressBookPage", "No saved addresses. Tap + Add to create one, or Import to load from a JSON file.") + i18nManagerObj.emptyString)
                color: "#666"
                wrapMode: Text.WordWrap
                width: parent.width * 0.8
                horizontalAlignment: Text.AlignHCenter
            }

            delegate: Rectangle {
                width: listView.width
                height: 64
                color: index % 2 === 0 ? "#ffffff" : "#f9fafb"

                required property var modelData
                required property int index

                readonly property var row: modelData || {}
                readonly property string rowAddress: row.address    || ""
                readonly property string rowLabel:   row.label      || ""
                readonly property string rowDesc:    row.description || ""
                readonly property int    rowId:      row.id         || 0

                // Bottom border (suppressed on the last row).
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: "#dcfce7"
                    visible: index < listView.count - 1
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: rowLabel.length > 0
                                ? rowLabel
                                : (i18nManagerObj.qtr("AddressBookPage", "(no label)") + i18nManagerObj.emptyString)
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: rowAddress
                            font.family: "monospace"
                            font.pixelSize: 11
                            color: "#444"
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: rowDesc.length > 0
                            text: rowDesc
                            font.pixelSize: 10
                            color: "#888"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Button {
                        objectName: "addressBookRowEditButton"
                        text: (i18nManagerObj.qtr("AddressBookPage", "Edit") + i18nManagerObj.emptyString)
                        onClicked: {
                            console.log("AddressBookPage (st-063): editClicked id=", rowId)
                            root._editing = {
                                id:          rowId,
                                address:     rowAddress,
                                label:       rowLabel,
                                description: rowDesc,
                                paymentId:   row.paymentId || ""
                            }
                        }
                    }
                    Button {
                        objectName: "addressBookRowDeleteButton"
                        text: (i18nManagerObj.qtr("AddressBookPage", "Delete") + i18nManagerObj.emptyString)
                        onClicked: {
                            console.log("AddressBookPage (st-063): deleteClicked id=", rowId)
                            root._deleteTargetId = rowId
                            root._confirmingDelete = true
                        }
                    }
                }
            }
        }
    }

    // ---- Edit / Add dialog (sibling EditAddressDialog.qml) -------------
    EditAddressDialog {
        id: editDialog
        objectName: "editAddressDialog"
        anchors.centerIn: Overlay.overlay
        initialEntry: root._editing ? root._editing : null
        onSaved: function(entry) {
            var mgr = root._mgr
            if (!mgr) {
                root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Address book unavailable") + i18nManagerObj.emptyString), true)
                return
            }
            if (entry.id > 0) {
                var ok = mgr.updateAddressEntry(entry.id, entry.label,
                                                 entry.description, entry.paymentId)
                if (ok) {
                    console.log("AddressBookPage (st-063): updateAddressEntry OK id=", entry.id)
                    root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Updated %1").arg(entry.label) + i18nManagerObj.emptyString), false)
                } else {
                    root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Update failed") + i18nManagerObj.emptyString), true)
                }
            } else {
                var newId = mgr.addAddressEntry(entry.address, entry.label,
                                                 entry.description, entry.paymentId)
                if (newId > 0) {
                    console.log("AddressBookPage (st-063): addAddressEntry OK id=", newId)
                    root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Added %1").arg(entry.label) + i18nManagerObj.emptyString), false)
                } else {
                    root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Add failed: address required") + i18nManagerObj.emptyString), true)
                }
            }
            root._editing = null
        }
        onCancelled: {
            console.log("AddressBookPage (st-063): editCancelled")
            root._editing = null
        }
    }

    // ---- Delete confirmation --------------------------------------------
    Dialog {
        id: deleteDialog
        objectName: "deleteConfirmDialog"
        title: (i18nManagerObj.qtr("AddressBookPage", "Delete address?") + i18nManagerObj.emptyString)
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        visible: root._confirmingDelete
        onAccepted: {
            if (root._mgr) {
                var ok = root._mgr.removeAddressEntry(root._deleteTargetId)
                if (ok) {
                    console.log("AddressBookPage (st-063): removeAddressEntry OK id=", root._deleteTargetId)
                    root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Deleted") + i18nManagerObj.emptyString), false)
                } else {
                    root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Delete failed") + i18nManagerObj.emptyString), true)
                }
            }
            root._confirmingDelete = false
            root._deleteTargetId = 0
        }
        onRejected: {
            root._confirmingDelete = false
            root._deleteTargetId = 0
        }
        contentItem: Label {
            text: (i18nManagerObj.qtr("AddressBookPage", "Remove this address from your address book? This cannot be undone.") + i18nManagerObj.emptyString)
            wrapMode: Text.WordWrap
        }
    }

    // ---- Path-input dialog (Import / Export) ----------------------------
    // V1.0 minimalist replacement for QtQuick.Dialogs.FileDialog.
    // Asks the user to type the absolute path. Closes on OK (calls
    // the matching import/export method) or Cancel.
    Dialog {
        id: pathDialog
        objectName: "addressBookPathDialog"
        title: root._pathDialogMode === "export"
               ? (i18nManagerObj.qtr("AddressBookPage", "Export address book to JSON") + i18nManagerObj.emptyString)
               : (i18nManagerObj.qtr("AddressBookPage", "Import address book from JSON") + i18nManagerObj.emptyString)
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        visible: root._pathDialogMode !== ""
        width: 520

        onAccepted: {
            var p = root._pathDialogPath
            if (!p || p.replace(/^\s+|\s+$/g, "").length === 0) {
                root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Path is required") + i18nManagerObj.emptyString), true)
                root._pathDialogMode = ""
                return
            }
            if (root._mgr) {
                if (root._pathDialogMode === "import") {
                    console.log("AddressBookPage (st-063): importFile path=", p)
                    var ok = root._mgr.importAddressBook(p)
                    if (ok) {
                        var n = root._mgr.addressBookSize()
                        root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Imported %1 entries").arg(n) + i18nManagerObj.emptyString), false)
                    } else {
                        root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Import failed: invalid JSON") + i18nManagerObj.emptyString), true)
                    }
                } else if (root._pathDialogMode === "export") {
                    console.log("AddressBookPage (st-063): exportFile path=", p)
                    var ok2 = root._mgr.exportAddressBook(p)
                    if (ok2) {
                        root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Exported %1 entries").arg(root._count) + i18nManagerObj.emptyString), false)
                    } else {
                        root._setStatus((i18nManagerObj.qtr("AddressBookPage", "Export failed") + i18nManagerObj.emptyString), true)
                    }
                }
            }
            root._pathDialogMode = ""
        }
        onRejected: {
            root._pathDialogMode = ""
        }

        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: root._pathDialogMode === "export"
                      ? (i18nManagerObj.qtr("AddressBookPage", "Absolute path to write the JSON file:") + i18nManagerObj.emptyString)
                      : (i18nManagerObj.qtr("AddressBookPage", "Absolute path of the JSON file to import:") + i18nManagerObj.emptyString)
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: pathField
                objectName: "addressBookPathField"
                Layout.fillWidth: true
                placeholderText: (i18nManagerObj.qtr("AddressBookPage", "/home/user/address_book.json") + i18nManagerObj.emptyString)
                text: root._pathDialogPath
                onTextChanged: root._pathDialogPath = text
            }
            Label {
                text: (i18nManagerObj.qtr("AddressBookPage", "Example: /home/user/address_book.json") + i18nManagerObj.emptyString)
                color: "#888"
                font.pixelSize: 10
                Layout.fillWidth: true
            }
        }
    }
}
