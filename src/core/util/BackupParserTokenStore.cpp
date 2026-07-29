// =============================================================================
// Judecoin Wallet - BackupParserTokenStore implementation (P0 #12)
// =============================================================================

#include "core/util/BackupParserTokenStore.h"

#include <QMutexLocker>

namespace judecoin_wallet::core::util {

BackupParserTokenStore& BackupParserTokenStore::instance()
{
    static BackupParserTokenStore s;
    return s;
}

int BackupParserTokenStore::store(const ParseResult& result)
{
    QMutexLocker lock(&mutex_);
    const int id = nextId_++;
    tokens_.insert(id, result);
    return id;
}

bool BackupParserTokenStore::consume(int tokenId, ParseResult& out)
{
    QMutexLocker lock(&mutex_);
    auto it = tokens_.find(tokenId);
    if (it == tokens_.end()) {
        return false;
    }
    out = it.value();
    tokens_.erase(it);
    return true;
}

bool BackupParserTokenStore::release(int tokenId)
{
    QMutexLocker lock(&mutex_);
    return tokens_.remove(tokenId) > 0;
}

int BackupParserTokenStore::size() const
{
    QMutexLocker lock(&mutex_);
    return tokens_.size();
}

} // namespace judecoin_wallet::core::util
