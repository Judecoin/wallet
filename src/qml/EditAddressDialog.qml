// =============================================================================
// Judecoin Wallet - EditAddressDialog.qml (st-063)
//
// 5-field modal dialog for adding a new entry to the address book or
// editing an existing one. Mounted as a child of AddressBookPage.qml
// (and reachable from the Add / Edit buttons in the ListView delegate).
//
// Fields (per design_specs.md section 5.7 + st-063 spec_notes):
//   * Address        - required, plain text (public Judecoin address)
//   * Label          - short user-visible name
//   * Description    - longer free-form notes
//   * Payment ID     - legacy 16-hex id (hidden in V1.0 UI per V5.4.4 #6
//                      decision A but preserved for forward-compat with
//                      imported data). The UI keeps the field
//                      collapsed by default; advanced toggle reveals it.
//   * Read-only id   - hidden in add mode (id==0), shown as a read-only
//                      info row in edit mode
//
// API contract:
//   * property var initialEntry : null OR { id, address, label, description, paymentId }
//   * signal saved(entry)       - user clicked Save; entry has same shape
//                                  but with id either preserved (edit) or
//                                  0 (add, the parent will assign the real id).
//   * signal cancelled()        - user clicked Cancel, X, or pressed Esc.
//
// Validation:
//   * Address must be non-empty (whitespace-stripped) for Save to enable.
//   * The C++ side re-validates the address (and dedupes by address)
//     via AddressBook::addEntry, so a UI-side regex isn't strictly
//     needed. We do a basic non-empty check so the user gets feedback
//     before the round-trip.
//
// V5.4.7 compliance: no key/seed/password/privateKey is ever bound.
// V5.4.9 compliance: every visible string is qsTr() wrapped.
// V5.4.15 compliance: single Forest Green theme baked in.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "EditAddressDialog"

    // ---- public API -----------------------------------------------------
    /// Initial entry to edit. Set to a non-null object to switch to
    /// edit mode (fields pre-filled, id preserved); set to null to
    /// close the dialog. The parent flips this property to open/close.
    property var initialEntry: null

    /// Emitted on Save click. The parent forwards the entry to
    /// localDataManagerObj.addAddressEntry / updateAddressEntry.
    signal saved(var entry)

    /// Emitted on Cancel / X / Esc.
    signal cancelled()

    // ---- internal state -------------------------------------------------
    /// Working copy of the entry. Copied from initialEntry on
    /// Component.onCompleted so user edits don't mutate the parent.
    property var _entry: ({ id: 0, address: "", label: "",
                            description: "", paymentId: "" })

    /// True when initialEntry is non-null (dialog is in add or edit mode).
    readonly property bool _open: initialEntry !== null
    readonly property bool _editMode: _open && initialEntry && initialEntry.id > 0

    /// "advanced" toggle reveals the payment_id field. Default off
    /// (V1.0 hides it per V5.4.4 #6 decision A).
    property bool _showAdvanced: false

    /// True when the address is non-empty (after trim). Drives the
    /// Save button enable state.
    readonly property bool _addressValid:
        _entry && _entry.address && _entry.address.toString().replace(/^\s+|\s+$/g, "").length > 0

    // ---- dialog chrome --------------------------------------------------
    title: _editMode ? (i18nManagerObj.qtr("EditAddressDialog", "Edit address") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("EditAddressDialog", "Add address") + i18nManagerObj.emptyString)
    modal: true
    anchors.centerIn: Overlay.overlay
    standardButtons: Dialog.NoButton   // we own the buttons
    closePolicy: Popup.CloseOnEscape
    width: 480
    padding: 0

    // Sync _entry with initialEntry whenever the parent opens the dialog.
    onInitialEntryChanged: {
        if (initialEntry) {
            _entry = {
                id:          initialEntry.id          || 0,
                address:     initialEntry.address     || "",
                label:       initialEntry.label       || "",
                description: initialEntry.description || "",
                paymentId:   initialEntry.paymentId   || ""
            }
        }
    }

    // ESC + X both flow into the parent's cancelled() signal so the
    // parent can null out initialEntry and unmount us.
    onRejected: {
        console.log("EditAddressDialog (st-063): rejected (Esc / X)")
        root.cancelled()
    }

    background: Rectangle {
        color: "#ffffff"
        radius: 8
        border.color: "#dcfce7"
        border.width: 1
    }

    // ---- content --------------------------------------------------------
    contentItem: ColumnLayout {
        spacing: 0

        // Header bar (mirrors the ConfirmModal header for consistency).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "#16a34a"
            radius: 8
            // bottom corners are sharp (it joins the body)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.radius
                color: parent.color
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    text: root._editMode ? (i18nManagerObj.qtr("EditAddressDialog", "Edit address") + i18nManagerObj.emptyString) : (i18nManagerObj.qtr("EditAddressDialog", "Add address") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 16
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: "X"
                    onClicked: {
                        console.log("EditAddressDialog (st-063): X clicked")
                        root.cancelled()
                    }
                }
            }
        }

        // Body (form fields).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 12

            // id (read-only, edit mode only)
            Label {
                visible: root._editMode
                text: (i18nManagerObj.qtr("EditAddressDialog", "ID: %1").arg(_entry.id || 0) + i18nManagerObj.emptyString)
                    color: "#666"
                font.pixelSize: 11
            }

            // Address (required)
            Label {
                text: (i18nManagerObj.qtr("EditAddressDialog", "Address *") + i18nManagerObj.emptyString)
                font.bold: true
            }
            TextField {
                id: addressField
                objectName: "editDialogAddressField"
                Layout.fillWidth: true
                placeholderText: (i18nManagerObj.qtr("EditAddressDialog", "Enter destination address") + i18nManagerObj.emptyString)
                text: _entry.address || ""
                onTextChanged: {
                    _entry = Object.assign({}, _entry, { address: text })
                }
            }
            Label {
                visible: addressField.text.replace(/^\s+|\s+$/g, "").length === 0
                text: (i18nManagerObj.qtr("EditAddressDialog", "Address is required") + i18nManagerObj.emptyString)
                color: "#991b1b"
                font.pixelSize: 10
            }

            // Label
            Label {
                text: (i18nManagerObj.qtr("EditAddressDialog", "Label") + i18nManagerObj.emptyString)
                font.bold: true
            }
            TextField {
                id: labelField
                objectName: "editDialogLabelField"
                Layout.fillWidth: true
                placeholderText: (i18nManagerObj.qtr("EditAddressDialog", "Binance, Mom, etc.") + i18nManagerObj.emptyString)
                text: _entry.label || ""
                onTextChanged: {
                    _entry = Object.assign({}, _entry, { label: text })
                }
            }

            // Description
            Label {
                text: (i18nManagerObj.qtr("EditAddressDialog", "Description") + i18nManagerObj.emptyString)
                font.bold: true
            }
            TextArea {
                id: descriptionField
                objectName: "editDialogDescriptionField"
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                placeholderText: (i18nManagerObj.qtr("EditAddressDialog", "Optional notes") + i18nManagerObj.emptyString)
                text: _entry.description || ""
                wrapMode: TextArea.Wrap
                onTextChanged: {
                    _entry = Object.assign({}, _entry, { description: text })
                }
            }

            // Advanced toggle (reveals payment_id)
            CheckBox {
                objectName: "editDialogAdvancedToggle"
                text: (i18nManagerObj.qtr("EditAddressDialog", "Advanced (payment_id)") + i18nManagerObj.emptyString)
                checked: root._showAdvanced
                onCheckedChanged: root._showAdvanced = checked
            }

            // Payment ID (advanced)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: root._showAdvanced
                Label {
                    text: (i18nManagerObj.qtr("EditAddressDialog", "Payment ID") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                TextField {
                    id: paymentIdField
                    objectName: "editDialogPaymentIdField"
                    Layout.fillWidth: true
                    placeholderText: (i18nManagerObj.qtr("EditAddressDialog", "16 hex chars (legacy)") + i18nManagerObj.emptyString)
                    text: _entry.paymentId || ""
                    onTextChanged: {
                        _entry = Object.assign({}, _entry, { paymentId: text })
                    }
                }
            }
        }

        // Footer (action buttons).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "#f0fdf4"
            radius: 8
            // top corners sharp
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: parent.radius
                color: parent.color
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    objectName: "editDialogCancelButton"
                    text: (i18nManagerObj.qtr("EditAddressDialog", "Cancel") + i18nManagerObj.emptyString)
                    onClicked: {
                        console.log("EditAddressDialog (st-063): cancelClicked")
                        root.cancelled()
                    }
                }
                Button {
                    objectName: "editDialogSaveButton"
                    text: (i18nManagerObj.qtr("EditAddressDialog", "Save") + i18nManagerObj.emptyString)
                    enabled: root._addressValid
                    onClicked: {
                        console.log("EditAddressDialog (st-063): saveClicked id=",
                                    _entry.id, "label=", _entry.label)
                        // Hand the entry back to the parent. id is
                        // preserved as-is; the parent will forward to
                        // addAddressEntry (id==0) or updateAddressEntry.
                        root.saved(_entry)
                    }
                }
            }
        }
    }
}
