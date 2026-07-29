// =============================================================================
// Judecoin Wallet - BackupParserFacade (st-022)
//
// QML-friendly wrapper around BackupParser. The parser itself is a
// stateless namespace of static methods, which QML cannot call directly;
// this thin QObject adapter exposes `parse(QString) -> QVariantMap` and
// `summary(QVariantMap) -> QString` so QML can:
//
//   const r = backupParserObj.parse(payload)
//   if (r.ok) ...
//
// V5.4.7: this facade does not retain secrets; it forwards the payload
// to BackupParser and returns the populated ParseResult as a QVariantMap.
// The keys land in the returned map (spendkey/viewkey) and travel
// straight into WalletManager.generateFromKeysBackup. We deliberately
// do not log or persist them anywhere on this side.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_FACADE_H
#define JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_FACADE_H

#include "core/util/BackupParser.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace judecoin_wallet::core::util {

class BackupParserFacade : public QObject
{
    Q_OBJECT
public:
    explicit BackupParserFacade(QObject* parent = nullptr)
        : QObject(parent) {}

    /// QML-callable: read a key-backup file from disk, parse it with
    /// BackupParser, and return a metadata-only map. Sensitive fields
    /// (spendkey / viewkey) are stored in BackupParserTokenStore under a
    /// token id returned in the map; QML never sees them as JS strings.
    ///
    /// Returned map keys:
    ///   ok:            bool
    ///   reason:        string   (empty on success)
    ///   format:        int      (BackupFormat)
    ///   address:       string
    ///   restoreHeight: int      (quint64)
    ///   date:          string
    ///   tokenId:       int      (>0 on success, -1 on failure)
    Q_INVOKABLE QVariantMap parseBackupFile(const QString& path) const;

    /// QML-callable: release a token returned by parseBackupFile(). This
    /// destroys the stored ParseResult and scrubs its secrets. Safe to call
    /// with an invalid / already-released token (returns false).
    Q_INVOKABLE bool releaseToken(int tokenId) const;

    /// QML-callable: parse a payload string and return a QVariantMap.
    /// The returned map shape (QML keys are camelCase to match Qt style):
    ///   ok:            bool
    ///   format:        int   (0=Unknown, 1=Json, 2=TextKeyValue, 3=Mnemonic25)
    ///   reason:        string
    ///   address:       string
    ///   spendkey:      string
    ///   viewkey:       string
    ///   restoreHeight: int (quint64)
    ///   date:          string
    ///   mnemonicWords: stringlist (only populated for format=Mnemonic25)
    Q_INVOKABLE QVariantMap parse(const QString& payload) const;

    /// QML-callable: read a file from disk and return its content as a string.
    /// The path can be a QML file:// URL or a native path. Returns empty
    /// string on failure (file not found, permission denied, etc.).
    Q_INVOKABLE QString readFile(const QString& path) const;

    /// QML-callable: write content to a file on disk.
    /// The path can be a QML file:// URL or a native path.
    /// Returns true on success, false on failure.
    Q_INVOKABLE bool writeFile(const QString& path, const QString& content) const;

    /// QML-callable: render a ParseResult-as-map as a one-line summary.
    /// Mirrors BackupParser::summary.
    Q_INVOKABLE QString summary(const QVariantMap& result) const;
};

} // namespace judecoin_wallet::core::util

#endif // JUDECOIN_WALLET_CORE_UTIL_BACKUP_PARSER_FACADE_H
