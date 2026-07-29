// =============================================================================
// SidebarTab - one row in the sidebar (48px tall, full sidebar width).
//
// Extracted from Sidebar.qml as a standalone file because the inline
// `component SidebarTab : Rectangle` declaration was rejected by the
// Qt 6.6.3 qml engine bundled in the AppImage ("Syntax error for
// .currentItem"); the same shape works fine as a top-level type in
// its own .qml file.
// =============================================================================
// Judecoin Wallet - SidebarTab.qml (st-097b2 extract)
//
// Extracted from Sidebar.qml so the parent file's root Rectangle {} is the
// only top-level object. Qt 6.6.3 inline-component parser rejects file-level
// `component Name : BaseType {}` declarations (only nested inline components
// are supported), and putting this declaration AFTER the root Rectangle also
// failed with the same syntax error. A separate file is the portable form.
//
// SidebarTab - one row in the sidebar (48px tall, full sidebar width).
//
// When selected: 3px accent border on the LEFT edge, translucent green
// overlay (rgba(34,197,94,0.18)) over the gradient, white text at
// Font.DemiBold (≈600). When unselected: slate-300 (#cbd5e1) text,
// transparent background, no border. Hovering an unselected tab tints
// it slightly to telegraph interactivity.
//
// Each row carries a 18x18 inline SVG icon (PathSvg) that follows the
// text color: white when selected, slate-300 when not. The "currentColor"
// contract is honored by binding ShapePath.strokeColor to a property
// derived from the row's selected state.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Rectangle {
    id: tab

    property int index: 0
    property bool selected: false
    property string tabLabel: ""
    property string iconPath: ""
    signal clicked()

    // a11y (st-095): each tab is a PageTab with its label as the AT name;
    // the selected state is exposed via Accessible.checked so a screen
    // reader announces e.g. "Send, tab, selected" on the active tab.
    Accessible.role: Accessible.PageTab
    Accessible.name: tabLabel
    Accessible.checkable: true
    Accessible.checked: selected
    Accessible.description: (i18nManagerObj.qtr("SidebarTab", "Open the %1 page").arg(tabLabel) + i18nManagerObj.emptyString)// st-161 (2026-07-18): row height 52 -> 56, radius 10 -> 16,
    // pill widening from 4px -> 12px side margin so the active state
    // reads as a chunkier chip, background deepened from #15803d to
    // #14532d (--accent-darker).
    height: 56
    radius: 16

    // st-161 (2026-07-18): active pill background deepened
    // #15803d -> #14532d (--accent-darker) for a stronger brand
    // anchor on the white rail. Inactive hover keeps the soft
    // rgba(0,0,0,0.04) tint.
    color: selected ? "#14532d"
         : (hover.containsMouse ? Qt.rgba(0, 0, 0, 0.04) : "transparent")
    border.width: 0

    // st-161 (2026-07-18): pill widened by inflating the inner
    // Item's left/right margins from 4px -> 12px so the chip reads
    // chunkier and matches the reference mockup's wider active state.
    Item {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12

        // ---- icon (18x18 inline SVG, currentColor stroke) -------------
        // st-160 (2026-07-18): inactive icon color switched from
        // light slate (#cbd5e1, readable on green rail) to dark slate
        // (#374151 slate-700) so it reads on the white rail.
        Shape {
            id: tabIcon
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            antialiasing: true
            layer.enabled: true
            layer.samples: 4

            ShapePath {
                strokeColor: tab.selected ? "#ffffff" : "#374151"
                strokeWidth: 1.6
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathSvg { path: tab.iconPath }
            }
        }

        // ---- label ---------------------------------------------------
        // st-160 (2026-07-18): inactive label color flipped to
        // dark slate-800 (#1f2937) so it reads on the white rail.
        Label {
            anchors.left: tabIcon.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            verticalAlignment: Text.AlignVCenter
            text: tab.tabLabel
            color: tab.selected ? "#ffffff" : "#1f2937"
            font.pixelSize: 14
            font.weight: tab.selected ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
        }
    }

    // Hover state for mouse-friendly feedback on inactive tabs.
    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    // ---- click target (covers the whole row) --------------------------
    MouseArea {
        anchors.fill: parent
        onClicked: tab.clicked()
        cursorShape: Qt.PointingHandCursor
    }
}
