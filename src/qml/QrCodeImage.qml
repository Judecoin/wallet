// =============================================================================
// QrCodeImage — QML wrapper around QrCodeItem (QQuickPaintedItem)
//
// FIX-G19 v3 (2026-07-20): QQuickPaintedItem + Nayuki qrcodegen.
// The C++ paint() method calculates a square region (min(width,height))
// and draws modules centered within it, so the QR output is always
// square — no aspect-ratio distortion even if the Layout allocates a
// non-square area for this item.
//
// FIX-G19 v2 (2026-07-20): replaced the image://qr/ Image provider
// pipeline with QQuickPaintedItem (QrCodeItem C++ class). QrCodeItem
// renders QR codes directly via QPainter in the QML scene graph — no
// Image element, no sourceSize/fillMode, no URL encoding/decoding.
//
// External API unchanged: `source` (text to encode) + `pixelSize`
// (square edge length in logical pixels). All existing callers
// (Subaddresses.qml, ReceivePage.qml) continue to work without changes.
// =============================================================================

import QtQuick
import QtQuick.Layouts
import judecoin.wallet.core 1.0

QrCodeItem {
    id: root

    source: ""
    pixelSize: 256

    // Force square dimensions at the QML level. The C++ side's
    // paint() also draws square within whatever bounds it gets,
    // so this is a belt-and-suspenders — both QML layout and C++
    // rendering enforce squareness.
    width: pixelSize
    height: pixelSize
    implicitWidth: pixelSize
    implicitHeight: pixelSize

    Layout.preferredWidth: pixelSize
    Layout.preferredHeight: pixelSize
}
