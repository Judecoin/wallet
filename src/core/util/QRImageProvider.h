// =============================================================================
// Judecoin Wallet - QRImageProvider (st-024)
//
// QQuickImageProvider that turns `image://qr/<encoded-text>?size=N`
// URLs into QRGenerator-encoded QImages. Used by QrCodeImage.qml.
//
// Why an image provider (vs. returning a QImage from a context
// property): QML's Image element only knows how to load images from
// URLs. The cleanest URL scheme for "render this text as a QR" is
// `image://qr/<source>?size=...`. The provider hands back a fresh
// QImage on every cache miss; we don't try to dedupe across calls
// because QRGenerator::encode is microseconds-fast.
//
// The provider URL is intentionally simple: only `size` is read. Any
// other query keys are ignored (forward compat for future tuning).
// =============================================================================

#ifndef JUDECOIN_WALLET_QR_IMAGE_PROVIDER_H
#define JUDECOIN_WALLET_QR_IMAGE_PROVIDER_H

#include <QQuickImageProvider>
#include <QImage>
#include <QSize>

namespace judecoin_wallet {

class QRImageProvider : public QQuickImageProvider
{
public:
    explicit QRImageProvider();

    /// Decode the URL, call QRGenerator.encode(), hand the QImage
    /// back to QML. `id` is the path part of the URL (the encoded
    /// text, percent-decoded); `requestedSize` is what the QML
    /// Image element wants. We honour requestedSize if non-empty,
    /// otherwise default to QRGenerator::kDefaultSize.
    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;
};

} // namespace judecoin_wallet

#endif // JUDECOIN_WALLET_QR_IMAGE_PROVIDER_H
