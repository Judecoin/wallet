// =============================================================================
// Judecoin Wallet - Settings.qml (st-023, extended in st-080)
//
// V1.1 Settings page entry point. Reachable from the dashboard via
// the "Settings" menu item. Renders an 8-category tree, with Network
// selected by default.
//
// Per st-080: each of the 8 categories now embeds a dedicated sub-page:
//   - network           -> NetworkSettings.qml    (full, st-023)
//   - language          -> LanguageSettings.qml   (9-locale dropdown + auto)
//   - auto_lock         -> AutoLockSettings.qml   (5/15/30/60min/Never)
//   - transfer_confirm  -> TransferConfirmSettings.qml (2 switches)
//   - appearance        -> AppearanceSettings.qml (read-only, locked theme)
//   - advanced          -> AdvancedSettings.qml   (collapsible diagnostic)
//   - reset             -> ResetSettings.qml      (double-confirm + resetRequested)
//   - about             -> AboutSettings.qml      (version grid)
//
// V5.4.9 compliance: pure QML, no Widgets.
// V5.4.7 compliance: this page never sees secret material - keys /
// passwords stay in WalletManager. Sub-pages use SettingsManager's
// Q_PROPERTYs (declared on src/core/settings/SettingsManager.h) so
// there is no secret-bearing string in any of them.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: settingsRoot
    objectName: "SettingsPage"

    // a11y (st-095): Settings root is a landmark Pane so AT can jump
    // to it directly by role + name; category buttons / Loader content
    // announce themselves through their own Accessible.* properties.
    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Settings", "Settings") + i18nManagerObj.emptyString)

    // -- public properties (set by main.qml context) ---------------------
    property var settingsManager: typeof settingsManagerObj !== "undefined"
        ? settingsManagerObj : null
    property var remoteNode: typeof remoteNodeObj !== "undefined"
        ? remoteNodeObj : null

    // -- internal state ------------------------------------------------
    property string currentCategory: "network"   // default to Network per spec

    // -- public signal: re-emitted from ResetSettings so the page host -
    // can react (e.g. drop in-memory cache, refresh dashboard). --------
    signal resetRequested()

    // a11y v2 (st-100): forward Tab chain. Pressing Tab from the
    // page-level landmark drops focus onto the first category button
    // (Network) so a keyboard-only user lands somewhere actionable.
    // The per-button chain (network -> language -> ... -> about -> cycle
    // back to network) is wired declaratively inside the Repeater
    // delegate via `categoryRepeater.itemAt((index + 1) % 8)`, which
    // is the standard QML pattern for keyboard nav across a dynamic
    // list. Mirrors SendPage.qml (no KeyNavigation.forward).
    KeyNavigation.tab: categoryRepeater.itemAt(0)

    // -- category -> sub-page map (used by the Loader) ------------------
    // st-170 (2026-07-19): temporarily hidden per request —
    // `transfer_confirm` / `appearance` / `advanced` / `reset` cases
    // are commented out. The matching model entries in the Repeater
    // above are also removed, so the Loader never sees them in
    // `currentCategory` in normal flow. The `default` arm returns
    // "" so a stale value would render an empty Loader (defensive).
    function _subPageFor(category) {
        switch (category) {
        case "network":          return "qrc:/qml/NetworkSettings.qml"
        case "language":         return "qrc:/qml/LanguageSettings.qml"
        case "auto_lock":        return "qrc:/qml/AutoLockSettings.qml"
        // case "transfer_confirm": return "qrc:/qml/TransferConfirmSettings.qml"
        // case "appearance":       return "qrc:/qml/AppearanceSettings.qml"
        case "security":         return "qrc:/qml/SecuritySettings.qml"
        // case "advanced":         return "qrc:/qml/AdvancedSettings.qml"
        // case "reset":            return "qrc:/qml/ResetSettings.qml"
        case "about":            return "qrc:/qml/AboutSettings.qml"
        default:                 return ""
        }
    }

    // ---- page background (st-171, 2026-07-19) ---------------------
    // Mirrors Dashboard.qml (st-161), SendPage.qml (st-165),
    // ReceivePage.qml (st-166), Subaddresses.qml (st-169). Horizontal
    // gradient: light-green (#f0fdf4) on the LEFT edge, transitions
    // to pure WHITE by 35% from the left, then stays white so the
    // right ~65% of the page reads as a clean white surface.
    Rectangle {
        id: pageBackground
        objectName: "SettingsPageBackground"
        anchors.fill: parent
        z: -1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#f0fdf4" }   // left edge
            GradientStop { position: 0.35; color: "#ffffff" }  // fast -> white
            GradientStop { position: 1.0; color: "#ffffff" }   // stays white
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 24
        anchors.bottomMargin: 24
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 20

        // -----------------------------------------------------------------
        // TopBar (st-171, 2026-07-19): matches SendPage.qml topBar
        // (st-167). Just the page title "Settings" — no back button,
        // no Forest Green rectangle header.
        // -----------------------------------------------------------------
        RowLayout {
            id: topBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 12

            Label {
                id: pageTitle
                objectName: "SettingsPageTitle"
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("Settings", "Settings") + i18nManagerObj.emptyString)
                color: "#166534"                       // --external (per design_specs §2)
                font.pixelSize: 18
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
                // a11y (st-095): heading role for the page title.
                Accessible.role: Accessible.Heading
                Accessible.name: text
                // a11y v2 (st-100): the title is a static landmark, so
                // we keep activeFocusOnTab off; the page-level
                // KeyNavigation.tab on settingsRoot already routes Tab
                // into the category button chain (categoryRepeater.itemAt(0)).
                // We expose a KeyNavigation.tab override here so the file
                // surfaces a declarative entry point for the chain -- this
                // also lets a future test key-focus the title and Tab
                // forward into the categories without touching the root.
                KeyNavigation.tab: categoryRepeater.itemAt(0)
            }
        }

        // Category picker. V1.1 ships all 8; each routes to its own
        // dedicated sub-page (st-080).
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                id: categoryRepeater
                model: [
                    { id: "network",         label: (i18nManagerObj.qtr("Settings", "Network") + i18nManagerObj.emptyString) },
                    { id: "language",        label: (i18nManagerObj.qtr("Settings", "Language") + i18nManagerObj.emptyString) },
                    { id: "auto_lock",       label: (i18nManagerObj.qtr("Settings", "Auto-Lock") + i18nManagerObj.emptyString) },
                    { id: "security",        label: (i18nManagerObj.qtr("Settings", "Security") + i18nManagerObj.emptyString) },
                    { id: "about",           label: (i18nManagerObj.qtr("Settings", "About") + i18nManagerObj.emptyString) }
                    // st-170 (2026-07-19): temporarily hidden
                    // per request — Transfer Confirm / Appearance
                    // / Advanced / Reset category buttons are not
                    // rendered, so the matching sub-pages are never
                    // loaded. To restore, uncomment the 4 entries
                    // below and re-add the matching `case` arms in
                    // _subPageFor() above.
                    // { id: "transfer_confirm",label: (i18nManagerObj.qtr("Settings", "Transfer Confirm") + i18nManagerObj.emptyString) },
                    // { id: "appearance",      label: (i18nManagerObj.qtr("Settings", "Appearance") + i18nManagerObj.emptyString) },
                    // { id: "advanced",        label: (i18nManagerObj.qtr("Settings", "Advanced") + i18nManagerObj.emptyString) },
                    // { id: "reset",           label: (i18nManagerObj.qtr("Settings", "Reset") + i18nManagerObj.emptyString) },
                ]
                delegate: Button {
                    id: categoryBtn
                    objectName: "SettingsCategory_" + modelData.id
                    text: modelData.label
                    checkable: true
                    checked: settingsRoot.currentCategory === modelData.id
                    // a11y (st-095): expose category buttons to AT. The
                    // checked state tracks the radio-button pattern via
                    // Accessible.checked so a screen reader announces
                    // "selected" on the currently active category.
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.checkable: true
                    Accessible.checked: checked
                    Accessible.description: (i18nManagerObj.qtr("Settings", "Open the %1 settings category").arg(modelData.label) + i18nManagerObj.emptyString)// a11y v2 (st-100): per-button Tab chain. The
                    // Repeater exposes sibling delegates via
                    // itemAt(index), so the chain wraps the visible
                    // categories into a single cycle. Per SendPage.qml:
                    // no KeyNavigation.forward.
                    KeyNavigation.tab: categoryRepeater.itemAt(
                        (index + 1) % categoryRepeater.count)
                    KeyNavigation.backtab: categoryRepeater.itemAt(
                        (index - 1 + categoryRepeater.count) % categoryRepeater.count)
                    activeFocusOnTab: true
                    Layout.preferredHeight: 32
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    // st-171 (2026-07-19): Forest Green toggle
                    // button palette (matches SendPage / ReceivePage /
                    // Subaddresses primary button styling). Unchecked
                    // -> white card w/ light-green border + dark-green
                    // text; checked -> saturated Forest Green fill +
                    // dark-green-on-accent text.
                    background: Rectangle {
                        radius: 6
                        color: categoryBtn.checked
                            ? (categoryBtn.pressed
                                ? "#16a34a"          // checked, pressed darker
                                : (categoryBtn.hovered
                                    ? "#4ade80"      // checked, hover
                                    : "#22c55e"))    // checked, idle
                            : (categoryBtn.pressed
                                ? "#dcfce7"         // unchecked, pressed
                                : (categoryBtn.hovered
                                    ? "#f0fdf4"     // unchecked, hover
                                    : "#ffffff"))   // unchecked, idle
                        border.color: categoryBtn.checked ? "#16a34a" : "#d1fae5"
                        border.width: 1
                    }
                    contentItem: Label {
                        text: categoryBtn.text
                        font: categoryBtn.font
                        color: categoryBtn.checked ? "#052e16" : "#14532d"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: settingsRoot.currentCategory = modelData.id
                }
            }
        }

        // Category content area: one Loader, source swapped whenever
        // currentCategory changes. The Loader is the SAME element so
        // QML reuses the underlying Item when possible and only tears
        // it down on a real switch.
        Loader {
            id: subPageLoader
            objectName: "subPageLoader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: settingsRoot._subPageFor(settingsRoot.currentCategory)
            // a11y v2 (st-100): the Loader container itself is a
            // landmark (Pane role), but we expose a KeyNavigation.tab
            // so a keyboard-only user can Shift+Tab back from the
            // sub-page content into the category button chain
            // (wraps to the last category, "about"). This keeps the
            // chain cyclic for the 8 categories even if a future
            // sub-page intercepts focus.
            KeyNavigation.tab: categoryRepeater.itemAt(0)
            KeyNavigation.backtab: categoryRepeater.itemAt(
                categoryRepeater.count - 1)

            // Re-emit resetRequested from the loaded ResetSettings so the
            // page host (main.qml's StackView or the dashboard router)
            // can pick it up. We use Connections rather than inline
            // onItemChanged because the Loader may briefly null out.
            Connections {
                target: subPageLoader.item
                ignoreUnknownSignals: true
                function onResetRequested() {
                    settingsRoot.resetRequested()
                }
            }
        }
    }
}
