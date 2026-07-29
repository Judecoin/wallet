// =============================================================================
// Judecoin Wallet - BackupParserTokenStore (P0 #12)
//
// In-memory, C++-only holding area for parsed backup secrets between the
// moment a backup file is selected and the moment the user submits the
// restore request. It is deliberately NOT exposed to QML as a QObject:
// only integer token ids cross the QML/C++ boundary.
//
// Why this exists:
//   - QML JS strings are immutable and V8 provides no zero-memory API;
//     keeping spendkey/viewkey in a JS property (page.parsed.spendkey)
//     leaves the key bytes in freed heap memory until the GC reclaims them,
//     and even then the underlying allocator page is not zeroed.
//   - BackupParserTokenStore keeps the secrets inside C++ QStrings that are
//     scrubbed via SecureBuffer::secureClear() as soon as the token is
//     consumed or released.
//
// Thread safety: BackupParserTokenStore is intended to be used from the
// main (QML) thread, but all operations are protected by a QMutex.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_TOKEN_STORE_H
#define JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_TOKEN_STORE_H

#include "core/util/BackupParser.h"

#include <QHash>
#include <QMutex>

namespace judecoin_wallet::core::util {

class BackupParserTokenStore
{
public:
    static BackupParserTokenStore& instance();

    /// Store a parsed backup. Returns an opaque token id (>0) that the QML
    /// side can hold instead of the secret keys.
    int store(const ParseResult& result);

    /// Remove the token and copy its ParseResult into `out`. The stored copy
    /// is destroyed (triggering secureClear on spendkey/viewkey). Returns
    /// false if the token does not exist.
    bool consume(int tokenId, ParseResult& out);

    /// Release a token without consuming it. Returns true if the token existed
    /// and was removed (and its secrets scrubbed).
    bool release(int tokenId);

    /// Diagnostic helper for leak-checking; not exposed to QML.
    int size() const;

private:
    BackupParserTokenStore() = default;
    Q_DISABLE_COPY(BackupParserTokenStore)

    mutable QMutex mutex_;
    QHash<int, ParseResult> tokens_;
    int nextId_ = 1;
};

} // namespace judecoin_wallet::core::util

#endif // JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_TOKEN_STORE_H
