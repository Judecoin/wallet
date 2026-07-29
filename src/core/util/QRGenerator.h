// =============================================================================
// Judecoin Wallet - QRGenerator (st-024)
//
// QR-code renderer for V5.4.14 receive flows (subaddress list / ReceivePage
// / make_uri output). Thin wrapper around libqrencode
// (https://github.com/fukuchi/libqrencode) - the same C encoder monero
// GUI uses (per UI_DETAILS_TODO.md:795 V5.4.14 flip that made QR a V1.0
// requirement instead of deferring to V1.1+).
//
// Design notes:
//   - Encoding only. We do not decode QR; the only consumer is QML's
//     Image element bound to the QImage we return.
//   - ECC level M (Medium) by default, matching monero wallet2 / monero
//     GUI conventions. M survives a single 25% area occlusion, plenty
//     for screen rendering.
//   - 8-bit mode hint (QR_MODE_8) so raw addresses (which can include
//     anything up to 95 base58 chars) and the judecoin: URI scheme
//     both round-trip safely without alnum-mode re-encoding surprises.
//   - Output is a square QImage in Format_ARGB32. White background,
//     black modules, with a 4-module quiet-zone border so scanners
//     can find the symbol reliably.
//   - size parameter is the desired OUTPUT pixel edge. The raw
//     qrencode symbol is width-modules wide; we scale + add quiet zone
//     so the resulting image lands at exactly `size x size` pixels.
//
// V5.4.7 compliance: nothing here touches keys. We render text. The
// caller passes either a bare address or a judecoin: URI built by
// UriHandler.makeUri() (st-020).
//
// Thread safety: encode() is reentrant and stateless (no mutable static
// state). Safe to call from multiple QML image providers concurrently.
// =============================================================================

#ifndef JUDECOIN_WALLET_QR_GENERATOR_H
#define JUDECOIN_WALLET_QR_GENERATOR_H

#include <QImage>
#include <QString>

namespace judecoin_wallet {

class QRGenerator
{
public:
    // Default output size in pixels (edge length). Matches the spec
    // (V5.4.14 §3 "256x256 default, 2x/3x on high DPI"). High-DPI
    // callers should compute their own size and pass it in.
    static constexpr int kDefaultSize = 256;

    // Default error-correction level (QR_ECLEVEL_M). Exposed so
    // callers / tests can read the value without poking at qrencode
    // headers directly.
    static constexpr int kDefaultEccLevel = 1; // 0=L,1=M,2=Q,3=H

    // Number of "module" cells added as a quiet-zone border on each
    // side of the symbol. Spec follows ISO/IEC 18004:2015 §6.3.8
    // (4-module quiet zone minimum).
    static constexpr int kQuietZoneModules = 4;

    // Minimum allowed output size. Below this the quiet zone would
    // collapse to <4 px and scanners may misfire.
    static constexpr int kMinSize = 64;

    // Maximum allowed output size. qrencode itself has no upper
    // bound but cap defensively so a fat-fingered caller cannot
    // burn gigabytes on a 10k+ pixel edge.
    static constexpr int kMaxSize = 4096;

    // Encode `text` into a square QImage (Format_ARGB32) at the
    // requested size.
    //
    // Returns an empty QImage() on any failure (empty input, text
    // longer than the QR capacity at version 40 ECC M (~2,953 bytes
    // binary), or qrencode internal error). Callers should treat an
    // isNull() result as "show fallback".
    //
    // size defaults to kDefaultSize. If size < kMinSize it is
    // clamped up; if size > kMaxSize it is clamped down. This means
    // encode() never throws and never returns an oversized image.
    static QImage encode(const QString &text, int size = kDefaultSize);
};

} // namespace judecoin_wallet

#endif // JUDECOIN_WALLET_QR_GENERATOR_H
