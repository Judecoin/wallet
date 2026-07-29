// =============================================================================
// Judecoin Wallet - ClipboardHelper (st-178)
//
// Thin QML-facing wrapper around QGuiApplication::clipboard().
// Qt 6 QML has no built-in clipboard access, expose a single
// Q_INVOKABLE copyToClipboard(text) for QML.
// =============================================================================

#ifndef JUDECOIN_WALLET_CLIPBOARD_HELPER_H
#define JUDECOIN_WALLET_CLIPBOARD_HELPER_H

#include <QObject>
#include <QClipboard>
#include <QGuiApplication>
#include <QString>

namespace judecoin_wallet {

class ClipboardHelper : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardHelper(QObject *parent = nullptr)
        : QObject(parent) {}

    Q_INVOKABLE void copyToClipboard(const QString &text)
    {
        if (text.isEmpty()) return;
        QClipboard *cb = QGuiApplication::clipboard();
        if (cb) {
            cb->setText(text);
        }
    }
};

} // namespace judecoin_wallet

#endif // JUDECOIN_WALLET_CLIPBOARD_HELPER_H
