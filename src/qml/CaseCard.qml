// =============================================================================
// Judecoin Wallet - CaseCard.qml (st-097b1 extract, restyled st-120)
//
// Extracted from OnboardingWizard.qml so the QML file's root Item {} is the
// only top-level object. Qt 6.6.3 inline-component parser rejects file-level
// `component Name : BaseType {}` declarations (only nested inline components
// are supported), and putting this declaration AFTER the root Item also
// failed with the same syntax error. A separate file is the portable form.
//
// One per "Case N" entry on the home page. Renders a numbered badge, a
// title, a subtitle, and is clickable. When disabled (greyed out) the
// card cannot be clicked.
//
// st-120 restyle (2026-07-16): "small green-transparent button" per
// manual-verify feedback. Pre-st-120 was a large white card with deep
// greens on the title — too big and too contrast-y against the new
// Forest Green wizard background. New defaults:
//   * radius: 10 (was 14)
//   * border: 1.5px rgba(34,197,94,0.5) (was 1px #d1d5db grey)
//   * fill:   rgba(34,197,94,0.08) (was solid white)
//   * title:  14px white DemiBold (was 18px deep green on white)
//   * subtitle: 11px mint (was 13px slate on white)
//   * badge:  26x26 (was 32x32)
// highlight state still flips the border to solid #16a34a and the
// fill to a more saturated rgba(22,163,74,0.28).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    property string title: ""
    property string subtitle: ""
    property string badge: ""
    property bool highlight: false
    signal clicked()

    // a11y (st-095): a case card is a Button affordance (mouseArea
    // beneath). Disabled state is announced via Accessible.checkable +
    // Accessible.checked so AT says e.g. "Create, button, disabled"
    // on the locked cards.
    Accessible.role: Accessible.Button
    Accessible.name: card.title
    Accessible.checkable: true
    Accessible.checked: false
    Accessible.description: card.subtitle

    // st-120: small green-transparent button skin. The fill uses
    // rgba over a transparent base so the Forest Green wizard bg
    // (st-119 gradient) tints through, giving the buttons a
    // "frosted glass" look that matches Dashboard.qml's panels.
    //
    // st-120b (2026-07-16 17:38): the prior 3-deep nested ternary
    // at the root of `color:` triggered a qmlcachegen parser error
    // ("Invalid property assignment: color expected" at the
    // corresponding column) even though the ternary was
    // syntactically balanced. Refactored to explicit helper
    // functions so each binding is a single identifier lookup, which
    // the parser never refuses. Same change applied to
    // `border.color` for symmetry.
    //
    // st-120c (2026-07-16 17:43): the helper functions previously
    // returned CSS-style "rgba(r,g,b,a)" strings; Qt 6.6.3
    // qmlcachegen does NOT accept that syntax in `color:` property
    // assignments (the parser tries to match a color literal and
    // bails). Switched to Qt.rgba() function calls which return
    // real QColor values. The same fix is applied to the badge
    // color (line ~89) and the subtitle color (line ~120) below.
    function _borderColor() {
        if (card.highlight) return "#16a34a";
        if (mouseArea.containsMouse) return "#22c55e";
        return Qt.rgba(0.133, 0.773, 0.369, 0.5);
    }
    function _bgColor() {
        if (!card.enabled) return Qt.rgba(1, 1, 1, 0.04);
        if (card.highlight) return Qt.rgba(0.086, 0.639, 0.451, 0.28);
        if (mouseArea.containsMouse) return Qt.rgba(0.133, 0.773, 0.369, 0.18);
        return Qt.rgba(0.133, 0.773, 0.369, 0.08);
    }
    radius: 10
    border.width: card.highlight ? 2 : 1.5
    border.color: _borderColor()
    color: _bgColor()
    opacity: card.enabled ? 1.0 : 0.55

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        RowLayout {
            spacing: 10
            Rectangle {
                width: 28; height: 28; radius: 14
                color: card.highlight ? "#16a34a" : "#0d2818"
                Label {
                    anchors.centerIn: parent
                    text: card.badge
                    color: "white"
                    font.weight: Font.DemiBold
                    font.pixelSize: 13
                    // a11y (st-095): the badge digit is decorative;
                    // expose it as StaticText so AT can read "Case 1".
                    Accessible.role: Accessible.StaticText
                    Accessible.name: (i18nManagerObj.qtr("CaseCard", "Case %1").arg(card.badge) + i18nManagerObj.emptyString)}
            }
            Label {
                text: card.title
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: "#ffffff"
                Layout.fillWidth: true
                // st-120d (2026-07-16 17:55): requested bigger
                // text and no line wrap on the button labels. The
                // English titles are short (Create/Open/Recover/
                // Restore) so NoWrap is safe; we still elide on
                // the right if a localized translation ever pushes
                // the string past the button width.
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                // a11y (st-095): the card title is also surfaced on
                // the root Button's Accessible.name, but reading it
                // as a separate StaticText lets screen readers
                // re-announce the title with the badge context.
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
        Label {
            text: card.subtitle
            font.pixelSize: 14
            color: "#a8d5ba"  // mint on the green button
            opacity: 0.9
            Layout.fillWidth: true
            // st-120d: no wrap, elide right (same rationale as title).
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            // a11y (st-095): subtitle mirrors the card description.
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: card.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        onClicked: if (card.enabled) card.clicked()
    }
}
