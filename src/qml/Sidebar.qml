// =============================================================================
// Judecoin Wallet - Sidebar.qml (st-041 + st-098a + st-101b + st-102)
//
// Left-edge vertical navigation rail for the main app shell (post-onboarding).
// Fixed 180px-wide column rendered on top of a Forest Green gradient. Surfaces
// 5 primary tabs (Dashboard / Receive / Send / History / Subaddress) plus a
// 48x48 logo block + version line at the top, separated from the tab list by
// a 1px divider. Clicking a tab emits `tabClicked(pageName)` so the parent
// (main.qml) can StackView.replace() (or .push() for Receive) the matching
// Component.
//
// st-102 (v2 polish, decision 5a 2026-07-13, msg #57969): the v1.0
// 4-tab nav rail grew to 5 tabs to add Receive as the 2nd position
// (between Dashboard and Send). The mockup slot at position 2 was
// previously labeled "Undefined" - it now lands on Receive, which is
// the most common second-level wallet entry and pairs with Send as
// the 3rd tab. Implementation notes:
//   * tabNames / tabLabels / tabIconPaths arrays each grew from 4 to
//     5 entries; the 5th column (SidebarTab) is inserted between
//     the Dashboard and Send rows.
//   * Receive icon is the south_west arrow (top-right → bottom-left
//     diagonal), visually mirroring Send's ArrowUpRight (bottom-left →
//     top-right) - the same paired-axes convention Send and Receive
//     use in the rest of the v1.0 wallet (AddressBook + TxRowList
//     reuse it).
//   * main.qml routes Receive via mainStack.push(receivePage) so the
//     page's top-bar Back button can mainStack.pop() back to the
//     parent page (Dashboard) - mirrors the Subaddress push pattern
//     added in st-065.
//   * SidebarTab.qml is unchanged: it still binds index/tabLabel/
//     iconPath/selected via the parent, so the 5th row reuses the
//     existing 48px tab contract (3px accent border, rgba overlay,
//     18x18 PathSvg icon, qsTr label).
//   * Subaddress icon was already swapped to antenna in st-101b, so
//     st-102 carries that change over (no re-swap needed).
//
// st-101b visual polish (Sidebar batch B, 18 P0 fixes per
// devplan/2026-07-13-judecoin-wallet-v1-fix-v2.specs.json):
//   * sidebar_width stays 180 (already correct from st-098a; spec notes
//     "width 180 (currently 160/170/180 variants — unify to 180)" — the historical
//     multi-variant was collapsed in st-098a; this branch keeps the
//     unified 180 contract).
//   * logo block radius raised from 10 -> 12 (per visual_diff_report P0
//     "logo block radius 12" fix_hint; mockup uses r-xl rounded square).
//   * sidebar background gradient direction + stops verified #15803d
//     (top, --accent-dk) -> #16a34a (bottom, --accent-mid), matching the
//     mockup delta #1 gradient. Rendered via a bottom-anchored overlay
//     Rectangle (z:-1) so the gradient reads vertically without a
//     ShaderEffect; same contract that already passes test_sidebar.
//   * selected indicator already in place from st-098a: 3px left border
//     #22c55e + rgba(34,197,94,0.18) overlay + #fff text + Font.DemiBold.
//   * subaddress icon path-data swapped from hash (2 horizontal +
//     2 diagonal bars) to antenna (3 concentric signal arcs + base dot
//     + post + base bar), per visual_diff_report P0 "subaddress icon
//     should use qml/material-icons antenna.svg" fix_hint.
//   * appNameLabel "Judecoin" + appVersionLabel "Wallet v3.2.0" remain
//     rendered (test_sidebar_qml_logo_area mandates (i18nManagerObj.qtr("Sidebar", "Judecoin") + i18nManagerObj.emptyString) +
//     (i18nManagerObj.qtr("Sidebar", "Wallet v3.2.0") + i18nManagerObj.emptyString) — moved to StatusPill happens in st-101d,
//     not st-101b).
//
// st-098a responsive contract: the 180px rail width is INTENTIONAL and
// is the single fixed-width leaf of the v1.0 desktop shell (per the
// st-041 contract). The inner ColumnLayout uses anchors.fill + margins,
// the logo row uses Layout.fillWidth, the tab rows use Layout.fillWidth,
// and the divider bleeds to the sidebar edge via negative Layout margins
// so it always spans the full rail. No inner widths are hardcoded.
//
// st-041 contract (must-pass):
//   * sidebar_width:        180px (implicitWidth + Layout.preferredWidth)
//   * tab_height:           48px
//   * selected state:
//       - 3px left border accent (color #22c55e)
//       - background  rgba(34, 197, 94, 0.18) (translucent overlay on the
//         gradient)
//       - text color  #ffffff, font weight 600 (Font.DemiBold)
//   * unselected state:
//       - text color  #cbd5e1 (Tailwind slate-300)
//   * inline SVG icons:
//       - 18x18, currentColor stroke (white when selected, slate-300 when
//         not), single-stroke outline style
//       - Dashboard  = Home
//       - Send       = ArrowUpRight
//       - History    = Clock (circle + hour/minute hands)
//       - Subaddress = Antenna (3 concentric signal arcs + base dot +
//         vertical post + base bar; st-101b swapped from the older
//         hash-glyph path-data)
//   * logo area:
//       - top, 48x48 accent-green rounded square (radius 12, r-xl) + white "J" monogram
//       - text block to the right: "Judecoin" (bold) + "Wallet v3.2.0"
//         (kept under the logo block; st-101d moves them into StatusPill
//         when that component lands)
//       - 1px divider (#15803d) below the logo area before the tab list
//   * property selectedIndex: 0..4 for
//       Dashboard(0) / Receive(1) / Send(2) / History(3) / Subaddress(4)
//     drives the highlight. Clicking a tab sets selectedIndex AND emits
//     tabClicked(pageName). The locked order is the source of truth for
//     test_sidebar_qml_tab_names_locked_order; main.qml mirrors it via
//     _pageIndexFor() so the highlight follows the active StackView page.
//   * signal tabClicked(string pageName) — parent (main.qml) routes this
//     into StackView.replace(...).
//
// V5.4.7 compliance: no secret material; tabs are just page names.
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs via
//                   src/i18n/*.ts).
// V5.4.15 compliance: single Forest Green theme baked in, no theme switch.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet selectors.
//
// Note: Onboarding and Settings are NOT sidebar tabs (the --page CLI from
// st-033 + st-102 still routes to them, but the Sidebar only surfaces the
// 5 main shell tabs as primary nav affordance).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

Rectangle {
    id: sidebar

    // a11y (st-095): the sidebar is a navigation landmark; AT can list
    // its 5 tabs via the Accessible.role + per-row PageTab role below.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Sidebar", "Main navigation") + i18nManagerObj.emptyString)
    Accessible.description: (i18nManagerObj.qtr("Sidebar", "Primary app tabs: Dashboard, Receive, Send, History, Subaddress") + i18nManagerObj.emptyString)

    // ---- public API -----------------------------------------------------
    /// Canonical index of the active tab. Drives the highlight + icon color.
    /// 0 = Dashboard, 1 = Send, 2 = History, 3 = Subaddress. Setter is
    /// public so the parent (main.qml) can keep it in sync with the page
    /// currently mounted in the StackView (e.g. via initialPage from CLI).
    property int selectedIndex: 0

    /// Emitted when the user clicks a tab. The parent decides whether to
    /// StackView.replace(...) or to switch into the wizard (Onboarding).
    signal tabClicked(string pageName)

    // Locked order for the 5 tabs. Indexed by selectedIndex (0..4). The
    // icon paths live in SVG path-data mini-language; renderered via
    // PathSvg (Qt 6.4+ in QtQuick.Shapes, in SidebarTab.qml).
    readonly property var tabNames: ["Dashboard", "Receive", "Send", "History", "Subaddress", "Settings"]
    readonly property var tabLabels: [
        (i18nManagerObj.qtr("Sidebar", "Dashboard") + i18nManagerObj.emptyString),
        (i18nManagerObj.qtr("Sidebar", "Receive") + i18nManagerObj.emptyString),
        (i18nManagerObj.qtr("Sidebar", "Send") + i18nManagerObj.emptyString),
        (i18nManagerObj.qtr("Sidebar", "History") + i18nManagerObj.emptyString),
        (i18nManagerObj.qtr("Sidebar", "Subaddress") + i18nManagerObj.emptyString),
        (i18nManagerObj.qtr("Sidebar", "Settings") + i18nManagerObj.emptyString)
    ]
    readonly property var tabIconPaths: [
        // Dashboard: Home outline + door arch.
        "M4 21 V11 L12 4 L20 11 V21 Z " +
        "M10 21 V14 H14 V21",
        // Receive: south_west arrow (top-right → bottom-left diagonal),
        // mirror of Send's ArrowUpRight. Material-icons south_west:
        // M9 5 v2 h6.59 L4 18.59 5.41 20 17 8.41 V15 h2 V5 H9 z
        "M9 5 v2 h6.59 L4 18.59 5.41 20 17 8.41 V15 h2 V5 H9 z",
        // Send: ArrowUpRight (diagonal + top-right corner).
        "M7 17 L17 7 M9 7 H17 V15",
        // History: Clock circle (cubic-Bezier approximation) + 12/3 hands.
        "M3 12 C3 7 7 3 12 3 C17 3 21 7 21 12 C21 17 17 21 12 21 C7 21 3 17 3 12 Z " +
        "M12 7 V12 L15.5 14",
        // Subaddress: Antenna (3 concentric signal arcs + base dot + post
        // + base bar) per visual_diff_report P0 #33 + st-101b spec_note
        // "subaddress icon: hash.svg → qml/material-icons/antenna.svg".
        "M2 9 C 5 5 19 5 22 9 " +
        "M5 12 C 8 9 16 9 19 12 " +
        "M12 13 m-1 0 a1 1 0 1 0 2 0 a1 1 0 1 0 -2 0 " +
        "M12 16 V21 " +
        "M9 21 H15",
        // Settings: gear/cog icon (16-point outer circle + 10-point inner circle).
        "M12 2 L14 6 L18 4 L20 7 L17 10 L21 12 L20 16 L17 14 L12 18 " +
        "L12 22 L10 22 L10 18 L5 14 L2 16 L2 12 L6 10 L3 7 L5 4 L9 6 Z"
    ]

    // ---- visual contract (st-030 + st-041) -----------------------------
    // st-160 (2026-07-18): rail widened 180 -> 220 and background
    // switched from Forest Green gradient (#15803d -> #16a34a) to white
    // to match the reference mockup. The dark green pill on the active
    // tab still anchors the brand color; the rail itself reads as a
    // clean light surface so the deep-green hero card on the Dashboard
    // stays the visual focal point.
    implicitWidth: 220
    Layout.preferredWidth: 220
    Layout.fillHeight: true
    color: "#ffffff"

    // 1px right-edge divider so the white rail reads as a distinct panel
    // from the Dashboard's body-bg gradient. Color: subtle gray (#e5e7eb
    // slate-200) so it doesn't compete with the Forest Green accents.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: "#e5e7eb"
        z: 0
    }

    // ---- sidebar contents ----------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 8

        // ----------------------------------------------------------------
        // Logo area: real brand mark (qrc:/judecoin-logo.png) + text block
        // (Judecoin + Wallet vX.Y.Z). Logo size 40x40 (matches the
        // Onboarding headerBar sourceSize); text + chevron sit to the
        // right.
        // ----------------------------------------------------------------
        RowLayout {
            id: logoRow
            Layout.fillWidth: true
            Layout.leftMargin: 0
            Layout.rightMargin: 0
            Layout.topMargin: 4
            Layout.bottomMargin: 12
            spacing: 12

            // st-160: replace the "J" monogram with the real
            // qrc:/judecoin-logo.png asset (same PNG used in the
            // Onboarding headerBar, registered via brand_assets in
            // src/CMakeLists.txt). 40x40, PreserveAspectFit so it
            // scales without distortion.
            Image {
                id: logoMark
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                source: "qrc:/judecoin-logo.png"
                sourceSize.width: 40
                sourceSize.height: 40
                fillMode: Image.PreserveAspectFit
                smooth: true
                Accessible.role: Accessible.StaticText
                Accessible.name: (i18nManagerObj.qtr("Sidebar", "Judecoin logo") + i18nManagerObj.emptyString)
            }

            // Two-line title block next to the logo.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("Sidebar", "Judecoin") + i18nManagerObj.emptyString)
                    color: "#166534"           // --external (dark green)
                    font.pixelSize: 18
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: (i18nManagerObj.qtr("Sidebar", "Wallet v3.2.0") + i18nManagerObj.emptyString)
                    color: "#6b8074"           // --muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            // st-160 (2026-07-18): small chevron next to the
            // wordmark, matching the reference mockup's logo row. Reads
            // as a dropdown indicator for the brand / wallet selector.
            Label {
                Layout.alignment: Qt.AlignVCenter
                text: "\u2304"   // ⌄
                color: "#6b8074"           // --muted (matches sub-label)
                font.pixelSize: 16
            }
        }

        // 1px divider separating the logo area from the tab list.
        // Color is a soft gray so it reads as a bevel on the white rail.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 12
            Layout.leftMargin: -8    // bleed to sidebar edge so the divider
            Layout.rightMargin: -8   // runs flush across the rail
            height: 1
            color: "#e5e7eb"
        }

        // ----------------------------------------------------------------
        // Five primary tabs (Dashboard / Receive / Send / History /
        // Subaddress). Order is locked by st-033 + st-102 kAllowedPages
        // and mirrored 1:1 by main.qml _pageIndexFor() so the highlight
        // follows the active StackView page.
        // ----------------------------------------------------------------
        SidebarTab {
            Layout.fillWidth: true
            index: 0
            selected: sidebar.selectedIndex === 0
            tabLabel: sidebar.tabLabels[0]
            iconPath: sidebar.tabIconPaths[0]
            onClicked: sidebar._selectTab(0)
        }
        SidebarTab {
            Layout.fillWidth: true
            index: 1
            selected: sidebar.selectedIndex === 1
            tabLabel: sidebar.tabLabels[1]
            iconPath: sidebar.tabIconPaths[1]
            onClicked: sidebar._selectTab(1)
        }
        SidebarTab {
            Layout.fillWidth: true
            index: 2
            selected: sidebar.selectedIndex === 2
            tabLabel: sidebar.tabLabels[2]
            iconPath: sidebar.tabIconPaths[2]
            onClicked: sidebar._selectTab(2)
        }
        SidebarTab {
            Layout.fillWidth: true
            index: 3
            selected: sidebar.selectedIndex === 3
            tabLabel: sidebar.tabLabels[3]
            iconPath: sidebar.tabIconPaths[3]
            onClicked: sidebar._selectTab(3)
        }
        SidebarTab {
            Layout.fillWidth: true
            index: 4
            selected: sidebar.selectedIndex === 4
            tabLabel: sidebar.tabLabels[4]
            iconPath: sidebar.tabIconPaths[4]
            onClicked: sidebar._selectTab(4)
        }
        SidebarTab {
            Layout.fillWidth: true
            index: 5
            selected: sidebar.selectedIndex === 5
            tabLabel: sidebar.tabLabels[5]
            iconPath: sidebar.tabIconPaths[5]
            onClicked: sidebar._selectTab(5)
        }

        // Push everything below to the bottom, then render the
        // sync block at the very bottom of the sidebar rail.
        Item { Layout.fillHeight: true }

        // ---- Sync status block (st-162) -----------------------
        // Rendered at the bottom of the sidebar rail.
        // Shows real-time blockchain sync progress from daemonClientObj
        // with a colored status dot, current/target block numbers on
        // separate lines, and a progress bar. No background, no border.
        // All labels in English per request.
        //
        // st-164 (2026-07-19): light-gray background (#f5f5f5),
        // full sidebar width (negative margins bleed past the ColumnLayout
        // 20px side margins to span the entire 220px rail).
        Rectangle {
            id: syncBlockContainer
            Layout.fillWidth: true
            Layout.leftMargin: -20
            Layout.rightMargin: -20
            Layout.bottomMargin: -24   // bleed to bottom edge
            implicitHeight: syncColumn.implicitHeight + 32
            color: "#f5f5f5"

            // ---- sync state helpers (reactive, bind to daemonClientObj) --
            readonly property string _statusLabel: {
                if (typeof daemonClientObj === "undefined" || !daemonClientObj)
                    return "Synced"
                if (!daemonClientObj.isHealthy)
                    return "Offline"
                if (daemonClientObj.isSynchronized)
                    return "Synced"
                // st-163: daemon may return height==target even when
                // synchronized flag is still false. Treat as synced.
                const h = daemonClientObj.lastHeight || 0
                const t = daemonClientObj.targetHeight || 0
                if (t > 0 && h >= t) return "Synced"
                return "Syncing"
            }
            readonly property string _dotColor: {
                if (syncBlockContainer._statusLabel === "Offline") return "#dc2626"
                if (syncBlockContainer._statusLabel === "Syncing") return "#d97706"
                return "#16a34a"
            }
            // st-163: synced text uses dark green (#14532d), not the dot's bright green.
            readonly property string _labelColor: {
                if (syncBlockContainer._statusLabel === "Offline") return "#dc2626"
                if (syncBlockContainer._statusLabel === "Syncing") return "#d97706"
                return "#14532d"
            }
            readonly property int _heightVal: {
                if (typeof daemonClientObj === "undefined" || !daemonClientObj)
                    return 0
                return daemonClientObj.lastHeight || 0
            }
            readonly property int _targetVal: {
                if (typeof daemonClientObj === "undefined" || !daemonClientObj)
                    return 0
                return daemonClientObj.targetHeight || 0
            }
            readonly property real _progressVal: {
                const h = syncBlockContainer._heightVal
                if (h <= 0) return 0
                // st-163: when synced, bar is full (always visible).
                if (syncBlockContainer._statusLabel !== "Syncing")
                    return syncBlockContainer._statusLabel === "Synced" ? 1.0 : 0.0
                const t = syncBlockContainer._targetVal
                if (t > 0 && h >= t) return 1.0
                if (t > h) return Math.min(1.0, h / t)
                return Math.min(0.99, h / (h + 10000))
            }

            ColumnLayout {
                id: syncColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 26
                anchors.bottomMargin: 16
                spacing: 8

                // Status row: colored dot + state label.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: syncBlockContainer._dotColor
                    }
                    Label {
                        text: syncBlockContainer._statusLabel
                        color: syncBlockContainer._labelColor
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                // st-163: current block on its own line (bold, green).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: syncBlockContainer._heightVal > 0
                    Label {
                        text: (i18nManagerObj.qtr("Sidebar", "Current") + i18nManagerObj.emptyString)
                        color: "#6b8074"
                        font.pixelSize: 11
                    }
                    Label {
                        text: syncBlockContainer._heightVal > 0
                            ? syncBlockContainer._heightVal.toLocaleString() : "--"
                        color: "#166534"
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                }

                // st-163: target block on its own line (muted, gray).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: syncBlockContainer._targetVal > 0
                          && syncBlockContainer._targetVal > syncBlockContainer._heightVal
                    Label {
                        text: (i18nManagerObj.qtr("Sidebar", "Target") + i18nManagerObj.emptyString)
                        color: "#9ca3af"
                        font.pixelSize: 11
                    }
                    Label {
                        text: syncBlockContainer._targetVal.toLocaleString()
                        color: "#9ca3af"
                        font.pixelSize: 13
                    }
                }

                // Progress bar (st-163: 12px tall, was 3px = 4x).
                // Always visible: full green when synced, partial when syncing,
                // empty when offline.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 12
                    radius: 6
                    color: "#e5e7eb"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * syncBlockContainer._progressVal
                        radius: 6
                        color: "#16a34a"
                    }
                }
            }
        }

        // Push everything else to the bottom (footer slot in a later sub-task).
        Item { Layout.fillHeight: true }
    }

    // ---- helpers -------------------------------------------------------
    /// Programmatic tab activation: updates selectedIndex + emits signal.
    /// Used by the SidebarTab onClicked handler AND can be called from
    /// the parent for parity (e.g. keyboard shortcuts in a later sub-task).
    function _selectTab(index) {
        if (index < 0 || index >= tabNames.length) return
        if (selectedIndex !== index) selectedIndex = index
        tabClicked(tabNames[index])
    }
}

// SidebarTab definition moved to its own file: src/qml/SidebarTab.qml.
// (Same reason as CaseCard -- inline `component` keyword doesn't parse
//  in the Qt 6.6.3 qml engine bundled in the AppImage.)
