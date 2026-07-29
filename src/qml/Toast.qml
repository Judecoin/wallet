// =============================================================================
// Judecoin Wallet - Toast.qml (st-130)
//
// Top-center modal toast notification queue. Surfaces network / RPC / startup
// errors via a non-dismissing stack. While the queue is non-empty the rest of
// the UI is locked (MouseArea eats background events) so the user must
// acknowledge each error before continuing.
//
// Design points:
//   1. top-center                  — ColumnLayout anchors.top + horizontalCenter
//   2. persistent display (no auto-dismiss)    — no Timer; only the × button dismisses
//   3. queue stacking                  — Repeater on queue: [] (each toast = one entry)
//   4. category-based display                  — category field maps to a Chinese label
//                                  (daemon_timeout / connection_refused /
//                                   dns_failure / rpc_error /
//                                   remote_node_unreachable /
//                                   wallet_rpc_error / startup_failed / general)
//   5. red warning                  — error #dc2626 / warning #f59e0b / info #3b82f6
//   6. UI lock                  — Rectangle overlay + MouseArea eat events when
//                                  queue.length > 0
//
// Usage from QML:
//   globalToast.show("daemon_timeout", (i18nManagerObj.qtr("Toast", "Network Error") + i18nManagerObj.emptyString),
//                    "judecoind not responding", "error")
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: toastRoot
    anchors.fill: parent
    z: 9999   // above all other UI

    // ---- queue model ------------------------------------------------------
    // Each entry: { category, title, message, severity, timestamp }
    property var queue: []

    function show(category, title, message, severity) {
        queue = queue.concat([{
            category:  category || "general",
            title:     title    || (i18nManagerObj.qtr("Toast", "Error") + i18nManagerObj.emptyString),
            message:   message  || "",
            severity:  severity || "error",
            timestamp: Date.now()
        }])
    }

    function dismiss(index) {
        var q = queue.slice()
        q.splice(index, 1)
        queue = q
    }

    function dismissAll() {
        queue = []
    }

    // ---- helpers ----------------------------------------------------------
    function categoryLabel(c) {
        switch (c) {
            case "daemon_timeout":           return "Daemon timeout"
            case "connection_refused":       return "Connection refused"
            case "dns_failure":              return "DNS failure"
            case "rpc_error":                return "RPC error"
            case "remote_node_unreachable":  return "Remote node unreachable"
            case "wallet_rpc_error":         return "Wallet-RPC error"
            case "startup_failed":           return "Startup failed"
            case "daemon_start_failed":      return "Daemon start failed"
            case "wallet_rpc_start_failed":  return "Wallet-RPC start failed"
            case "version_mismatch":         return "Version mismatch"
            case "settings_invalid":         return "Invalid settings"
            default:                         return "Error"
        }
    }

    function severityColor(s) {
        switch (s) {
            case "error":   return "#dc2626"   // Tailwind red-600
            case "warning": return "#f59e0b"   // Tailwind amber-500
            case "info":    return "#3b82f6"   // Tailwind blue-500
            default:        return "#dc2626"
        }
    }

    // ---- modal lock overlay (design point 6) -------------------------------------
    Rectangle {
        id: lockOverlay
        anchors.fill: parent
        color: "#000000"
        opacity: 0.35
        visible: toastRoot.queue.length > 0

        MouseArea {
            anchors.fill: parent
            enabled: toastRoot.queue.length > 0
            // Eat every event so background controls (sidebar tabs, buttons)
            // can't be interacted with while errors are pending.
            onPressed:  function(mouse)  { mouse.accepted = true }
            onClicked:  function(mouse)  { mouse.accepted = true }
            onWheel:    function(wheel)  { wheel.accepted = true }
        }
    }

    // ---- toast stack (design points 1 + 3) ---------------------------------------
    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: 32
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 12
        width: Math.min(parent.width * 0.55, 640)

        Repeater {
            id: toastRepeater
            model: toastRoot.queue

            delegate: Rectangle {
                id: toastCard
                required property int index
                required property var modelData

                Layout.fillWidth: true
                Layout.preferredHeight: cardCol.implicitHeight + 24

                color: toastRoot.severityColor(modelData.severity)
                radius: 10
                border.color: "#ffffff"
                border.width: 2

                RowLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            text: toastCard.modelData.title
                            color: "#ffffff"
                            font.pixelSize: 14
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        Label {
                            text: toastRoot.categoryLabel(toastCard.modelData.category)
                                + " · " + toastCard.modelData.message
                            color: "#fef2f2"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                    }
                    Button {
                        text: "✕"
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        onClicked: toastRoot.dismiss(toastCard.index)
                        Accessible.role: Accessible.Button
                        Accessible.name: (i18nManagerObj.qtr("Toast", "Dismiss error message") + i18nManagerObj.emptyString)
                        ToolTip.visible: hovered
                        ToolTip.text: (i18nManagerObj.qtr("Toast", "Dismiss") + i18nManagerObj.emptyString)
                    }
                }
            }
        }
    }
}