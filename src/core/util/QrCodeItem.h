// =============================================================================
// Judecoin Wallet - QrCodeItem (FIX-G19 v3, 2026-07-20)
//
// QQuickPaintedItem that renders a QR code directly in the QML scene
// graph via QPainter. Uses Nayuki's qrcodegen library for matrix
// generation (recommended by the Qt 6.6 QR rendering guide).
//
// The paint() method follows the Google-recommended rendering pattern:
//   - Calculate square region: side = min(width, height)
//   - Center with module-margin offsets
//   - Draw each module as a filled rectangle
// This guarantees a square QR output regardless of the Layout-allocated
// item dimensions.
//
// Usage from QML:
//   import judecoin.wallet.core 1.0
//   QrCodeItem {
//       source: "J93y6..."
//       pixelSize: 256
//   }
// =============================================================================

#ifndef JUDECOIN_WALLET_QRCODE_ITEM_H
#define JUDECOIN_WALLET_QRCODE_ITEM_H

#include <QQuickPaintedItem>
#include <QVector>

namespace judecoin_wallet {

class QrCodeItem : public QQuickPaintedItem
{
    Q_OBJECT

    /// Text to encode as a QR code (address or judecoin: URI).
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)

    /// Preferred pixel edge length (square, default 256).
    Q_PROPERTY(int pixelSize READ pixelSize WRITE setPixelSize NOTIFY pixelSizeChanged)

public:
    explicit QrCodeItem(QQuickItem *parent = nullptr);

    QString source() const;
    void setSource(const QString &src);

    int pixelSize() const;
    void setPixelSize(int size);

    void paint(QPainter *painter) override;

signals:
    void sourceChanged();
    void pixelSizeChanged();

private:
    void regenerate();

    QString       m_source;
    int           m_pixelSize;
    int           m_qrSize = 0;        // module grid size (getSize())
    QVector<bool> m_modules;           // row-major: modules[y*qrSize+x]
};

} // namespace judecoin_wallet

#endif // JUDECOIN_WALLET_QRCODE_ITEM_H
