// =============================================================================
// Judecoin Wallet - MnemonicWordList (st-012)
//
// Embeds the 1626-word monero English wordlist (V5.4.4 #8). Source:
//   monero-project/monero src/mnemonics/english.h (BSD-3-Clause)
//   https://github.com/monero-project/monero (commit master, retrieved
//   2026-07-02 by code-modifier for st-012).
//
// The full list is embedded as a static const std::vector<QString> so
// validation is fully offline - no RPC, no external file dependency.
//
// V1.0 supports ONLY English 25-word mnemonics. 13-word is V1.1+
// (per docs/UI_DETAILS_TODO.md Case 3 decisions).
//
// All words are lowercase ASCII, lengths 3..8. No duplicates (verified
// in tests). The vector is sorted alphabetically (same order as upstream
// monero) - isValidEnglishWord does a binary search for O(log n) lookups.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_WALLET_MNEMONIC_WORD_LIST_H
#define JUDECOIN_WALLET_CORE_WALLET_MNEMONIC_WORD_LIST_H

#include <QString>
#include <QStringList>
#include <vector>

namespace judecoin_wallet::core::wallet {

class MnemonicWordList
{
public:
    /// Returns the full 1626-word English monero wordlist (sorted,
    /// lowercase). The vector is built lazily on first call and cached.
    /// This is the canonical list used by V5.4.4 #8.
    static const std::vector<QString>& english();

    /// Total word count (1626 for V1.0 English).
    static int englishSize();

    /// Case-insensitive lookup. Empty / non-ASCII input returns false.
    static bool isValidEnglishWord(const QString& word);

    /// Lowercase + trim + collapse internal whitespace. Used to normalize
    /// pasted user input before parsing into 25 words.
    static QString normalize(const QString& raw);

    /// Parse a free-form string into a list of words. Splits on any
    /// whitespace (spaces / tabs / newlines / commas). Empty input
    /// returns an empty list. Words are normalized to lowercase.
    static QStringList splitWords(const QString& mnemonic);

    /// V1.0 only: the expected word count for a Case 3 recovery mnemonic.
    static constexpr int kEnglishWordCount25 = 25;

private:
    static const std::vector<QString>& buildEnglishList();
};

} // namespace judecoin_wallet::core::wallet

#endif // JUDECOIN_WALLET_CORE_WALLET_MNEMONIC_WORD_LIST_H
