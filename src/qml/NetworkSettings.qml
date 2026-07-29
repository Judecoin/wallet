// =============================================================================
// Judecoin Wallet - NetworkSettings.qml (st-023, st-132 explicit-save)
//
// Subpage of Settings.qml (Category::Network). Lets the user pick a
// remote node from a preset list, OR switch to "Custom..." and type a
// host:port by hand. Changes are NOT immediately written to QSettings;
// they go into a local _pending buffer and only commit when the user
// clicks "Save". "Discard" reverts _pending from the saved baseline.
//
// st-132 (2026-07-16 21:59): explicit-save mode. Before
// st-132 every change wrote straight to SettingsManager (which wrote
// to QSettings); the user saw no Save button and got no obvious cue
// when the write happened. With explicit save, the Save button is
// only enabled when _hasChanges is true, and an amber
// "\u26a0 Unsaved changes" label reminds the user. Also: st-132 adds
// a fallback string for the Active Daemon Target "Source: %1" label
// so we never see the raw "Source: undefined" the previous binding
// could fall through to during transient re-evaluation windows.
//
// V5.4.9 compliance: pure QML, no Widgets.
// V5.4.7 compliance: no secrets touched - just hostnames and ports.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: networkRoot
    objectName: "NetworkSettings"
    // st-175 (2026-07-19): removed clip — let root auto-expand
    // with content so parent layout can push subsequent sections down.
    // Expose content height so parent layouts can bind to it.
    readonly property alias contentHeight: contentColumn.implicitHeight

    // a11y (st-095): Network sub-page is a landmark Pane; preset picker
    // + host/port fields carry their own Accessible.* roles below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("NetworkSettings", "Network settings") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("NetworkSettings", "Choose a remote daemon and custom host/port") + i18nManagerObj.emptyString)

    // -- public properties (inherited from Settings.qml via Loader) ----
    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null
    property var remoteNode: typeof remoteNodeObj !== "undefined"
        ? remoteNodeObj : null

    // -- explicit-save state (st-132) -----------------------------------
    // _baseline = snapshot of saved values (read from settingsManager).
    // _pending  = user's draft values.
    // _hasChanges = true iff _pending differs from _baseline.
    // Component.onCompleted captures _baseline once; _commit / _discard
    // refresh it.
    property var _baseline: ({})
    property var _pending:  ({})
    property bool _hasChanges: false
    property bool _justSaved: false

    Component.onCompleted: _captureBaseline()

    function _captureBaseline() {
        if (settingsManager === null) {
            _baseline = {}
            _pending  = {}
            _hasChanges = false
            return
        }
        _baseline = {
            isCustomRemote:    settingsManager.isCustomRemote,
            presetNode:        settingsManager.presetNode,
            remoteDaemonHost:  settingsManager.remoteDaemonHost,
            remoteDaemonPort:  settingsManager.remoteDaemonPort
        }
        _pending = Object.assign({}, _baseline)
        _hasChanges = false
    }

    function _recomputeDirty() {
        if (settingsManager === null || !_baseline || !_pending) {
            _hasChanges = false
            return
        }
        var dirty = false
        for (var k in _baseline) {
            if (String(_pending[k]) !== String(_baseline[k])) {
                dirty = true
                break
            }
        }
        _hasChanges = dirty
    }

    function _commit() {
        if (settingsManager === null) return
        settingsManager.isCustomRemote   = _pending.isCustomRemote
        settingsManager.presetNode       = _pending.presetNode
        settingsManager.remoteDaemonHost = _pending.remoteDaemonHost
        settingsManager.remoteDaemonPort = _pending.remoteDaemonPort
        _captureBaseline()
        _justSaved = true
    }

    function _discard() {
        _captureBaseline()
        _justSaved = false
    }

    // st-172 (2026-07-19): the Custom host/port Frame pushes the
    // NetworkSettings content taller than the Loader's allocated
    // area. Without a Flickable, the QML ColumnLayout expands into
    // the sibling Loader area below, causing visual overlap with
    // Language / Security / etc. sub-pages (manual test 10:42).
    // Wrapping in a Flickable enables vertical scroll when the
    // Custom panel is visible and clips the overflow.
    Flickable {
        id: contentFlick
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

    ColumnLayout {
        id: contentColumn
        width: parent.width
        spacing: 12

        // -- Save / Discard bar (st-132 explicit-save) -----------------
    // st-172 (2026-07-19): restart note. Network changes only take
    // effect on the next app launch (daemon/wallet-rpc spawn decision
    // happens in StartupManager::stepStartDaemon, one-shot at boot).
    Label {
        id: restartNote
        visible: networkRoot._justSaved
        text: (i18nManagerObj.qtr("NetworkSettings", "Network changes will take effect after restart.") + i18nManagerObj.emptyString)
        color: "#f59e0b"
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: networkRoot._hasChanges
                    ? (i18nManagerObj.qtr("NetworkSettings", "\u26a0 Unsaved changes") + i18nManagerObj.emptyString)
                    : ""
                color: networkRoot._hasChanges ? "#f59e0b" : "transparent"
                font.italic: true
            }

            Button {
                objectName: "networkDiscardBtn"
                text: (i18nManagerObj.qtr("NetworkSettings", "Discard") + i18nManagerObj.emptyString)
                enabled: networkRoot._hasChanges
                onClicked: networkRoot._discard()
            }

            Button {
                objectName: "networkSaveBtn"
                text: (i18nManagerObj.qtr("NetworkSettings", "Save") + i18nManagerObj.emptyString)
                enabled: networkRoot._hasChanges
                highlighted: true
                Accessible.role: Accessible.Button
                Accessible.name: (i18nManagerObj.qtr("NetworkSettings", "Save network settings") + i18nManagerObj.emptyString)
                Accessible.description: (i18nManagerObj.qtr("NetworkSettings", "Persist the chosen remote node and custom host/port") + i18nManagerObj.emptyString)
                onClicked: networkRoot._commit()
            }
        }

        // -- Active Daemon Target status frame -------------------------
        Frame {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                Label {
                    text: (i18nManagerObj.qtr("NetworkSettings", "Active Daemon Target") + i18nManagerObj.emptyString)
                    font.bold: true
                }
                Label {
                    text: networkRoot.remoteNode !== null
                        ? networkRoot.remoteNode.activeLabel
                        : (i18nManagerObj.qtr("NetworkSettings", "(RemoteNodeManager not wired)") + i18nManagerObj.emptyString)
                    font.pixelSize: 14
                }
                Label {
                    visible: networkRoot.remoteNode !== null
                    // st-132 fix: "Source: undefined" red text.
                    // Coalesce empty/missing activeSourceString to
                    // "(unknown)" so the qsTr() never shows the raw
                    // sentinel. The previous binding
                    //   (i18nManagerObj.qtr("NetworkSettings", "Source: %1").arg(activeSourceString) + i18nManagerObj.emptyString)// surfaced "undefined" when activeSourceString
                    // returned a non-string during a transient
                    // re-evaluation window; the fallback now hides
                    // that.
                    text: {
                        if (networkRoot.remoteNode === null) return ""
                        var src = networkRoot.remoteNode.activeSourceString
                        return (i18nManagerObj.qtr("NetworkSettings", "Source: %1").arg(src || "(unknown)") + i18nManagerObj.emptyString)
                    }
                    font.italic: true
                }
            }
        }

        // -- Preset picker --------------------------------------------
        Label {
            text: (i18nManagerObj.qtr("NetworkSettings", "Remote Node") + i18nManagerObj.emptyString)
            font.bold: true
        }

        ComboBox {
            id: presetCombo
            Layout.fillWidth: true
            // Pre-populated at startup from remoteNode's preset list.
            // We pre-compute it here so the QML scene can be styled
            // independently of C++.
            // st-172 (2026-07-19): reduced to exactly two choices -
            // Local Daemon (spawn our own judecoind) and Custom
            // (user-typed host:port). The official/community/localhost
            // presets are gone from the UI; the C++ preset table
            // (findRemotePreset) stays for tests + legacy saved values.
            model: [
                { id: "",           label: (i18nManagerObj.qtr("NetworkSettings", "Local Daemon") + i18nManagerObj.emptyString) },
                { id: "__custom__", label: (i18nManagerObj.qtr("NetworkSettings", "Custom...") + i18nManagerObj.emptyString) }
            ]
            textRole: "label"
            valueRole: "id"
            // a11y (st-095): preset picker exposed as a combo box.
            Accessible.role: Accessible.ComboBox
            Accessible.name: (i18nManagerObj.qtr("NetworkSettings", "Remote node preset") + i18nManagerObj.emptyString)
            Accessible.description: (i18nManagerObj.qtr("NetworkSettings", "Pick a preset or switch to a custom host/port") + i18nManagerObj.emptyString)
            // st-132: read currentIndex from _pending so the ComboBox
            // reflects the user's draft (not the saved value). On Save,
            // _captureBaseline() refreshes both _pending and _baseline
            // to match, so this re-evaluates to the same value.
            // st-172: two-entry model - index 1 iff Custom is active.
            // A legacy preset id (official/community/localhost) has no
            // row anymore; it displays as Local here while the Active
            // Daemon Target frame above keeps showing the real resolved
            // source until the user saves a new choice.
            currentIndex: {
                if (!networkRoot._pending) return 0
                return networkRoot._pending.isCustomRemote === true ? 1 : 0
            }
            // st-132: write to _pending, NOT to settingsManager.
            // _recomputeDirty() updates the Save button's enabled state.
            // st-172 bug fix (manual test: selecting
            // Custom never showed the host/port inputs): the old
            // handler mutated members of the _pending JS object IN
            // PLACE. `property var` emits NO change signal on internal
            // mutation, so the custom host/port Frame's `visible:`
            // binding (reading _pending.isCustomRemote) never
            // re-evaluated. Build a fresh draft object and REASSIGN
            // networkRoot._pending so the notify fires and every
            // binding on _pending re-evaluates.
            onActivated: function(idx) {
                if (!networkRoot._pending) return
                var item = model[idx]
                var draft = Object.assign({}, networkRoot._pending)
                if (item.id === "__custom__") {
                    draft.isCustomRemote = true
                } else {
                    draft.isCustomRemote = false
                    draft.presetNode = ""
                }
                networkRoot._pending = draft
                networkRoot._recomputeDirty()
            }
        }

        // -- Custom host/port fields ----------------------------------
        Frame {
            Layout.fillWidth: true
            visible: networkRoot._pending
                && networkRoot._pending.isCustomRemote === true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: (i18nManagerObj.qtr("NetworkSettings", "Custom Host") + i18nManagerObj.emptyString); font.bold: true }
                TextField {
                    id: customHostField
                    Layout.fillWidth: true
                    placeholderText: (i18nManagerObj.qtr("NetworkSettings", "e.g. node.example.com") + i18nManagerObj.emptyString)
                    // st-132: read from _pending so Save/Discard state
                    // matches the visible text. On Save the baseline
                    // is refreshed and the binding stays consistent.
                    text: networkRoot._pending
                        ? (networkRoot._pending.remoteDaemonHost || "")
                        : ""
                    Accessible.role: Accessible.EditableText
                    Accessible.name: (i18nManagerObj.qtr("NetworkSettings", "Custom host") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("NetworkSettings", "Hostname or IP of the custom daemon") + i18nManagerObj.emptyString)
                    // st-132: onTextChanged (not onEditingFinished)
                    // so the Save button enables as the user types.
                    // The previous onEditingFinished wrote only on
                    // focus loss, which made "Save" feel unresponsive
                    // for live-typing feedback.
                    onTextChanged: {
                        if (!networkRoot._pending) return
                        networkRoot._pending.remoteDaemonHost = text
                        networkRoot._recomputeDirty()
                    }
                }

                Label { text: (i18nManagerObj.qtr("NetworkSettings", "Custom Port") + i18nManagerObj.emptyString); font.bold: true }
                SpinBox {
                    id: customPortField
                    Layout.fillWidth: true
                    from: 1
                    to: 65535
                    // st-132: read from _pending so Save/Discard state
                    // matches the visible value.
                    value: networkRoot._pending
                        ? (networkRoot._pending.remoteDaemonPort || 19061)
                        : 19061
                    Accessible.role: Accessible.SpinButton
                    Accessible.name: (i18nManagerObj.qtr("NetworkSettings", "Custom port") + i18nManagerObj.emptyString)
                    Accessible.description: (i18nManagerObj.qtr("NetworkSettings", "Daemon port between 1 and 65535 (default 19061)") + i18nManagerObj.emptyString)
                    // st-132: write to _pending, NOT to settingsManager.
                    onValueModified: {
                        if (!networkRoot._pending) return
                        networkRoot._pending.remoteDaemonPort = value
                        networkRoot._recomputeDirty()
                    }
                }
            }
        }
    }  // contentColumn
    }  // contentFlick
}
