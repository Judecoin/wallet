// =============================================================================
// Judecoin Wallet - TxRow.qml (st-052)
//
// Single transaction row used by the RecentTransactions panel on the Dashboard
// and by the HistoryPage (st-053 + later). Reused across both surfaces so the
// row contract (properties + signals + visual) is locked in one place.
//
// st-052 contract (must-pass):
//   * Item root { RowLayout ... } — 32x32 circular avatar + ColumnLayout
//     (primary text + status line) + right-aligned amount block.
//   * Avatar:
//       - 32x32 px, radius 16 (full circle), font 12px Bold white.
//       - letter: counterparty[0] (uppercased) || "?"
//       - default background: --storage (#16a34a) for "in",
//                             --tx-icon-out (#ea580c) for "out".
//       - useDerivedAvatarColor=true: background derived from the first 8
//         characters of `counterparty` hashed through a 4-shade Forest Green
//         palette so adjacent rows never share the exact same color (per
//         st-052 spec_notes "avoid same color per row"). This mode is
//         opt-in; the default follows design_specs.md §5.5.
//   * Gap avatar-to-text: 10px (design_specs 5.5).
//   * Primary text: counterparty string (truncated to a single line +
//     middle-ellipsis when the address is long).
//   * Status line: micro token, --muted color. When `note` is non-empty the
//     status reads "note • <relative-time>" (e.g. "March rent • 2 hours
//     ago", V5.4 #8); otherwise just the relative-time.
//   * Direction glyph: up-arrow "↑" (in, color --tx-in #15803d) / down-arrow
//     "↓" (out, color --tx-out #c2410c) shown as a 14px Label immediately
//     to the left of the amount text. Extra affordance on top of the
//     color-coded avatar so the direction reads at a glance even on small
//     rows.
//   * Amount: body-bold, right-aligned, color --tx-in (in) / --tx-out (out).
//     Rendered as "<dir><sign><abs> JDE" with the absolute amount divided
//     from atomic units (1 JDE = 1000 atomic, mirrors BalanceCard.qml).
//   * Bottom border: 1px #dcfce7 (design_specs 5.5). The last row in the
//     list suppresses the border via `isLast`.
//   * Vertical padding 10/10, horizontal 12 (design_specs 5.5).
//   * LayoutMirroring.enabled = true so RTL locales mirror the row
//     (amount on the left, avatar on the right) without code changes.
//
// Public API:
//   property string  txHash        - 64-char monero tx id (hex)
//   property string  direction     - "in" | "out"
//   property string  counterparty  - address (95+ chars) or short label
//   property double  amount        - JDE atomic units (1 JDE = 1000 atomic)
//   property var     timestamp     - Date OR unix-ms int (seconds OK too);
//                                    defaults to current Date.now()
//   property string  note          - optional user note (e.g. "March rent")
//   property bool    isLast        - last row in the list (suppress border)
//   property bool    useDerivedAvatarColor - opt-in hash-derived avatar bg
//
//   signal rowClicked(txHash) - parent (RecentTransactions / HistoryPage)
//                              listens and pushes the tx detail page.
//
// V5.4.7 compliance: no key/seed/password is ever bound. txHash is public
//                   chain data; counterparty / note are user-supplied text.
// V5.4.9 compliance: qsTr() wraps every user-visible string (9 langs via
//                   src/i18n/*.ts).
// V5.4.15 compliance: single Forest Green theme baked in, no theme switch.
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet selector.
//
// Implementation note: avatar background is a `Rectangle` and the letter
// is a `Label` with anchors.centerIn. This keeps the row declarative and
// avoids QtQuick.Effects / DropShadow (not available on Qt 6.4.2). The
// amount is right-aligned via `Layout.alignment: Qt.AlignRight` + horizontal
// alignment on the Label.
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: txRow

    // ---- public API ----------------------------------------------------
    /// Monero transaction hash (64-char hex). Emitted back via rowClicked.
    property string txHash: ""

    /// "in" (received) or "out" (sent). Drives the avatar bg default,
    /// direction glyph, amount color and sign.
    property string direction: "in"

    /// Counterparty address or short label. The avatar letter derives
    /// from this string (counterparty[0] || "?").
    property string counterparty: ""

    /// JDE amount in atomic units (1 JDE = 1000 atomic). The display
    /// layer divides by 1000 to render JDE.
    property double amount: 0.0

    /// Transaction timestamp. Accepts a Date, a unix-seconds integer, or a
    /// unix-milliseconds integer (we sniff via magnitude).
    property var timestamp: new Date()

    /// Optional user note ("March rent", V5.4 #8). When non-empty the row
    /// prefixes the status line with this label.
    property string note: ""

    /// True when this row is the last one in the list. Suppresses the
    /// bottom border so the list does not draw a stray line past the last
    /// row (design_specs 5.5).
    property bool isLast: false

    /// When true, the avatar background is derived from the first 8 chars
    /// of `counterparty` (hash through a small Forest Green palette) so
    /// adjacent rows never share the exact same color. Default false to
    /// match design_specs.md §5.5 (per-direction color).
    property bool useDerivedAvatarColor: false

    // ---- signal --------------------------------------------------------
    /// Fired on click. Parent (RecentTransactions / HistoryPage) uses
    /// txHash to push the detail view (TxDetailDialog from design_specs
    /// §5.7).
    signal rowClicked(string txHash)

    // ---- derived state -------------------------------------------------
    /// Direction normalised to lower-case so the property is forgiving
    /// (QML bindings compare case-insensitively in practice but we keep
    /// it explicit).
    readonly property string _dirLower: (direction || "in").toString().toLowerCase()

    /// True when this is an inbound (received) transfer.
    readonly property bool _isIn: _dirLower === "in"

    /// Direction glyph: ↑ for in, ↓ for out.
    readonly property string _dirGlyph: _isIn ? "\u2191" : "\u2193"

    /// Color for the direction glyph + amount text. --tx-in for in,
    /// --tx-out for out (design_specs 2.2).
    readonly property color _amountColor: _isIn ? "#15803d" : "#c2410c"

    /// Color for the avatar background (default mode = per direction).
    /// --storage (#16a34a) for in, --tx-icon-out (#ea580c) for out
    /// (design_specs §5.5).
    readonly property color _avatarBgDefault: _isIn ? "#16a34a" : "#ea580c"

    /// Counterparty hash bucket (0..3) used when `useDerivedAvatarColor`
    /// is true. Hash is computed on the first 8 chars (after .toLowerCase)
    /// so case / length variations after char 8 do not flip the bucket.
    /// Returns 0..3 inclusive.
    readonly property int _avatarBucket: {
        if (!useDerivedAvatarColor) return -1
        var src = (counterparty || "").toString().toLowerCase()
        if (src.length === 0) return 0
        var slice = src.substring(0, 8)
        var h = 0
        for (var i = 0; i < slice.length; ++i) {
            h = ((h << 5) - h + slice.charCodeAt(i)) | 0
        }
        return Math.abs(h) % 4
    }

    /// Resolved avatar background: derived bucket when opted-in, otherwise
    /// the default per-direction color. The 4 buckets are 4 shades of
    /// Forest Green so the "no two adjacent rows share a color" invariant
    /// from st-052 spec_notes holds even within the same direction.
    readonly property color _avatarBg: {
        if (!useDerivedAvatarColor) return _avatarBgDefault
        switch (_avatarBucket) {
            case 0:  return "#16a34a"   // --storage
            case 1:  return "#22c55e"   // --accent
            case 2:  return "#15803d"   // --accent-dk
            case 3:  return "#4ade80"   // --accent-light / --process
            default: return _avatarBgDefault
        }
    }

    /// Avatar letter: first char of counterparty, upper-cased. Falls back
    /// to "?" when counterparty is empty (spec_notes avatar letter rule).
    readonly property string _avatarLetter: {
        var c = (counterparty || "").toString()
        if (c.length === 0) return "?"
        return c.substring(0, 1).toUpperCase()
    }

    /// Amount formatted as JDE (atomic / 1000), up to 5 fractional digits
    /// with trailing-zero stripping. Mirrors BalanceCard.qml's formatting
    /// so the Dashboard reads consistently.
    readonly property string _amountText: {
        var jde = amount / 1000.0
        var sign = _isIn ? "+" : "-"
        var abs = Math.abs(jde)
        var str
        if (abs === 0) {
            str = "0"
        } else if (abs >= 1) {
            // Up to 5 fractional digits, strip trailing zeros.
            str = abs.toLocaleString(Qt.locale(), 'f', 5)
            // Strip trailing zeros (and a possible trailing dot) produced
            // by fixed-precision formatting.
            if (str.indexOf('.') !== -1) {
                str = str.replace(/0+$/, '').replace(/\.$/, '')
            }
        } else {
            // Sub-1 JDE: show up to 5 fractional digits (no leading zeros
            // stripping so 0.0001 stays visible).
            str = abs.toLocaleString(Qt.locale(), 'f', 5)
            if (str.indexOf('.') !== -1) {
                str = str.replace(/0+$/, '').replace(/\.$/, '')
            }
        }
        return sign + str + " " + (i18nManagerObj.qtr("TxRow", "JDE") + i18nManagerObj.emptyString)
    }

    /// "March rent • 2 hours ago" when note non-empty, else "2 hours ago".
    readonly property string _statusText: {
        var ts = _toDate(timestamp)
        var rel = _relativeTime(ts)
        if (note && note.length > 0) {
            return note + " \u2022 " + rel   // bullet separator
        }
        return rel
    }

    // ---- helpers -------------------------------------------------------
    /// Coerce the `timestamp` property to a Date. Accepts Date, ISO 8601
    /// string, unix-seconds int, or unix-milliseconds int (sniffed via
    /// magnitude: > 10^12 implies ms, otherwise seconds).
    function _toDate(t) {
        if (t === undefined || t === null) return new Date()
        if (t instanceof Date) return t
        if (typeof t === "number") {
            // Unix ms: ~1.7e12 in 2026, unix s: ~1.7e9.
            if (t > 1e12) return new Date(t)
            return new Date(t * 1000)
        }
        if (typeof t === "string") {
            var d = new Date(t)
            if (!isNaN(d.getTime())) return d
            // Treat as unix seconds if purely numeric.
            var n = Number(t)
            if (!isNaN(n)) {
                if (n > 1e12) return new Date(n)
                return new Date(n * 1000)
            }
        }
        return new Date()
    }

    /// Render the timestamp as a localized relative-time string
    /// ("just now", "2 minutes ago", "3 hours ago", "2 days ago",
    /// "Mar 5"). Locale-aware via Qt.locale() so tr / ja / ar get
    /// correct number formatting.
    function _relativeTime(d) {
        var now = new Date()
        var diffMs = now.getTime() - d.getTime()
        if (diffMs < 0) diffMs = 0
        var sec = Math.floor(diffMs / 1000)
        if (sec < 45) return (i18nManagerObj.qtr("TxRow", "just now") + i18nManagerObj.emptyString)
        var min = Math.floor(sec / 60)
        if (min < 60) {
            return (i18nManagerObj.qtr("TxRow", "%1 minutes ago").arg(min.toLocaleString(Qt.locale() + i18nManagerObj.emptyString)))
        }
        var hr = Math.floor(min / 60)
        if (hr < 24) {
            return (i18nManagerObj.qtr("TxRow", "%1 hours ago").arg(hr.toLocaleString(Qt.locale() + i18nManagerObj.emptyString)))
        }
        var day = Math.floor(hr / 24)
        if (day < 7) {
            return (i18nManagerObj.qtr("TxRow", "%1 days ago").arg(day.toLocaleString(Qt.locale() + i18nManagerObj.emptyString)))
        }
        // Older than a week: short date label, e.g. "Mar 5".
        return d.toLocaleDateString(Qt.locale(), "MMM d")
    }

    // ---- root geometry -------------------------------------------------
    /// Row height is computed from padding + content. Default 56px matches
    /// the 32x32 avatar (10 + 32 + 10) so the avatar always reads with
    /// breathing room. The parent (ListView delegate) can override via
    /// `implicitHeight` if it needs tighter rows.
    implicitHeight: 56
    implicitWidth: 320

    /// Mirror in RTL locales (Hebrew / Arabic) so the avatar stays on the
    /// right edge of the window per design_specs §5.5.
    LayoutMirroring.enabled: true
    LayoutMirroring.childrenInherit: true

    // ---- background + bottom border ------------------------------------
    /// Hit-test surface covers the entire row so click works anywhere
    /// on the row, not just on the visible elements.
    Rectangle {
        id: rowSurface
        anchors.fill: parent

        // Slight hover affordance so the row reads as clickable.
        color: hoverArea.containsMouse ? "#f0fdf4" : "transparent"

        // Bottom border (1px #dcfce7 per design_specs 5.5). Last row
        // suppresses the line.
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: txRow.isLast ? 0 : 1
            color: "#dcfce7"
        }

        // Hover detection: covers the whole row.
        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // Visual pressed feedback.
            onPressed: rowSurface.color = "#dcfce7"
            onReleased: rowSurface.color = containsMouse ? "#f0fdf4" : "transparent"
            onClicked: txRow.rowClicked(txRow.txHash)
        }
    }

    // ---- content layout ------------------------------------------------
    /// RowLayout holds avatar (32x32) + text Column + amount block.
    /// padding 10/12 (design_specs 5.5), spacing 10 (design_specs 5.5
    /// gap avatar-to-text).
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 10

        // ---- avatar (32x32 circle) ------------------------------------
        Item {
            id: avatarWrap
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                id: avatarBg
                anchors.fill: parent
                radius: 16                  // full circle
                color: txRow._avatarBg
                // Subtle 1px inner ring mirrors BalanceCard / ActionButtons
                // so the avatar does not look sticker-flat.
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.color: Qt.rgba(255, 255, 255, 0.18)
                    border.width: 1
                }
            }

            Label {
                id: avatarLetter
                anchors.centerIn: avatarBg
                text: txRow._avatarLetter
                color: "#ffffff"
                font.pixelSize: 12
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                // Cap the visible letter to 1 character defensively even
                // though _avatarLetter already does so.
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        // ---- primary text + status ------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Label {
                id: primaryLabel
                Layout.fillWidth: true
                text: txRow.counterparty.length > 0 ? txRow.counterparty
                                                     : (i18nManagerObj.qtr("TxRow", "(no counterparty)") + i18nManagerObj.emptyString)
                // body-bold token per design_specs 3.1 (14px DemiBold).
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: "#0f172a"             // --fg
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }

            Label {
                id: statusLabel
                Layout.fillWidth: true
                text: txRow._statusText
                // micro token per design_specs 3.1 (12px Regular).
                font.pixelSize: 12
                font.weight: Font.Normal
                color: "#6b8074"             // --muted
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        // ---- direction glyph + amount (right-aligned) -----------------
        RowLayout {
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: 4

            Label {
                id: dirGlyphLabel
                text: txRow._dirGlyph
                color: txRow._amountColor
                font.pixelSize: 14
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Label {
                id: amountLabel
                text: txRow._amountText
                color: txRow._amountColor
                // body-bold per design_specs 3.1 + 5.5.
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                maximumLineCount: 1
                // Pin the amount column so long values do not push the
                // counterparty off the left edge.
                Layout.preferredWidth: 140
                Layout.minimumWidth: 100
                Layout.alignment: Qt.AlignRight
            }
        }
    }

    // ---- objectName (visual-diff + QTest) ------------------------------
    objectName: "TxRow"
}