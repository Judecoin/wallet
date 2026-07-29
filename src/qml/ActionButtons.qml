// =============================================================================
// Judecoin Wallet - ActionButtons.qml (st-051 + st-101c polish)
//
// Three-button row that lives directly under the BalanceCard on the Dashboard
// page. Provides the primary wallet affordances (Send / Receive / Subaddresses)
// without forcing the user to reach for the Sidebar for actions they perform
// constantly.
//
// st-101c reorders the visual roles so Receive (the most-used entry on the
// landing surface) reads as the primary CTA:
//   Send        -> secondary (neutral bg + external text)
//   Receive     -> primary   (accent bg + dark green text)
//   Subaddresses-> secondary (neutral bg + external text)
// All three share height / radius / padding so the row still reads as a
// balanced triplet (8px r-md radius, 12 vertical / 16 horizontal padding,
// 40px row height).
//
// st-051 contract (must-pass, updated for st-101c ordering):
//   * RowLayout: 3 Buttons of equal width filling the parent horizontally.
//   * Send (id: sendButton, objectName: "SendButton"):
//       - text:     (i18nManagerObj.qtr("ActionButtons", "Send") + i18nManagerObj.emptyString)
//       - role:     "secondary"   (st-101c; was primary in st-051)
//       - bg:       #d1fae5   (--neutral)
//       - fg:       #166534   (--external)
//       - hover:    #bbf7d0   (--border-card)
//       - pressed:  #86efac   (--input)
//       - radius:   8px       (--r-md per design_specs 4.2)
//       - padding:  12px vertical / 16px horizontal
//       - height:   40px (a touch taller than the theme.qss 36px default so
//                    the row reads as a hero CTA on the Dashboard)
//       - onClicked: emit signal sendClicked()
//   * Receive (id: receiveButton, objectName: "ReceiveButton"):
//       - text:     (i18nManagerObj.qtr("ActionButtons", "Receive") + i18nManagerObj.emptyString)
//       - role:     "primary"   (st-101c; was secondary in st-051)
//       - bg:       #22c55e   (--accent)
//       - fg:       #052e16   (--text-on-accent, near-black green)
//       - hover:    #4ade80   (--accent-light)
//       - pressed:  #16a34a   (--storage, slightly darker for tactile feedback)
//       - same radius / padding / height as Send
//       - onClicked: emit signal receiveClicked()
//   * Subaddresses (id: subaddressesButton, objectName: "SubaddressesButton"):
//       - text:     (i18nManagerObj.qtr("ActionButtons", "Subaddresses") + i18nManagerObj.emptyString)
//       - role:     "secondary"
//       - same colors as Send (both secondary; st-101c parity)
//       - same radius / padding / height
//       - onClicked: emit signal subaddressesClicked()
//   * objectName: "ActionButtons" on the root so visual-diff + QTest can
//     locate the row.
//   * font.pixelSize: 13, font.weight: Font.Bold (700) per
//     devplan/2026-07-13-judecoin-wallet-v1-fix-v2.specs.json §st-101c
//     "font system-stack/13/Bold". Was Font.DemiBold (600) in st-051;
//     st-101c bumps weight to match the mockup's heavier hero CTA label.
//
// V5.4.7 compliance: no key/seed/password is ever bound. The component is
//                   pure UI; parent (Dashboard.qml, future st-054) routes
//                   the click signals into navigation / wallet-rpc calls.
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs via
//                   src/i18n/*.ts). Tooltips are also qsTr().
// V5.4.15 compliance: single Forest Green theme baked in. No palette
//                    tweaks, no theme switch. Colors are hex literals
//                    sourced from docs/release/design_specs.md §2.1-2.2.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet selector.
//
// Implementation note: We style the Controls 2 Button background/color
// directly via `background: Rectangle { ... }` and `contentItem: Label`
// because theme.qss is a Qt Widgets stylesheet and does NOT reach into
// QML Controls 2 Buttons (Fusion style governs their default paint). The
// colors below are the same hex literals theme.qss carries, so a future
// port back to Widgets stays visually identical.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: actionButtons

    // ---- public API ----------------------------------------------------
    /// Marker for visual-diff / objectName queries.
    objectName: "ActionButtons"

    /// 8px gap between the 3 buttons (--sm per design_specs 4.1).
    spacing: 8

    /// Fill the row horizontally; height is the buttons' preferredHeight.
    Layout.fillWidth: true
    Layout.preferredHeight: 40
    implicitHeight: 40

    // ---- a11y v2 (st-100) chain entry points ---------------------------
    // Property aliases exposing the 3 inner buttons so Dashboard.qml
    // (the host) can wire the cross-component Tab chain into the
    // ActionButtons chain and out the other side. The aliases are
    // read-only by convention (the inner buttons are owned by the
    // component); the host only reads them when building its own
    // KeyNavigation bindings.
    readonly property alias sendButtonRef:          sendButton
    readonly property alias receiveButtonRef:       receiveButton
    readonly property alias subaddressesButtonRef:  subaddressesButton
    /// First focusable widget in the row (Send). Used by Dashboard.qml
    /// as the Tab-forward target on its page-level landmark so a
    /// keyboard-only user lands on the primary CTA.
    readonly property Item firstFocusable: sendButton
    /// Last focusable widget in the row (Subaddresses). The host wires
    /// its KeyNavigation.tab to the next focusable outside the row
    /// (first RecentTransactions row) so the chain crosses the page.
    readonly property Item lastFocusable:  subaddressesButton

    // ---- signals (consumed by Dashboard.qml in st-054) -----------------
    /// Emitted when the user clicks the Send button. Parent should switch
    /// to the Send page (or open the Send modal — to be decided in st-054).
    signal sendClicked()

    /// Emitted when the user clicks the Receive button. Parent should
    /// switch to the ReceivePage.qml scene (it already exists at
    /// src/qml/ReceivePage.qml).
    signal receiveClicked()

    /// Emitted when the user clicks the Subaddresses button. Parent should
    /// switch to the Subaddresses.qml page (sidebar tab #4 in st-041).
    signal subaddressesClicked()

    // ---- shared button geometry ----------------------------------------
    // Locked dimensions and font weight (design_specs 3.1 `body-bold`,
    // 4.1 `md`/`lg` padding, 4.2 `r-md` radius). Per-button bg/fg/hover
    // are set on the Button's own `background` Rectangle so a future
    // theme.qss refactor can replace this without touching geometry.

    // ---- buttons --------------------------------------------------------
    // st-101c ordering: Send(secondary) / Receive(primary) / Subaddresses(secondary).
    // The role swap moves the primary CTA surface to Receive (the most-used
    // landing action) while keeping Send and Subaddresses as the same neutral
    // triplet pair as before.
    //
    // Send — SECONDARY (neutral). The button visually sits at the left of the
    // triplet, but with the st-101c role swap the accent fill moved to
    // Receive. Send now uses --neutral bg + --external fg, matching
    // Subaddresses for visual parity.
    Button {
        id: sendButton
        objectName: "SendButton"
        text: (i18nManagerObj.qtr("ActionButtons", "Send") + i18nManagerObj.emptyString)

        // Geometry: equal-width, fill row, 40px tall.
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 40
        implicitWidth: 0
        implicitHeight: 40

        // Padding: 12px vertical / 16px horizontal (theme.qss default).
        leftPadding: 16
        rightPadding: 16
        topPadding: 12
        bottomPadding: 12

        // Font: 13px Bold (700) per st-101c spec_notes
        // "font system-stack/13/Bold". Was DemiBold (600) in st-051;
        // st-101c bumps weight to match the mockup's heavier hero CTA.
        // font.family left at the platform default (system sans-serif
        // stack) so the button reads identically on Windows / macOS /
        // Linux without bundling a custom face.
        font.pixelSize: 13
        font.weight: Font.Bold

        // a11y (st-095): the Send CTA is announced as a
        // button with the visible label.
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: (i18nManagerObj.qtr("ActionButtons", "Open the Send page to transfer funds") + i18nManagerObj.emptyString)
        // a11y v2 (st-100): chain entry. sendButton -> receiveButton
        // (forward). The backtab is wired externally (Dashboard.qml)
        // so Shift+Tab from Send can land on the page's previous
        // focusable (typically the first RecentTransactions row, or
        // nothing if the list is empty).
        KeyNavigation.tab: receiveButton
        activeFocusOnTab: true

        // Secondary palette (st-101c role swap; was primary in st-051).
        // --neutral / --external / --border-card / --input.
        background: Rectangle {
            radius: 8                            // --r-md
            color: sendButton.pressed
                ? "#86efac"                      // --input, pressed darker
                : (sendButton.hovered
                    ? "#bbf7d0"                  // --border-card, hover
                    : "#d1fae5")                 // --neutral, idle
            border.width: 0
            // Subtle inner ring at low alpha gives the button a slight
            // glassy depth (mirrors BalanceCard's border-card sheen).
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.color: Qt.rgba(20, 83, 45, 0.06)
                border.width: 1
            }
        }
        contentItem: Label {
            text: sendButton.text
            font: sendButton.font
            color: "#166534"                     // --external
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        onClicked: actionButtons.sendClicked()

        // Accessibility tooltip.
        ToolTip.visible: hovered
        ToolTip.text: (i18nManagerObj.qtr("ActionButtons", "Send funds to an address") + i18nManagerObj.emptyString)
        ToolTip.delay: 600
    }

    // Receive — PRIMARY (accent). st-101c promotes Receive to the hero CTA:
    // --accent bg + --text-on-accent fg + --accent-light hover + --storage
    // pressed. The role swap makes Receive the visual anchor of the
    // triplet; Send and Subaddresses are now both secondary.
    Button {
        id: receiveButton
        objectName: "ReceiveButton"
        text: (i18nManagerObj.qtr("ActionButtons", "Receive") + i18nManagerObj.emptyString)

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 40
        implicitWidth: 0
        implicitHeight: 40

        leftPadding: 16
        rightPadding: 16
        topPadding: 12
        bottomPadding: 12

        font.pixelSize: 13
        font.weight: Font.Bold

        // a11y (st-095): the Receive CTA mirrors Send's announcement.
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: (i18nManagerObj.qtr("ActionButtons", "Open the Receive page to generate a payment URI") + i18nManagerObj.emptyString)
        // a11y v2 (st-100): receiveButton -> subaddressesButton
        // (forward), sendButton -> receiveButton (backward).
        KeyNavigation.tab: subaddressesButton
        KeyNavigation.backtab: sendButton
        activeFocusOnTab: true

        // Primary palette (st-101c; was secondary in st-051).
        // --accent / --text-on-accent / --accent-light / --storage.
        background: Rectangle {
            radius: 8
            color: receiveButton.pressed
                ? "#16a34a"                      // --storage, pressed darker
                : (receiveButton.hovered
                    ? "#4ade80"                  // --accent-light, hover
                    : "#22c55e")                 // --accent, idle
            border.width: 0
            // Subtle inner ring at low alpha gives the button a slight
            // glassy depth (mirrors BalanceCard's accent sheen).
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.10)
                border.width: 1
            }
        }
        contentItem: Label {
            text: receiveButton.text
            font: receiveButton.font
            color: "#052e16"                     // --text-on-accent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        onClicked: actionButtons.receiveClicked()

        ToolTip.visible: hovered
        ToolTip.text: (i18nManagerObj.qtr("ActionButtons", "Receive funds to your wallet") + i18nManagerObj.emptyString)
        ToolTip.delay: 600
    }

    // Subaddresses — SECONDARY (neutral). st-101c keeps the Subaddresses
    // role as secondary; the palette matches Send so the two non-Receive
    // buttons form a symmetric pair around the central primary CTA.
    Button {
        id: subaddressesButton
        objectName: "SubaddressesButton"
        text: (i18nManagerObj.qtr("ActionButtons", "Subaddresses") + i18nManagerObj.emptyString)

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 40
        implicitWidth: 0
        implicitHeight: 40

        leftPadding: 16
        rightPadding: 16
        topPadding: 12
        bottomPadding: 12

        font.pixelSize: 13
        font.weight: Font.Bold

        // a11y (st-095): the Subaddresses CTA mirrors the other two.
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: (i18nManagerObj.qtr("ActionButtons", "Open the Subaddresses page to manage receive addresses") + i18nManagerObj.emptyString)
        // a11y v2 (st-100): the receiveButton already chained here
        // (KeyNavigation.tab: subaddressesButton). The forward
        // KeyNavigation.tab (subaddressesButton -> first
        // RecentTransactions row) is wired externally from
        // Dashboard.qml so this file stays decoupled from the
        // downstream sibling. The backtab returns to receiveButton.
        KeyNavigation.backtab: receiveButton
        activeFocusOnTab: true

        background: Rectangle {
            radius: 8
            color: subaddressesButton.pressed
                ? "#86efac"
                : (subaddressesButton.hovered
                    ? "#bbf7d0"
                    : "#d1fae5")
            border.width: 0
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.color: Qt.rgba(20, 83, 45, 0.06)
                border.width: 1
            }
        }
        contentItem: Label {
            text: subaddressesButton.text
            font: subaddressesButton.font
            color: "#166534"                     // --external
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        onClicked: actionButtons.subaddressesClicked()

        ToolTip.visible: hovered
        ToolTip.text: (i18nManagerObj.qtr("ActionButtons", "Manage subaddresses") + i18nManagerObj.emptyString)
        ToolTip.delay: 600
    }
}
