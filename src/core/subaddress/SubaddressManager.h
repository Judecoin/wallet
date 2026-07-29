// =============================================================================
// Judecoin Wallet - SubaddressManager (st-019)
//
// V5.4.4 #5 4 sub-decisions + V5.4.5 storage policy
// ----------------------------------------------------
// A) 1 primary account + N subaddresses   (the data model — see also st-005
//    AccountList: V1.0 only ever has account 0 in active rotation)
// B) Dialog input for label               (the UI owns the dialog; this
//                                          manager exposes the store + display
//                                          helpers; default is "Subaddress N"
//                                          for index > 0 and "Primary" for 0:0)
//    QR deferred to V1.1+                 (V1.0 only has text + Copy)
// B) Inter-subaddress transfer            (the From dropdown in the Send
//                                          page shows entries in
//                                          "Subaddress #N - Label" form)
//
// V5.4.5 storage: labels live in subaddress_labels.json (UI-local),
// NOT on the wallet-rpc. wallet-rpc.get_accounts only returns indices +
// the base58 public address for each subaddress; there is no
// set_subaddress_label / get_subaddress_label RPC in V1.0.
//
// The on-chain source of truth for "what subaddresses exist" is
// wallet-rpc.get_accounts. SubaddressManager reads that on refresh()
// and pairs the addresses with the local label store. The UI binds to
// entries() and to the subaddressesUpdated() signal — like
// TransactionTracker (st-018) the manager is the single reader/owner
// of the in-memory snapshot, so QML never has to reach into the
// RPC client directly.
//
// What this is NOT
// ----------------
//   - Not a wallet creator (that's st-011 / st-012).
//   - Not a key holder (V5.4.7: UI never reads .keys files).
//   - Not a balance tracker (use BalanceApi / get_balance).
//   - Not a transfer submitter (use TransferApi; subaddresses appear
//     in transfer_split as `subaddr_indices` only — this manager
//     surfaces the user's chosen `account_index` + `address_index`
//     pair so the Send page can build a TransferRequest).
//
// Threading
// ---------
//   All signals are emitted on the thread that owns this QObject
//   (UI main thread). refresh() does a synchronous wallet-rpc call,
//   so it MUST be called from the GUI event loop — never from a
//   worker thread. Matches the threading rule from st-013 / st-018.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_SUBADDRESS_SUBADDRESS_MANAGER_H
#define JUDECOIN_WALLET_CORE_SUBADDRESS_SUBADDRESS_MANAGER_H

#include "core/localdata/SubaddressLabel.h"
#include "core/rpc/BalanceApi.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace judecoin_wallet::core::localdata {
class LocalDataManager;
} // namespace judecoin_wallet::core::localdata

namespace judecoin_wallet::core::rpc {
class WalletRpcClient;
} // namespace judecoin_wallet::core::rpc

namespace judecoin_wallet::core::subaddress {

// -----------------------------------------------------------------------------
// SubaddressEntry
//
// One row of the UI subaddress list. Combines:
//   - the on-chain fields from wallet-rpc.get_accounts (address_index, address)
//   - the local label (from SubaddressLabel store, or computed default)
//   - a `isPrimary` convenience flag for the (account_index==0, address_index==0)
//     row that the UI renders prominently.
//
// Equality is intentionally by (account_index, address_index). The base58
// address is treated as opaque metadata — we don't compare on it because
// the wallet may rotate the address derivation if the user restores from
// seed, and the indices stay stable across that.
// -----------------------------------------------------------------------------
struct SubaddressEntry
{
    Q_GADGET
    Q_PROPERTY(quint32 account_index MEMBER account_index)
    Q_PROPERTY(quint32 address_index MEMBER address_index)
    Q_PROPERTY(QString address MEMBER address)
    Q_PROPERTY(QString label MEMBER label)
    Q_PROPERTY(bool isPrimary MEMBER isPrimary)
    Q_PROPERTY(quint64 balance MEMBER balance)                  // st-174: atomic units, from get_accounts
    Q_PROPERTY(quint64 unlocked_balance MEMBER unlocked_balance)  // st-174: spendable atomic units
public:
    quint32 account_index  = 0;
    quint32 address_index  = 0;
    QString address;        // 95-char base58 (Loki/Oxen mainnet, V1.0)
    QString label;          // resolved via resolveLabel(); never empty
    bool    isPrimary   = false;
    quint64 balance        = 0;  // st-174: per-account total (atomic units)
    quint64 unlocked_balance = 0; // st-174: per-account available (atomic units)

    bool isValid() const { return !address.isEmpty(); }

    // Stable "account:addr" key for hash lookups.
    QString key() const;
    static QString makeKey(quint32 account_index, quint32 address_index);

    bool sameAs(const SubaddressEntry& other) const
    {
        return account_index == other.account_index
            && address_index == other.address_index;
    }
};

// -----------------------------------------------------------------------------
// SubaddressManager
// -----------------------------------------------------------------------------
class SubaddressManager : public QObject
{
    Q_OBJECT

public:
    // Top-level state — same shape as TransactionTracker (st-018).
    enum class State {
        Idle,    // no refresh yet (or after clear())
        Loaded,  // last refresh succeeded; entries are valid
        Error,   // last refresh failed; lastError_ is non-empty
    };
    Q_ENUM(State)

    // Q_PROPERTY exposures for QML (FIX-G13 + FIX-G28, 2026-07-18).
    // Without these, QML sees `undefined` when reading
    // subaddressManager.lastError / state / entries.
    // FIX-G28: entries was missing Q_PROPERTY — SendPage.qml "From"
    // ComboBox model resolved to undefined and fell back to [], causing
    // the "From is empty, cannot select" bug reported in manual testing.
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(QVector<SubaddressEntry> entries READ entries NOTIFY subaddressesUpdated)

    // Default label for a given address_index. Index 0 in account 0 is the
    // primary address; per V5.4.4 B the UI shows "Primary" there. For all
    // other indices the default is "Subaddress N" where N == address_index.
    // Pure / static so QML and tests can use it without instantiating.
    static QString defaultLabelFor(quint32 address_index);

    // V5.4.4 B display string for the From dropdown. The rule:
    //   - index 0 in account 0           -> "Primary - <label>" (if label != default)
    //                                       or "Primary"            (if label is default)
    //   - all other indices              -> "Subaddress #N - <label>"
    //                                       or "Subaddress #N"      (if label is default)
    // "Default" means the user has NOT called setLabel() for this entry
    // (we detect that by comparing the resolved label to defaultLabelFor()).
    static QString displayStringFor(quint32 address_index, const QString& label);

    // -- ctor / dtor --------------------------------------------------------
    // parent-only:   defer wiring; use setWalletRpcClient + setLocalDataManager.
    // localdata:     wire only the local label store; useful in tests + for
    //                the pure-label path (V5.4.5 storage is UI-local, so this
    //                is the most common wiring in unit tests).
    // (wallet, localData):  production wiring; both refs set in the ctor.
    explicit SubaddressManager(QObject* parent = nullptr);
    explicit SubaddressManager(judecoin_wallet::core::localdata::LocalDataManager* localData,
                               QObject* parent = nullptr);
    SubaddressManager(judecoin_wallet::core::rpc::WalletRpcClient* wallet,
                      judecoin_wallet::core::localdata::LocalDataManager* localData,
                      QObject* parent = nullptr);
    ~SubaddressManager() override;

    // -- wiring (late binding, matches st-013 / st-017 / st-018) ----------
    void setWalletRpcClient(judecoin_wallet::core::rpc::WalletRpcClient*);
    void setLocalDataManager(judecoin_wallet::core::localdata::LocalDataManager*);

    // -- lifecycle ---------------------------------------------------------
    // refresh() reads wallet-rpc.get_accounts and rebuilds entries(). On
    // success the state goes Idle/Error -> Loaded and subaddressesUpdated
    // fires with the new snapshot. On failure the state goes Error and
    // errorOccurred fires.
    //
    // Synchronous (blocks on the RPC roundtrip) — call from the UI thread.
    Q_INVOKABLE void refresh();

    // clear() resets entries_ to empty and reverts to Idle. Does NOT touch
    // the local label store (those persist across wallet switches; only
    // the on-chain snapshot is wiped).
    void clear();

    // -- accessors ---------------------------------------------------------
    State    state()     const { return state_; }
    QString  lastError() const { return lastError_; }
    int      subaddressCount(quint32 account_index = 0) const;

    // Defensive copy — QML must not mutate our internal vector.
    QVector<SubaddressEntry> entries() const { return entries_; }

    // The next address_index to create on `account_index` (default 0).
    // Equal to the number of subaddresses we currently know about for that
    // account, so the UI can pre-allocate a label slot before the RPC
    // confirm roundtrip. Returns 0 if no entries have been loaded yet.
    quint32 nextAddressIndex(quint32 account_index = 0) const;

    // The (0, 0) entry. Returns a default-constructed SubaddressEntry
    // (with isPrimary=false, address="") if no refresh has been run yet.
    SubaddressEntry primary() const;

    // O(N) lookup by (account_index, address_index). N is small (a few
    // hundred at most for V1.0 users) so a parallel hash index would be
    // overkill. Returns a default-constructed entry on miss.
    SubaddressEntry entryAt(quint32 account_index, quint32 address_index) const;

    // -- labels (V5.4.5 storage policy) -----------------------------------
    // setLabel persists via LocalDataManager.subaddressLabels() — the
    // underlying SubaddressLabel store is what owns the on-disk JSON.
    // We forward the signal so QML can re-render the affected row.
    //
    // Empty text removes the label (so the UI's "clear" action is a
    // single setLabel("", ...)).
    void setLabel(quint32 account_index, quint32 address_index, const QString& text);
    void clearLabel(quint32 account_index, quint32 address_index);

    // labelFor() returns the user-set label if any, otherwise the
    // computed default for that address_index. Never returns "" for a
    // valid (in-range) address_index.
    QString labelFor(quint32 account_index, quint32 address_index) const;

    // FIX-G28 (2026-07-18): Q_INVOKABLE so QML can call
    // subaddressManager.displayString(account_index, address_index)
    // from SendPage.qml's _fromDisplayFor() delegate helper.
    // Convenience: full "Subaddress #N - Label" string for the From
    // dropdown in the Send page.
    Q_INVOKABLE QString displayString(quint32 account_index, quint32 address_index) const;

    // FIX-G28 (2026-07-18): convenience getter so ReceivePage.qml
    // can get the base58 address for a (account_index, address_index)
    // pair without walking entries(). Returns empty string on miss.
    Q_INVOKABLE QString addressFor(quint32 account_index, quint32 address_index) const;

    // -- subaddress creation (FIX-G13, 2026-07-18) --------------------
    // createSubaddress() calls wallet-rpc get_address with the next
    // available address_index (as computed by nextAddressIndex()).
    // In the Oxen/Loki wallet model, get_address with a higher index
    // DERIVES the subaddress from the seed — there is no separate
    // "create_address" RPC. On success the new entry is appended to
    // entries() and subaddressesUpdated fires so QML can re-render.
    // On failure (no wallet wired, RPC error) the method returns an
    // empty SubaddressEntry and errorOccurred fires.
    Q_INVOKABLE judecoin_wallet::core::subaddress::SubaddressEntry createSubaddress(
        quint32 account_index = 0,
        const QString& label = {});

Q_SIGNALS:
    void stateChanged(judecoin_wallet::core::subaddress::SubaddressManager::State state);
    void subaddressesUpdated(const QVector<judecoin_wallet::core::subaddress::SubaddressEntry>& entries);
    void labelChanged(quint32 account_index, quint32 address_index, const QString& label);
    void errorOccurred(const QString& message);

private:
    void setState(State newState);
    void applyAccountList(const judecoin_wallet::core::rpc::AccountList& list);
    QString resolveLabel(quint32 account_index, quint32 address_index) const;
    void emitLabelChangedFromStore(quint32 account_index, quint32 address_index);

    // Dependencies (weak pointers — we never own them).
    judecoin_wallet::core::rpc::WalletRpcClient*     wallet_   = nullptr;
    judecoin_wallet::core::localdata::LocalDataManager* localData_ = nullptr;

    State    state_     = State::Idle;
    QString  lastError_;
    QVector<SubaddressEntry> entries_;
    QHash<QString, int>     indexByKey_;   // "account:addr" -> index in entries_

    Q_DISABLE_COPY(SubaddressManager)
};

} // namespace judecoin_wallet::core::subaddress

Q_DECLARE_METATYPE(judecoin_wallet::core::subaddress::SubaddressEntry)
Q_DECLARE_METATYPE(QVector<judecoin_wallet::core::subaddress::SubaddressEntry>)

#endif // JUDECOIN_WALLET_CORE_SUBADDRESS_SUBADDRESS_MANAGER_H
