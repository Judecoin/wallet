// =============================================================================
// Judecoin Wallet - QrCodeItem (FIX-G19 v3, 2026-07-20)
//
// QQuickPaintedItem that renders a QR code directly in the QML scene
// graph. Uses Nayuki's qrcodegen library (recommended by Qt 6.6 QR
// rendering guide) for matrix generation + QPainter for rendering.
//
// Rendering approach (matching the Google/Qt 6.6 recommendation):
//   1. Generate matrix via qrcodegen::QrCode::encodeText()
//   2. Calculate square region within bounds: side = min(width, height)
//   3. Draw each module as a filled rectangle at calculated scale
//   4. Center within the item bounds via offsetX/offsetY
//
// This guarantees the QR code is always square regardless of the
// Layout-allocated item size, eliminating the aspect-ratio distortion
// that plagued the old Image provider + anchors.fill approach.
// =============================================================================

#include "core/util/QrCodeItem.h"
#include "core/util/qrcodegen.h"

#include <QPainter>

namespace judecoin_wallet {

// Match the Google example: 2-module margin on each side
static constexpr int kMarginModules = 2;

QrCodeItem::QrCodeItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_pixelSize(256)
{
    setFlag(QQuickItem::ItemHasContents, true);
    setSmooth(false);
    setAntialiasing(false);
    setRenderTarget(QQuickPaintedItem::Image);

    // Immediate implicit size so the Layout sees a square from frame 0.
    // regenerate() is NOT called here because source is still empty;
    // we set implicit manually to prevent a 0×0 layout collapse.
    setImplicitWidth(m_pixelSize);
    setImplicitHeight(m_pixelSize);
}

QString QrCodeItem::source() const { return m_source; }

void QrCodeItem::setSource(const QString &src)
{
    if (m_source != src) {
        m_source = src;
        regenerate();
        emit sourceChanged();
    }
}

int QrCodeItem::pixelSize() const { return m_pixelSize; }

void QrCodeItem::setPixelSize(int size)
{
    if (size < 16)  size = 16;
    if (size > 4096) size = 4096;

    if (m_pixelSize != size) {
        m_pixelSize = size;
        setImplicitWidth(m_pixelSize);
        setImplicitHeight(m_pixelSize);
        regenerate();
        emit pixelSizeChanged();
    }
}

void QrCodeItem::regenerate()
{
    m_modules.clear();

    if (!m_source.isEmpty()) {
        try {
            qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
                m_source.toUtf8().constData(),
                qrcodegen::QrCode::Ecc::LOW);  // LOW = max data capacity
            const int qrSize = qr.getSize();
            m_qrSize = qrSize;
            m_modules.resize(qrSize * qrSize, false);
            for (int y = 0; y < qrSize; ++y) {
                for (int x = 0; x < qrSize; ++x) {
                    m_modules[y * qrSize + x] = qr.getModule(x, y);
                }
            }
        } catch (const std::exception &e) {
            // Input too long or invalid — leave m_modules empty
            m_qrSize = 0;
        }
    }

    // Always report pixelSize as our preferred size, even when empty.
    // This prevents the Layout from collapsing the item to 0×0 during
    // initialization (the old code would set implicit to 0 when source
    // was empty, breaking the first layout pass).
    setImplicitWidth(m_pixelSize);
    setImplicitHeight(m_pixelSize);
    update();
}

void QrCodeItem::paint(QPainter *painter)
{
    if (m_modules.empty() || m_qrSize <= 0) {
        return;
    }

    // --- rendering logic matching the Google/Qt 6.6 recommendation ---
    //
    // Calculate the largest square that fits within the item bounds,
    // apply a 2-module margin, and center the result. Each module is
    // drawn as a filled rectangle at the computed scale so the output
    // is always perfectly square — no aspect-ratio stretching.

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);

    // White background
    painter->fillRect(boundingRect(), Qt::white);

    const int totalModules = m_qrSize + (kMarginModules * 2);

    // minSize: the largest square that fits in our width×height area
    const qreal minSide = qMin(width(), height());
    const qreal scale   = minSide / static_cast<qreal>(totalModules);

    // Center the QR within the available area
    const qreal offsetX = (width()  - minSide) / 2.0 + (kMarginModules * scale);
    const qreal offsetY = (height() - minSide) / 2.0 + (kMarginModules * scale);

    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::black);

    for (int y = 0; y < m_qrSize; ++y) {
        for (int x = 0; x < m_qrSize; ++x) {
            if (m_modules[y * m_qrSize + x]) {
                QRectF rect(offsetX + x * scale,
                            offsetY + y * scale,
                            scale, scale);
                painter->drawRect(rect);
            }
        }
    }
}

} // namespace judecoin_wallet
