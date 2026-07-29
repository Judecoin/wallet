// =============================================================================
// Judecoin Wallet - QRImageProvider implementation (st-024)
// =============================================================================

#include "core/util/QRImageProvider.h"
#include "core/util/QRGenerator.h"

#include <QUrl>
#include <QUrlQuery>

namespace judecoin_wallet {

QRImageProvider::QRImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQuickImageProvider::ForceAsynchronousImageLoading)
{
    // ForceAsynchronousImageLoading: even though encode() is fast,
    // running it on the QML render thread keeps large requests off
    // the GUI thread and matches Qt's recommendation for providers
    // that do any CPU work.
}

QImage QRImageProvider::requestImage(const QString &id, QSize *size,
                                     const QSize &requestedSize)
{
    // id arrives as the URL path part, e.g. "judecoin%3Aaddr%3Famount%3D5"
    // is already percent-decoded by QUrl by the time it reaches us.
    // The original full URL is not passed in - we have to parse the
    // id itself for any embedded query string (QQuickImageProvider
    // strips it for us and we'd have to reconstruct). For V1.0 the
    // QML side passes the entire source as the id, with `?size=N`
    // appended and re-parsed here.
    //
    // Format: <text>           -> default size
    //         <text>?size=N     -> explicit pixel size
    QString text = id;
    int pixelSize = QRGenerator::kDefaultSize;

    const int qpos = text.indexOf(QLatin1Char('?'));
    if (qpos >= 0) {
        const QString query = text.mid(qpos + 1);
        text = text.left(qpos);
        QUrlQuery qs(query);
        const QString sizeStr = qs.queryItemValue(
            QStringLiteral("size"));
        if (!sizeStr.isEmpty()) {
            bool ok = false;
            const int parsed = sizeStr.toInt(&ok);
            if (ok && parsed > 0) {
                pixelSize = parsed;
            }
        }
    }

    // Honour requestedSize if the QML caller passed one. We use the
    // width as the authoritative edge (QML asks for square QR via
    // explicit pixelSize, but a layout pass may ask for "fit 32x32").
    if (requestedSize.isValid() && requestedSize.width() > 0) {
        pixelSize = requestedSize.width();
    }

    QImage img = QRGenerator::encode(text, pixelSize);
    if (size) {
        if (img.isNull()) {
            *size = QSize(pixelSize, pixelSize);
        } else {
            *size = img.size();
        }
    }
    return img;
}

} // namespace judecoin_wallet
