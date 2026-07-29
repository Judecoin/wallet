// =============================================================================
// Judecoin Wallet - SecureBuffer (gf-001, gemma4-p0-fixes)
//
// V5.4.5 memory scrubbing helper. Qt's QString and QByteArray do not zero
// their internal storage on destruction: a QString that once held a user
// password leaves the characters in freed heap memory until the allocator
// reuses the page. For wallet passwords / private keys / JSON snapshots
// this is a security regression vs. the V5.4.5 "key clearing principle" requirement.
//
// This header provides:
//
//   1. secureClear(QString&)   - physically zero a QString's UTF-16 buffer
//                                then release it. Use on locals that have
//                                already been forwarded to the RPC layer
//                                and are no longer needed.
//
//   2. secureClear(QByteArray&) - same as above for QByteArray.
//
//   3. SecureBuffer             - a small std::vector<uint8_t>-backed RAII
//                                holder that zeroes itself on destruction.
//                                Use when the lifetime of the sensitive
//                                material needs to be tied to a scope.
//
// Design choices:
//   * Header-only: no .cpp needed; the symbol is local-static-inlinable.
//   * volatile-pointer writes: defeat dead-store elimination (modern
//     compilers will otherwise remove memset(0) on a buffer that is about
//     to go out of scope).
//   * No external deps beyond Qt6::Core (for QString/QByteArray).
//
// Constraints honoured:
//   - Does NOT break any existing QString-based public API. Callers
//     continue to pass QString by const ref; SecureBuffer / secureClear
//     are purely an in-class implementation detail.
//   - Idempotent / no-throw: secureClear() is safe to call on empty
//     containers and swallows nothing - if the buffer was already empty
//     it is a no-op.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_UTIL_SECURE_BUFFER_H
#define JUDECOIN_WALLET_CORE_UTIL_SECURE_BUFFER_H

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace judecoin_wallet::core::util {

// -------------------------------------------------------------------------
// Low-level: zero a raw byte range using a volatile pointer write loop.
//
// We do NOT use std::memset here: the standard library implementation
// is allowed to inline memset, and the compiler is free to recognise
// the "write-then-immediately-discard" pattern and elide the writes.
// The volatile sink forces every store to actually land in memory.
// -------------------------------------------------------------------------
namespace detail {

inline void zeroBytes(volatile void* p, std::size_t n) noexcept
{
    auto* vp = static_cast<volatile unsigned char*>(p);
    while (n--) {
        *vp++ = 0;
    }
}

} // namespace detail

// -------------------------------------------------------------------------
// secureClear(QString&)
//
// Forces the QString to detach (so we own its buffer exclusively), then
// overwrites every QChar with 0 and finally releases the (now-zero) buffer.
//
// QString::clear() alone does NOT zero; it merely releases the reference
// and lets the next allocator write reuse the freed page. This helper
// is the explicit scrubbing step the audit flagged as missing.
// -------------------------------------------------------------------------
inline void secureClear(QString& s) noexcept
{
    if (s.isEmpty()) {
        // Already empty: nothing to scrub, but call clear() anyway so
        // the post-condition (s.isEmpty() == true) holds unconditionally.
        s.clear();
        return;
    }

    // Detach so we don't accidentally zero a buffer that some other
    // QString is still observing. After detach(), s.size() returns the
    // length of our exclusive buffer, which is exactly what we want to
    // scrub.
    s.detach();

    const int n = s.size();
    if (n > 0) {
        // QString::data() returns QChar* (UTF-16 code units). We zero
        // each QChar's 16-bit value to 0.
        auto* p = reinterpret_cast<volatile unsigned short*>(
            const_cast<QChar*>(s.unicode()));
        detail::zeroBytes(p, static_cast<std::size_t>(n) * sizeof(unsigned short));
    }

    // Release the buffer (now full of zeros). The freed memory is no
    // longer observable to anyone holding a copy, and the bytes that
    // were just zeroed will be overwritten by the allocator before
    // being handed out again.
    s.clear();
}

// -------------------------------------------------------------------------
// secureClear(QByteArray&)
//
// Same idea for QByteArray. Use when sensitive data was held as bytes
// (e.g. a UTF-8 encoded password).
// -------------------------------------------------------------------------
inline void secureClear(QByteArray& b) noexcept
{
    if (b.isEmpty()) {
        b.clear();
        return;
    }
    b.detach();
    if (!b.isEmpty()) {
        // QByteArray::data() returns char*; zero via volatile sink so the
        // compiler can't optimise the writes away.
        auto* p = reinterpret_cast<volatile unsigned char*>(
            const_cast<char*>(b.data()));
        detail::zeroBytes(p, static_cast<std::size_t>(b.size()));
    }
    b.clear();
}

// -------------------------------------------------------------------------
// SecureBuffer
//
// RAII byte buffer that zeroes itself on destruction. Use when sensitive
// material needs to live for a longer span than a single expression,
// e.g. when multiple call sites need to read it before the function
// returns.
//
// Copy is intentionally deleted; ownership transfer is allowed via
// std::move. After a move the source is empty (no residue).
//
// Construction from QString / QByteArray copies the bytes verbatim; the
// caller is responsible for clearing the source separately (use
// secureClear() on it).
// -------------------------------------------------------------------------
class SecureBuffer
{
public:
    SecureBuffer() = default;

    explicit SecureBuffer(const QByteArray& src)
    {
        if (!src.isEmpty()) {
            bytes_.assign(src.cbegin(), src.cend());
        }
    }

    explicit SecureBuffer(const QString& src)
        : SecureBuffer(src.toUtf8())
    {
    }

    ~SecureBuffer()
    {
        zero();
    }

    // No copy: copies of secrets are exactly what we want to avoid.
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    // Move allowed; the moved-from instance is left empty (and zeroed).
    SecureBuffer(SecureBuffer&& other) noexcept
        : bytes_(std::move(other.bytes_))
    {
        // other.bytes_ is now empty (moved-from), nothing to zero there.
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept
    {
        if (this != &other) {
            zero();
            bytes_ = std::move(other.bytes_);
        }
        return *this;
    }

    const std::uint8_t* data() const noexcept { return bytes_.data(); }
    std::size_t size() const noexcept { return bytes_.size(); }
    bool empty() const noexcept { return bytes_.empty(); }

    /// Zero the buffer contents and shrink back to zero capacity.
    /// Safe to call multiple times.
    void zero() noexcept
    {
        if (!bytes_.empty()) {
            detail::zeroBytes(bytes_.data(), bytes_.size());
            // Clear + shrink_to_fit: returning memory to the allocator
            // maximises the chance the freed pages are recycled before
            // a memory-dump tool can read them.
            bytes_.clear();
            bytes_.shrink_to_fit();
        }
    }

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace judecoin_wallet::core::util

#endif // JUDECOIN_WALLET_CORE_UTIL_SECURE_BUFFER_H