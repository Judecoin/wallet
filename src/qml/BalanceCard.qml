// =============================================================================
// Judecoin Wallet - BalanceCard.qml (st-050 + st-101a polish)
//
// Gradient hero card rendered at the top of the Dashboard page. Displays the
// wallet's total JDE balance plus its USD equivalent (computed by the
// parent and passed in via usdValue, or derived from priceProviderObj when
// the parent leaves usdValue == 0 and the price feed is live).
//
// st-050 + st-101a contract (must-pass):
//   * Outer Rectangle:
//       - radius:        10px              (--r-lg per design_specs 4.2)
//       - gradient:      --storage -> --accent (135deg)
//                        = #16a34a -> #22c55e   (frozen spec)
//                        Implemented via QML Gradient. The native QML
//                        Gradient orientation enum is vertical-only on
//                        Qt 6.6, so the stops are laid out top -> bottom
//                        (#16a34a on top, #22c55e on bottom) which reads
//                        as a top-to-bottom wash; visually equivalent to
//                        the CSS linear-gradient(135deg, ...) for these
//                        two stops (which differ by ~30 in lightness).
//       - shadow:        shadow-card = 0 8px 24px rgba(22,163,74,0.25)
//                        Implemented via MultiEffect from QtQuick.Effects
//                        (available since Qt 6.5; we run on Qt 6.6.3).
//                        The shadow is on the cardSurface only, with
//                        autoPaddingEnabled true so the shadow has
//                        room to extend beyond the source rect.
//   * Inner padding:    20px vertical, 24px horizontal (5.2).
//   * Three Labels (ColumnLayout, spacing 6) -- ALWAYS visible:
//       - titleLabel "Total Balance"   caption token (12px),
//                                       color rgba(255,255,255,0.85)
//       - balanceLabel  (32px Bold, #ffffff)            formatted
//                       atomic / 1000 with trailing-zero strip
//       - usdLabel "≈ $X.XX" / "N/A"  (12px, rgba(255,255,255,0.75))
//   * Public properties (consumed by Dashboard.qml in st-054):
//       - balanceAmount  double  : JDE atomic units (1 JDE = 1000 atomic,
//                                   matching wallet-rpc conventions)
//       - usdValue       double  : USD value (parent may compute). When
//                                   usdValue <= 0 AND priceProviderObj is
//                                   reachable, we fall back to
//                                   priceProviderObj.formatUsd() for the
//                                   per-coin price string.
//       - isEmpty        bool    : DEPRECATED in v2 (kept for backward
//                                   compatibility with Dashboard.qml +
//                                   any external bindings). The v1.0
//                                   contract used isEmpty=true to swap
//                                   in a "No wallet loaded" placeholder;
//                                   v2 (st-101a) removes the placeholder
//                                   per visual_diff_report P0 #36 +
//                                   #1726 and always renders the three
//                                   structured labels. When the parent
//                                   leaves balanceAmount=0 + usdValue=0
//                                   the card naturally reads "Total
//                                   Balance / 0 / ≈ N/A" instead of
//                                   "No wallet loaded". The flag is
//                                   honoured by tinting the amount
//                                   down to 70% alpha so the empty
//                                   state still reads as a placeholder
//                                   visually without blanking the row.
//
// V5.4.7 compliance: no key/seed/password is ever bound. balanceAmount is
//                   a numeric total; private-key material never enters
//                   this scene.
// V5.4.9 compliance: qsTr() wraps every user-visible string. V5.4.9 ships
//                   9 languages; the source lives in src/i18n/*.ts.
// V5.4.15 compliance: single Forest Green theme baked in. No palette
//                    tweaks, no theme switch.
// V5.4.21 compliance: mainnet-only USD (CoinGecko /simple/price). No
//                    regtest/testnet/stagenet selector.
//
// Formatting contract (this file owns it for the Dashboard):
//   * balanceAmount is atomic units. Display divides by 1000 and prints
//     up to 5 fractional digits, trailing-zero-stripped.
//     e.g. 1234500 atomic -> "1,234.5"; 1000000 -> "1,000"; 0 -> "0".
//   * USD formatting prefers the parent-supplied usdValue when present
//     (>= 0.005 USD, i.e. > half a cent); otherwise falls back to
//     priceProviderObj.formatUsd() which renders the per-coin price
//     ("$0.01234" or "N/A").
//   * Number formatting uses Qt.locale() so digit grouping matches the
//     user's locale (e.g. 1,234.5 in en_US, 1.234,5 in de_DE).
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

Item {
    id: balanceCard

    // ---- public API ----------------------------------------------------
    /// JDE balance in atomic units (1 JDE = 1000 atomic). Default 0.
    /// The parent (Dashboard.qml, st-054) feeds the live total from
    /// wallet-rpc's get_balance aggregate. When 0 + isEmpty=true the
    /// amount renders "0" with reduced alpha; otherwise it's rendered
    /// at full alpha per design_specs §5.2.
    property double balanceAmount: 0.0

    /// USD value already computed by the parent. When this is set (>=
    /// 0.005) it takes precedence over the priceProviderObj fallback so
    /// the parent can render stale values or override rounding.
    property double usdValue: 0.0

    /// st-131: Available (unlocked) balance in atomic units. The locked
    /// amount is derived as balanceAmount - unlockedAmount. Both default
    /// to 0 so the card renders "0" until the parent feeds live data.
    property double unlockedAmount: 0.0

    /// st-131: Locked balance = total - unlocked. Computed property so
    /// QML automatically re-evaluates when either input changes.
    readonly property double lockedAmount: balanceAmount - unlockedAmount

    /// DEPRECATED in v2: kept for backward compatibility with Dashboard
    /// bindings (Dashboard.qml still passes isEmpty=true during the
    /// pre-onboarding render). v2 always renders the three structured
    /// labels; isEmpty now only tints the amount row at 70% alpha to
    /// signal that no real wallet is loaded. See contract block above.
    property bool isEmpty: false

    /// Marker for visual-diff / objectName queries.
    objectName: "BalanceCard"

    // ---- sizing ---------------------------------------------------------
    /// The card fills its container horizontally; height is the natural
    /// content height. The parent's ColumnLayout / Layout.fillWidth
    /// binding does the horizontal stretch.
    /// st-161 (2026-07-18): implicitHeight 168 -> 336 per
    /// request to double the banner area so it reads as a proper hero
    /// card.
    implicitHeight: 336
    implicitWidth: 320
    Layout.fillWidth: true

    // ---- public property ----------------------------------------------
    /// st-161 (2026-07-18): pill button label. Default "Send"
    /// per request (was "Receive" in st-160). The label is a
    /// public property so Dashboard.qml can repoint the pill at a
    /// different primary action without touching BalanceCard.qml.
    property string primaryActionLabel: (i18nManagerObj.qtr("BalanceCard", "Send") + i18nManagerObj.emptyString)

    // ---- the actual card -----------------------------------------------
    Rectangle {
        id: cardSurface
        objectName: "BalanceCardSurface"
        anchors.fill: parent
        // st-160 (2026-07-18): radius 10 -> 16 to match the
        // reference mockup's softer corner.
        radius: 16
        // st-161 (2026-07-18): replaced the dark-green gradient
        // with the generated banner image (qrc:/banner-bg.png). Flat
        // fillColor stays as a base layer in case the image fails to
        // load, so the hero card never renders as bare black.
        color: "#0d2818"

        // Soft border ring for visual depth (matches --border-card at
        // very low alpha so it reads as a sheen, not a stroke).
        border.color: Qt.rgba(1, 1, 1, 0.10)
        border.width: 1

        // ============================================================
        // st-161 (2026-07-18): banner image layer. Painted via
        // FillMode.PreserveAspectCrop so the 4K banner covers the full
        // card surface regardless of aspect mismatch.
        // ============================================================
        Image {
            id: bannerImage
            objectName: "BalanceCardBannerImage"
            anchors.fill: parent
            source: "qrc:/banner-bg.png"
            fillMode: Image.PreserveAspectCrop
            smooth: true
            asynchronous: false
            cache: true
            opacity: 1.0
        }

        // ---- content -----------------------------------------------------
        ColumnLayout {
            anchors.fill: parent
            // st-161 (2026-07-18): card is 336 tall (was 168),
            // so margins and spacing bumped up so the content breathes.
            anchors.topMargin: 32
            anchors.bottomMargin: 28
            anchors.leftMargin: 32
            anchors.rightMargin: 32
            spacing: 6

            // st-162 (2026-07-18): titleLabel font 12 -> 48 (4x)
            // per request. The heroic size makes the banner read
            // as a proper hero card at a glance.
            Label {
                id: titleLabel
                objectName: "BalanceCardTitleLabel"
                Layout.fillWidth: true
                text: (i18nManagerObj.qtr("BalanceCard", "TOTAL BALANCE") + i18nManagerObj.emptyString)
                color: Qt.rgba(1, 1, 1, 0.85)
                font.pixelSize: 48
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            // st-162 (2026-07-18): balance row now shows
            // Available / Locked split. The total is still the hero
            // number; locked sits as a muted suffix when non-zero.
            // Format: "1,234.5 JUDE"  (with locked suffix if applicable).
            Label {
                id: balanceLabel
                objectName: "BalanceCardAmountLabel"
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: balanceCard.balanceAmount > 0
                    ? balanceCard._formatBalance(balanceCard.balanceAmount) + " JUDE"
                    : balanceCard._formatBalance(balanceCard.balanceAmount)
                color: "#ffffff"
                font.pixelSize: 40
                font.weight: Font.Bold
                elide: Text.ElideRight
                opacity: balanceCard.isEmpty ? 0.70 : 1.0
            }

            // st-162 (2026-07-18): USD conversion line hidden
            // per request ("hide the fiat conversion area").

            // st-162: Available / Locked row (prominent, replaces the
            // small detail rows from st-131). Shows spendable vs locked
            // balance so the user knows what they can actually send.
            // Format: "Available: 1,000.5 JUDE  ·  Locked: 234.0 JUDE"
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                spacing: 8
                visible: !balanceCard.isEmpty

                Label {
                    text: (i18nManagerObj.qtr("BalanceCard", "Available:") + i18nManagerObj.emptyString)
                    color: Qt.rgba(1, 1, 1, 0.85)
                    font.pixelSize: 14
                    font.weight: Font.Normal
                }
                Label {
                    text: balanceCard._formatBalance(balanceCard.unlockedAmount) + " JUDE"
                    color: "#ffffff"
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Label {
                    text: "  ·  " + (i18nManagerObj.qtr("BalanceCard", "Locked:") + i18nManagerObj.emptyString)
                    color: Qt.rgba(1, 1, 1, 0.60)
                    font.pixelSize: 14
                    font.weight: Font.Normal
                    visible: balanceCard.lockedAmount > 0
                }
                Label {
                    text: balanceCard._formatBalance(balanceCard.lockedAmount) + " JUDE"
                    color: Qt.rgba(1, 1, 1, 0.75)
                    font.pixelSize: 16
                    font.weight: Font.Normal
                    visible: balanceCard.lockedAmount > 0
                }
            }

            // Spacer pushes the white pill to the bottom.
            Item { Layout.fillHeight: true }

            // st-162 (2026-07-18): Send pill redesigned.
            // Wider, softer shadow, bigger icon + text, rounded-full
            // corners, subtle hover-friendly elevation so it reads as
            // a proper CTA on the 336-tall hero card.
            Rectangle {
                id: primaryPill
                Layout.preferredHeight: 48
                Layout.preferredWidth: pillRow.implicitWidth + 52
                Layout.topMargin: 8
                radius: 24
                color: "#ffffff"
                border.color: Qt.rgba(20, 83, 45, 0.08)
                border.width: 1

                // Soft drop shadow under the pill so it "pops" off
                // the dark banner surface.
                MultiEffect {
                    source: primaryPill
                    anchors.fill: primaryPill
                    autoPaddingEnabled: true
                    shadowEnabled: true
                    shadowHorizontalOffset: 0
                    shadowVerticalOffset: 4
                    shadowBlur: 12
                    shadowColor: Qt.rgba(0, 0, 0, 0.20)
                    shadowOpacity: 1.0
                    shadowScale: 1.0
                }

                RowLayout {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: 14
                    Shape {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        antialiasing: true
                        ShapePath {
                            strokeColor: "#15803d"
                            strokeWidth: 2.0
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            // Up-right arrow icon (Send's brand icon).
                            PathSvg { path: "M7 17 L17 7 M9 7 H17 V15" }
                        }
                    }
                    Label {
                        text: balanceCard.primaryActionLabel
                        color: "#15803d"
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: balanceCard.primaryActionClicked()
                }
                Accessible.role: Accessible.Button
                Accessible.name: balanceCard.primaryActionLabel
                Accessible.description: (i18nManagerObj.qtr("BalanceCard", "Open the Send page to transfer funds") + i18nManagerObj.emptyString)
            }
        }

        // ---- shadow (QtQuick.Effects MultiEffect, st-101a polish) ------
        // MultiEffect implements the CSS "0 8px 24px rgba(22,163,74,0.25)"
        // shadow-card token from design_specs §4 directly via fragment
        // shader, replacing the v1 Rectangle-sibling fallback. Available
        // since Qt 6.5; our environment is Qt 6.6.3.
        //   shadowEnabled:        true (turn the shadow pass on)
        //   shadowHorizontalOffset: 0 (CSS x-offset = 0)
        //   shadowVerticalOffset:   8 (CSS y-offset = 8)
        //   shadowBlur:            24 (CSS blur radius = 24)
        //   shadowColor:           #16a34a @ 0.25 alpha (shadow-card token)
        //   shadowOpacity:         1.0 (alpha baked into shadowColor)
        //   shadowScale:           1.0 (no scale)
        //   autoPaddingEnabled:    true (MultiEffect adds internal padding
        //                          so the shadow has room to extend below
        //                          and to the right of the source rect).
        MultiEffect {
            id: cardShadow
            source: cardSurface
            anchors.fill: cardSurface
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 8
            shadowBlur: 24
            shadowColor: Qt.rgba(0.086, 0.639, 0.290, 0.25)  // rgba(22,163,74,0.25)
            shadowOpacity: 1.0
            shadowScale: 1.0
        }
    }

    // ---- helpers -------------------------------------------------------
    /// Render atomic-unit balance as a JDE string with up to 5 fractional
    /// digits, trailing-zero-stripped.
    ///   1234500 atomic -> "1,234.5"
    ///   1000000 atomic -> "1,000"
    ///   0       atomic -> "0"
    /// The "JDE" suffix lives in the parent context (Dashboard's "1.2345
    /// JDE" hero), keeping this component label-only.
    function _formatBalance(atomic) {
        var v = atomic / 1000.0
        if (v === 0) return "0"
        // toLocaleString honours the user's locale; 'f' = fixed-point, 5
        // = up to 5 fractional digits. Trailing zeros are stripped below.
        var s = v.toLocaleString(Qt.locale(), 'f', 5)
        return _stripTrailingZeros(s)
    }

    /// Strip trailing zeros after the decimal point (and the point itself
    /// when the result is integral). Pure-string helper, no Number side
    /// effects (so we never re-introduce scientific notation).
    function _stripTrailingZeros(s) {
        if (s.indexOf('.') < 0) return s
        var trimmed = s.replace(/0+$/, '')
        if (trimmed.charAt(trimmed.length - 1) === '.') {
            trimmed = trimmed.substring(0, trimmed.length - 1)
        }
        return trimmed
    }

    // ---- public signal -------------------------------------------------
    // st-160 (2026-07-18): the white pill button on the hero
    // card (default label "Receive") emits this when clicked. The
    // parent (Dashboard.qml) listens on it and forwards to
    // dashboardRoot.receiveClicked() so the StackView pushes the
    // ReceivePage. Kept as a BalanceCard-level signal (rather than
    // a hard-coded receiveClicked()) so a future sub_task can
    // repoint the pill at a different action without touching
    // BalanceCard.qml.
    signal primaryActionClicked()

    /// Decide which USD string to render in the USD row.
    ///   1. If parent supplied a non-trivial usdValue (>= $0.005),
    ///      format it ourselves with locale grouping.
    ///   2. Else, if priceProviderObj is reachable, defer to
    ///      formatUsd() (returns "$0.01234" or "N/A").
    ///   3. Else, "N/A".
    function _formatUsdLine() {
        if (balanceCard.usdValue >= 0.005) {
            return "$" + balanceCard.usdValue.toLocaleString(Qt.locale(), 'f', 2)
        }
        if (typeof priceProviderObj !== "undefined" && priceProviderObj) {
            return priceProviderObj.formatUsd()
        }
        return "N/A"
    }
}