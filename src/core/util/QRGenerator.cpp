// =============================================================================
// Judecoin Wallet - QRGenerator implementation (st-024)
// =============================================================================

#include "core/util/QRGenerator.h"

#include <qrencode.h>

#include <QByteArray>
#include <QPainter>

namespace judecoin_wallet {

namespace {

// Distribute `size` pixels across `n` slots so each slot gets either
// floor(size/n) or ceil(size/n) pixels, and the sum is exactly `size`.
// Returns the start index of each slot (length n+1) so caller can
// iterate [start[i], start[i+1]) to get slot i's pixel range.
//
// Matching QImage::scaled(IgnoreAspectRatio)'s rounding behaviour:
// the first (size - floor*n) slots get the extra pixel (i.e. earlier
// modules are slightly fatter; later modules slightly thinner).
void computePixelStarts(int n, int size, QVector<int> &startsOut)
{
    startsOut.resize(n + 1);
    startsOut[0] = 0;
    if (n <= 0) {
        return;
    }
    const int base = size / n;
    const int remainder = size - base * n;
    int acc = 0;
    for (int i = 0; i < n; ++i) {
        // Earlier slots absorb the rounding error.
        const int w = base + (i < remainder ? 1 : 0);
        acc += w;
        startsOut[i + 1] = acc;
    }
}

} // namespace

QImage QRGenerator::encode(const QString &text, int size)
{
    // Reject empty input early. qrencode treats NULL as "auto-detect
    // mode from content" which is fine but still requires non-empty
    // data, so an empty QString would deref-crash inside the library.
    // Return an isNull() image so QML renders nothing rather than
    // an undefined QImage.
    if (text.isEmpty()) {
        return QImage();
    }

    // Clamp size into [kMinSize, kMaxSize]. This keeps both the
    // quiet-zone proportion and the per-pixel memory budget sane.
    if (size < kMinSize) {
        size = kMinSize;
    } else if (size > kMaxSize) {
        size = kMaxSize;
    }

    // Encode in 8-bit mode (QR_MODE_8) so any byte in the input
    // round-trips. monero addresses contain base58 chars; the
    // judecoin: URI carries query-string bytes ('?', '=', '&',
    // percent-escapes); both work fine in 8-bit mode.
    //
    // version = 0 lets qrencode pick the smallest version that fits
    // (1 .. 40). For typical inputs (95-char address or a short URI)
    // this lands at version 5 or so, which is what monero wallet2
    // / monero GUI produce.
    //
    // casesensitive = 1: monero / judecoin addresses are case-sensitive
    // base58; the default 0 would fold 'O' vs 'o' etc.
    QByteArray utf8 = text.toUtf8();
    QRcode *qr = QRcode_encodeString(
        utf8.constData(),
        0,                              // version (auto)
        static_cast<QRecLevel>(kDefaultEccLevel), // QR_ECLEVEL_M
        QR_MODE_8,                      // 8-bit mode
        1                               // case sensitive
    );
    if (qr == nullptr) {
        // Capacity overflow (input > version 40 ECC M = ~2,953 bytes)
        // or internal error. Return null image so caller can fall back.
        return QImage();
    }

    // qrencode stores the symbol as 1 byte per module (0 = white,
    // 1 = black), width*width bytes total. Per libqrencode docs,
    // `width` is the pure module grid (no quiet zone). We add
    // kQuietZoneModules of white border on each side so the scanner
    // can locate the symbol reliably.
    const int symbolWidth = qr->width;
    const int totalModules = symbolWidth + 2 * kQuietZoneModules;

    if (totalModules <= 0 || size <= 0) {
        QRcode_free(qr);
        return QImage();
    }

    // Compute the per-module pixel band on each axis. Bands sum to
    // exactly `size` so the output image is exactly size x size.
    QVector<int> xStart, yStart;
    computePixelStarts(totalModules, size, xStart);
    computePixelStarts(totalModules, size, yStart);

    // Build the output image using QPainter for reliable cross-platform
    // rendering. QPainter on QImage handles color-space + format
    // correctly on every Qt backend (offscreen, X11, Wayland, WSLg).
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    // Walk the source grid row by row.
    for (int sy = 0; sy < symbolWidth; ++sy) {
        for (int sx = 0; sx < symbolWidth; ++sx) {
            const unsigned char module = qr->data[sy * symbolWidth + sx];
            if (module != 0) {
                continue; // white — inverted test for libqrencode 4.1.1
            }
            const int mx0 = sx + kQuietZoneModules;
            const int my0 = sy + kQuietZoneModules;
            const int x0 = xStart[mx0];
            const int y0 = yStart[my0];
            const int w  = xStart[mx0 + 1] - x0;
            const int h  = yStart[my0 + 1] - y0;
            painter.drawRect(x0, y0, w, h);
        }
    }
    painter.end();

    QRcode_free(qr);
    return image;
}

} // namespace judecoin_wallet
