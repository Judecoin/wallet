// =============================================================================
// Judecoin Wallet - SubaddressManager implementation (st-019)
// =============================================================================

#include "core/subaddress/SubaddressManager.h"

#include "core/localdata/LocalDataManager.h"
#include "core/rpc/HttpRpcClient.h"
#include "core/rpc/WalletRpcClient.h"

namespace judecoin_wallet::core::subaddress {

using judecoin_wallet::core::localdata::LocalDataManager;
using judecoin_wallet::core::rpc::AccountAddress;
using judecoin_wallet::core::rpc::AccountList;
using judecoin_wallet::core::rpc::RpcException;
using judecoin_wallet::core::rpc::WalletRpcClient;

// =============================================================================
// SubaddressEntry
// =============================================================================

QString SubaddressEntry::key() const
{
    return makeKey(account_index, address_index);
}

QString SubaddressEntry::makeKey(quint32 account_index, quint32 address_index)
{
    return QString::asprintf("%u:%u",
                             (unsigned)(account_index),
                             (unsigned)(address_index));
}

// =============================================================================
// SubaddressManager — pure helpers
// =============================================================================

QString SubaddressManager::defaultLabelFor(quint32 address_index)
{
    // V5.4.4 B: the (0, 0) row is the primary address; everything else is
    // "Subaddress N" with N == address_index. We pass through to the same
    // string for both account 0 and any future account (V1.1+) — the
    // "Primary" name only applies to (account 0, address 0); the From
    // dropdown disambiguates by index, not by account, in V1.0.
    if (address_index == 0) {
        return QStringLiteral("Primary");
    }
    return QStringLiteral("Subaddress %1").arg(address_index);
}

QString SubaddressManager::displayStringFor(quint32 address_index,
                                            const QString& label)
{
    // V5.4.4 B: "Subaddress #N - Label" for non-primary indices;
    // "Primary - Label" for index 0 of account 0. If the label is the
    // computed default (i.e. the user never set one), we drop the suffix
    // so the dropdown shows "Subaddress #N" rather than
    // "Subaddress #N - Subaddress N".
    const QString def = defaultLabelFor(address_index);
    if (label == def) {
        if (address_index == 0) {
            return QStringLiteral("Primary");
        }
        return QStringLiteral("Subaddress #%1").arg(address_index);
    }
    if (address_index == 0) {
        return QStringLiteral("Primary - %1").arg(label);
    }
    return QStringLiteral("Subaddress #%1 - %2").arg(address_index).arg(label);
}

// =============================================================================
// SubaddressManager — ctor / dtor
// =============================================================================

SubaddressManager::SubaddressManager(QObject* parent)
    : QObject(parent)
{
}

SubaddressManager::SubaddressManager(LocalDataManager* localData, QObject* parent)
    : QObject(parent)
{
    // Pure-label wiring (V5.4.5 storage is UI-local). Used by unit tests
    // and by callers that only need the label store and not the
    // on-chain refresh path. The wallet_ pointer stays null; refresh()
    // will surface "WalletRpcClient not wired" if anyone tries to call it.
    setLocalDataManager(localData);
}

SubaddressManager::SubaddressManager(WalletRpcClient* wallet,
                                     LocalDataManager* localData,
                                     QObject* parent)
    : QObject(parent)
{
    setWalletRpcClient(wallet);
    setLocalDataManager(localData);
}

SubaddressManager::~SubaddressManager() = default;

// =============================================================================
// SubaddressManager — wiring
// =============================================================================

void SubaddressManager::setWalletRpcClient(WalletRpcClient* w)
{
    wallet_ = w;
}

void SubaddressManager::setLocalDataManager(LocalDataManager* m)
{
    // If we are re-binding a new manager, disconnect from the old one
    // so we don't fire labelChanged() into a deleted object. This matches
    // the pattern used in st-013 / st-018 (defensive signal cleanup).
    if (localData_ != nullptr) {
        const auto* store = &localData_->subaddressLabels();
        disconnect(store, nullptr, this, nullptr);
    }
    localData_ = m;
    if (localData_ != nullptr) {
        const auto* store = &localData_->subaddressLabels();
        // Reload our in-memory entries' labels when the backing store
        // changes; that keeps the UI from showing stale labels after a
        // bulk import (st-017 exportFile / importFile flow).
        connect(store, &judecoin_wallet::core::localdata::SubaddressLabel::labelSet,
                this, &SubaddressManager::emitLabelChangedFromStore);
        connect(store, &judecoin_wallet::core::localdata::SubaddressLabel::labelCleared,
                this, &SubaddressManager::emitLabelChangedFromStore);
        connect(store, &judecoin_wallet::core::localdata::SubaddressLabel::cleared,
                this, [this]() {
            // After a full clear, every entry's label falls back to default.
            // Re-emit labelChanged for each one so QML can re-render.
            for (const auto& e : entries_) {
                const QString newLabel = resolveLabel(e.account_index,
                                                      e.address_index);
                if (newLabel != e.label) {
                    emit labelChanged(e.account_index, e.address_index, newLabel);
                }
            }
        });
    }
}

// =============================================================================
// SubaddressManager — lifecycle
// =============================================================================

void SubaddressManager::refresh()
{
    if (wallet_ == nullptr) {
        lastError_ = QStringLiteral("WalletRpcClient not wired");
        setState(State::Error);
        emit errorOccurred(lastError_);
        return;
    }

    // FIX-G25 (2026-07-18): Unified refresh using get_address only.
    // get_accounts returns only account base_addresses (primary),
    // NOT subaddresses.  get_address(0, N) works for both primary
    // (N=0) and subaddresses (N>0) after G24 fixed the response
    // parser to read the "addresses" array.  We drop getAccounts()
    // entirely and use getAddress() for the primary + label-store
    // enumeration for all known subaddress indices.
    //
    // This also handles wallet recovery: subaddresses are
    // deterministic from seed, so get_address(0, N) always returns
    // the same address regardless of whether create_address was
    // called on this device.

    QSet<QString> seenKeys;
    QVector<SubaddressEntry> newEntries;

    // ---- step 1: getAddress(0, 0) for the primary ---------------
    try {
        AccountAddress primary = wallet_->getAddress(0, 0);
        if (!primary.address.isEmpty()) {
            SubaddressEntry e;
            e.account_index = static_cast<quint32>(primary.account_index);
            e.address_index = static_cast<quint32>(primary.address_index);
            e.address       = primary.address;
            e.label         = resolveLabel(e.account_index, e.address_index);
            e.isPrimary     = true;
            seenKeys.insert(e.key());
            newEntries.push_back(e);
        }
    } catch (const std::exception&) {
        // Can't load primary — continue with label store only.
    }

    // ---- step 2: enumerate label store for subaddress indices -----
    if (localData_ != nullptr) {
        const auto allLabels = localData_->subaddressLabels().labels();
        for (auto it = allLabels.begin(); it != allLabels.end(); ++it) {
            quint32 acct = 0, addr = 0;
            if (!judecoin_wallet::core::localdata::SubaddressLabel::parseKey(
                    it.key(), &acct, &addr)) {
                continue;
            }
            const QString key = SubaddressEntry::makeKey(acct, addr);
            if (seenKeys.contains(key)) continue;  // already have primary

            try {
                AccountAddress a = wallet_->getAddress(acct, addr);
                if (a.address.isEmpty()) continue;

                SubaddressEntry e;
                e.account_index = static_cast<quint32>(a.account_index);
                e.address_index = static_cast<quint32>(a.address_index);
                e.address       = a.address;
                e.label         = resolveLabel(e.account_index, e.address_index);
                e.isPrimary     = (e.account_index == 0 && e.address_index == 0);
                seenKeys.insert(e.key());
                newEntries.push_back(e);
            } catch (const std::exception&) {
                // Stale label — index may not exist. Skip.
            }
        }
    }

    // ---- step 3: scan for used subaddresses (wallet recovery) ---------
    // FIX-G27 (2026-07-18): when the label store is empty (new device,
    // wallet recovery), scan get_address(0, N) upward from N=1 and stop
    // at the first unused index.  Wallet-rpc marks an address as
    // used=true once it receives funds, so this recovers subaddresses
    // that have transaction history without needing the labels file.
    if (newEntries.size() <= 1) {   // only primary (or nothing)
        for (quint64 i = 1; i < 100; ++i) {
            const QString key = SubaddressEntry::makeKey(0, i);
            if (seenKeys.contains(key)) continue;
            try {
                AccountAddress a = wallet_->getAddress(0, i);
                if (a.address.isEmpty()) break;
                if (!a.used) break;   // first unused → stop scanning

                SubaddressEntry e;
                e.account_index = static_cast<quint32>(a.account_index);
                e.address_index = static_cast<quint32>(a.address_index);
                e.address       = a.address;
                e.label         = resolveLabel(e.account_index, e.address_index);
                e.isPrimary     = false;
                newEntries.push_back(e);
            } catch (const std::exception&) {
                break;   // error → stop scanning
            }
        }
    }

    // ---- step 4: merge account-level balances from get_accounts ---
    // st-174 (2026-07-19): getAccounts() returns per-account
    // balance + unlocked_balance. Subaddresses within the same account
    // share the same balance pool (Oxen/Loki model), so we assign the
    // account balance to every SubaddressEntry matching that account.
    if (!newEntries.isEmpty()) {
        try {
            AccountList accts = wallet_->getAccounts();
            for (auto& e : newEntries) {
                for (const auto& a : accts.addresses) {
                    if (static_cast<quint32>(a.account_index) == e.account_index) {
                        e.balance = a.balance;
                        e.unlocked_balance = a.unlocked_balance;
                        break;
                    }
                }
            }
        } catch (const std::exception& ex) {
            qWarning().noquote()
                << "st-174: getAccounts failed for balances:" << ex.what()
                << "— subaddress balance will show as 0";
            // Non-fatal: entries still render with balance=0.
        }
    }

    // ---- commit ---------------------------------------------------
    entries_ = newEntries;
    indexByKey_.clear();
    for (int i = 0; i < entries_.size(); ++i) {
        indexByKey_.insert(entries_[i].key(), i);
    }

    lastError_.clear();
    setState(State::Loaded);
    emit subaddressesUpdated(entries_);
}

void SubaddressManager::clear()
{
    entries_.clear();
    indexByKey_.clear();
    lastError_.clear();
    setState(State::Idle);
    emit subaddressesUpdated(entries_);
}

// =============================================================================
// SubaddressManager — accessors
// =============================================================================

int SubaddressManager::subaddressCount(quint32 account_index) const
{
    int n = 0;
    for (const auto& e : entries_) {
        if (e.account_index == account_index) ++n;
    }
    return n;
}

quint32 SubaddressManager::nextAddressIndex(quint32 account_index) const
{
    quint32 max_seen = 0;
    bool any = false;
    for (const auto& e : entries_) {
        if (e.account_index != account_index) continue;
        any = true;
        if (e.address_index > max_seen) max_seen = e.address_index;
    }
    // The next index is one past the highest we've seen. If we haven't
    // seen any subaddresses for this account, the next index is 0 (which
    // is also the primary address, but the UI is responsible for
    // skipping primary — see V5.4.4 B: dialog only fires for "Add
    // Subaddress" actions, not on first refresh).
    return any ? (max_seen + 1) : 0;
}

SubaddressEntry SubaddressManager::primary() const
{
    for (const auto& e : entries_) {
        if (e.isPrimary) return e;
    }
    return SubaddressEntry{};
}

SubaddressEntry SubaddressManager::entryAt(quint32 account_index,
                                          quint32 address_index) const
{
    const auto it = indexByKey_.find(SubaddressEntry::makeKey(account_index,
                                                              address_index));
    if (it == indexByKey_.end()) {
        return SubaddressEntry{};
    }
    return entries_.at(*it);
}

// =============================================================================
// SubaddressManager — labels
// =============================================================================

void SubaddressManager::setLabel(quint32 account_index,
                                 quint32 address_index,
                                 const QString& text)
{
    // Production wiring always has a LocalDataManager; if not, this is
    // a programming error and we want a loud crash. Tests can exercise
    // the SubaddressLabel store directly without going through this
    // method if they need a no-LocalDataManager path.
    if (localData_ == nullptr) {
        return;
    }
    localData_->subaddressLabels().setLabel(account_index, address_index, text);
    // The signal from SubaddressLabel will reach us via the connection in
    // setLocalDataManager() and call emitLabelChangedFromStore(), which
    // updates the in-memory entry + emits labelChanged. No need to
    // double-emit here.
}

void SubaddressManager::clearLabel(quint32 account_index,
                                   quint32 address_index)
{
    if (localData_ == nullptr) return;
    localData_->subaddressLabels().clearLabel(account_index, address_index);
}

QString SubaddressManager::labelFor(quint32 account_index,
                                    quint32 address_index) const
{
    return resolveLabel(account_index, address_index);
}

QString SubaddressManager::displayString(quint32 account_index,
                                         quint32 address_index) const
{
    return displayStringFor(address_index, resolveLabel(account_index,
                                                        address_index));
}

// FIX-G28 (2026-07-18): QML convenience getter for ReceivePage.qml.
// Returns the base58 address for an (account_index, address_index) pair,
// or empty string if not found. Delegates to entryAt().
QString SubaddressManager::addressFor(quint32 account_index,
                                       quint32 address_index) const
{
    return entryAt(account_index, address_index).address;
}

// =============================================================================
// SubaddressManager — subaddress creation (FIX-G13, 2026-07-18)
// =============================================================================

SubaddressEntry SubaddressManager::createSubaddress(quint32 account_index,
                                                     const QString& label)
{
    if (wallet_ == nullptr) {
        lastError_ = QStringLiteral("WalletRpcClient not wired");
        setState(State::Error);
        emit errorOccurred(lastError_);
        return SubaddressEntry{};
    }

    // FIX-G20 (2026-07-18): Use create_address RPC instead of get_address.
    // get_address only retrieves existing subaddresses and may return the
    // primary when the requested index doesn't exist yet (subaddress not
    // previously created). create_address tells wallet-rpc to derive a NEW
    // subaddress at the next available index and record it in the wallet
    // state, so it shows up in get_accounts and survives restarts.
    AccountAddress addr;
    try {
        addr = wallet_->createAddress(account_index, label);
        qInfo().noquote()
            << "st-FIX-G20: createAddress OK"
            << "acct=" << addr.account_index
            << "addrIdx=" << addr.address_index
            << "address=" << addr.address.left(20) << "...";
    } catch (const RpcException& e) {
        lastError_ = QStringLiteral("get_address failed: %1").arg(e.message());
        setState(State::Error);
        emit errorOccurred(lastError_);
        return SubaddressEntry{};
    } catch (const std::exception& e) {
        lastError_ = QStringLiteral("get_address failed: %1").arg(e.what());
        setState(State::Error);
        emit errorOccurred(lastError_);
        return SubaddressEntry{};
    } catch (...) {
        lastError_ = QStringLiteral("get_address failed: unknown error");
        setState(State::Error);
        emit errorOccurred(lastError_);
        return SubaddressEntry{};
    }

    // Build the new entry.
    SubaddressEntry e;
    e.account_index = static_cast<quint32>(addr.account_index);
    e.address_index = static_cast<quint32>(addr.address_index);
    e.address       = addr.address;
    e.isPrimary     = (e.account_index == 0 && e.address_index == 0);

    // FIX-G16 (2026-07-18): Always persist a label entry — even
    // the default "Subaddress N" — so the SubaddressLabel store
    // tracks this index. On refresh(), we enumerate label store
    // keys to rebuild all subaddresses via getAddress(). Without
    // this, unlabeled subaddresses are invisible after app restart.
    const QString effectiveLabel = label.isEmpty()
        ? defaultLabelFor(e.address_index)
        : label;
    if (localData_ != nullptr) {
        localData_->subaddressLabels().setLabel(e.account_index,
                                                e.address_index,
                                                effectiveLabel);
    }
    e.label = resolveLabel(e.account_index, e.address_index);

    // Append to the in-memory snapshot.
    const int idx = entries_.size();
    entries_.push_back(e);
    indexByKey_.insert(e.key(), idx);

    lastError_.clear();
    setState(State::Loaded);
    emit subaddressesUpdated(entries_);
    return e;
}

// =============================================================================
// SubaddressManager — internals
// =============================================================================

void SubaddressManager::setState(State newState)
{
    if (state_ == newState) return;
    state_ = newState;
    emit stateChanged(newState);
}

void SubaddressManager::applyAccountList(const AccountList& list)
{
    entries_.clear();
    indexByKey_.clear();

    for (const AccountAddress& a : list.addresses) {
        SubaddressEntry e;
        e.account_index = static_cast<quint32>(a.account_index);
        e.address_index = static_cast<quint32>(a.address_index);
        e.address       = a.address;
        e.label         = resolveLabel(e.account_index, e.address_index);
        e.isPrimary     = (e.account_index == 0 && e.address_index == 0);
        const int idx   = entries_.size();
        entries_.push_back(e);
        indexByKey_.insert(e.key(), idx);
    }
}

QString SubaddressManager::resolveLabel(quint32 account_index,
                                       quint32 address_index) const
{
    if (localData_ != nullptr) {
        const QString userLabel =
            localData_->subaddressLabels().labelFor(account_index, address_index);
        if (!userLabel.isEmpty()) {
            return userLabel;
        }
    }
    return defaultLabelFor(address_index);
}

void SubaddressManager::emitLabelChangedFromStore(quint32 account_index,
                                                  quint32 address_index)
{
    // Re-resolve the label for the affected row and update the in-memory
    // snapshot. If the entry isn't in our snapshot (e.g. label set
    // before the first refresh) we still fire labelChanged so QML can
    // pick it up on the next binding.
    const QString newLabel = resolveLabel(account_index, address_index);
    const auto it = indexByKey_.find(
        SubaddressEntry::makeKey(account_index, address_index));
    if (it != indexByKey_.end()) {
        const int idx = *it;
        if (entries_[idx].label != newLabel) {
            entries_[idx].label = newLabel;
        }
    }
    emit labelChanged(account_index, address_index, newLabel);
}

} // namespace judecoin_wallet::core::subaddress
