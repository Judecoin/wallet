// =============================================================================
// Judecoin Wallet - ApiParsers.cpp (st-005)
//
// Parse implementations for every struct exposed by WalletRpcClient. Each
// struct has a `static bool parse(const QJsonObject&, <Struct>&)` method and
// a `QJsonObject toJson()` method (used by request structs to serialise
// back to a JSON-RPC params envelope).
//
// Conventions:
//   - parse() returns false ONLY for hard schema errors (missing required
//     field, wrong type). Tolerated absences (empty array, default zero
//     value) return true and leave the out parameter at its default state.
//   - toJson() always emits every field, even when empty / zero, so the
//     JSON envelope round-trips faithfully.
//   - All integer amounts are uint64. wallet-rpc emits them as JSON numbers
//     which QJsonValue parses to double; we round-trip through double.
// =============================================================================

#include "core/rpc/AddressBookApi.h"
#include "core/rpc/BalanceApi.h"
#include "core/rpc/BalanceProofApi.h"
#include "core/rpc/FreezeApi.h"
#include "core/rpc/HistoryApi.h"
#include "core/rpc/MultisigApi.h"
#include "core/rpc/OtherApi.h"
#include "core/rpc/TransferApi.h"
#include "core/rpc/TxProofApi.h"
#include "core/rpc/UriApi.h"

#include "core/util/SecureBuffer.h"   // P0 #3 (2026-07-23): secureClear() scrub helper

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <map>

namespace judecoin_wallet::core::rpc {

namespace {

// Read a uint64 from a JSON value. Tolerates double, integer, and string-
// of-decimal (the wallet-rpc sometimes emits large values as strings).
quint64 jsonToUint64(const QJsonValue& v, quint64 fallback = 0)
{
    if (v.isDouble())  return static_cast<quint64>(v.toDouble());
    if (v.isString()) {
        bool ok = false;
        quint64 u = v.toString().toULongLong(&ok);
        return ok ? u : fallback;
    }
    return fallback;
}

// Read an int64 (signed).
qint64 jsonToInt64(const QJsonValue& v, qint64 fallback = 0)
{
    if (v.isDouble())  return static_cast<qint64>(v.toDouble());
    if (v.isString()) {
        bool ok = false;
        qint64 i = v.toString().toLongLong(&ok);
        return ok ? i : fallback;
    }
    return fallback;
}

} // namespace

// =============================================================================
// P0 #3 (2026-07-23, audit 2026-07-21-gemma4-12b line 1476): sensitive-field
// scrubbing helper.
//
// The request/response structs below carry wallet passwords, seeds, private
// view/spend keys and full JSON snapshots as plain QString. QString does not
// zero its backing store on destruction, so once a struct goes out of scope
// the sensitive bytes linger in freed heap memory (recoverable via core dump
// or heap scan).
//
// scrubField() erases one such field IN PLACE at the point where toJson()
// has already deep-copied it into the outgoing QJsonObject. Verified against
// Qt 6.6: QJsonObject::insert(QString) copies the UTF-8 bytes into an
// independent QCborContainerPrivate QByteArray (qcborvalue_p.h addByteData
// memcpy), so clearing the source QString afterwards does NOT corrupt the
// outgoing JSON envelope.
//
// Why in-place (not a local copy):
//   * A local copy only scrubs the copy's detached buffer; the ORIGINAL
//     field (the one the audit flags) still holds plaintext until the caller
//     scrubs it. Caller-side scrubbing exists in WalletManager but NOT in
//     RecoveryManager / direct WalletRpcClient users, so the original would
//     leak for those paths. Erasing the original here closes the hole for
//     ALL callers.
//   * toJson() is the last read of each sensitive field on every call path
//     (WalletRpcClient.cpp: request.toJson() is the final use; the tests
//     call it exactly once and never re-read the field). Erasing after the
//     insert is therefore behaviour-preserving.
//
// The const_cast is a deliberate, documented trade-off: toJson() stays
// logically const for the JSON it produces, and the field is only mutated
// AFTER its value has been captured. This mirrors the existing contract of
// core::util::secureClear(QString&).
// =============================================================================
namespace {

inline void scrubField(const QString& sensitive) noexcept
{
    judecoin_wallet::core::util::secureClear(const_cast<QString&>(sensitive));
}

} // namespace

// =============================================================================
// BalanceApi
// =============================================================================

bool BalanceInfo::parse(const QJsonObject& obj, BalanceInfo& out)
{
    if (!obj.contains(QStringLiteral("balance")) &&
        !obj.contains(QStringLiteral("total")))
    {
        // Hard error: must contain at least one of these.
        return false;
    }
    out.total   = jsonToUint64(
        obj.value(QStringLiteral("balance")),
        jsonToUint64(obj.value(QStringLiteral("total")), 0));
    out.unlocked = jsonToUint64(obj.value(QStringLiteral("unlocked_balance")), 0);
    return true;
}

QJsonObject BalanceInfo::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("balance"), QJsonValue(static_cast<double>(total)));
    o.insert(QStringLiteral("unlocked_balance"),
              QJsonValue(static_cast<double>(unlocked)));
    return o;
}

bool AccountAddress::parse(const QJsonObject& obj, AccountAddress& out)
{
    // FIX-G21 (2026-07-18): get_address / create_address return
    // "address", but get_accounts returns "base_address".
    // Try "address" first, fall back to "base_address".
    QString addr = obj.value(QStringLiteral("address")).toString();
    if (addr.isEmpty()) {
        addr = obj.value(QStringLiteral("base_address")).toString();
    }
    if (addr.isEmpty()) {
        return false;
    }
    out.account_index = jsonToUint64(
        obj.value(QStringLiteral("account_index")));
    out.address_index = jsonToUint64(
        obj.value(QStringLiteral("address_index")));
    out.address = addr;
    out.used    = obj.value(QStringLiteral("used")).toBool(false);
    // st-131: get_accounts returns per-account balance / unlocked_balance.
    // get_address does NOT include these fields, so we parse optionally
    // (default stays 0 when absent).
    out.balance          = jsonToUint64(obj.value(QStringLiteral("balance")));
    out.unlocked_balance = jsonToUint64(obj.value(QStringLiteral("unlocked_balance")));
    return true;
}

QJsonObject AccountAddress::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("account_index"),
              QJsonValue(static_cast<double>(account_index)));
    o.insert(QStringLiteral("address_index"),
              QJsonValue(static_cast<double>(address_index)));
    o.insert(QStringLiteral("address"), address);
    o.insert(QStringLiteral("balance"),
              QJsonValue(static_cast<double>(balance)));
    o.insert(QStringLiteral("unlocked_balance"),
              QJsonValue(static_cast<double>(unlocked_balance)));
    return o;
}

bool AccountList::parse(const QJsonObject& obj, AccountList& out)
{
    out.addresses.clear();
    out.primary.clear();

    // FIX-G21 (2026-07-18): the wallet-rpc may use "subaddress_accounts"
    // (Oxen/Loki standard) or "subaddr_accounts" (our local spelling).
    // Try the canonical spelling first, then the abbreviated one.
    QJsonArray arr = obj.value(QStringLiteral("subaddress_accounts")).toArray();
    if (arr.isEmpty()) {
        arr = obj.value(QStringLiteral("subaddr_accounts")).toArray();
    }
    for (const QJsonValue& v : arr) {
        AccountAddress a;
        if (AccountAddress::parse(v.toObject(), a)) {
            out.addresses.push_back(a);
        }
    }
    // get_accounts also returns { primary_address: "..." } in some forks.
    out.primary = obj.value(QStringLiteral("primary_address")).toString();
    if (out.primary.isEmpty() && !out.addresses.isEmpty()) {
        out.primary = out.addresses.first().address;
    }
    return true;
}

QJsonObject AccountList::toJson() const
{
    QJsonObject o;
    QJsonArray arr;
    for (const auto& a : addresses) {
        arr.append(a.toJson());
    }
    o.insert(QStringLiteral("subaddr_accounts"), arr);
    o.insert(QStringLiteral("primary_address"), primary);
    return o;
}

// ---------------------------------------------------------------------------
// WalletVersion::parse - the v3.2.0 wallet-rpc emits `release` as a BOOL
// (`true` for tagged releases, `false` for dev builds), but older forks
// and our own test fixtures still emit it as a STRING. Accept both:
//   - bool true   -> is_release=true,  release="true"
//   - bool false  -> is_release=false, release="false"
//   - string      -> is_release inferred from contents,
//                    release=verbatim
// `version_string` is derived from the numeric `version` via the same
// semver packing used elsewhere in the wallet (high byte=major,
// mid byte=minor, low byte=patch), with a small mapping table for the
// historical Oxen/Loki RPC numbers that don't fit cleanly.
// ---------------------------------------------------------------------------
bool WalletVersion::parse(const QJsonObject& obj, WalletVersion& out)
{
    if (!obj.contains(QStringLiteral("version"))) {
        return false;
    }
    out.version = jsonToUint64(obj.value(QStringLiteral("version")));

    const QJsonValue releaseVal = obj.value(QStringLiteral("release"));
    if (releaseVal.isBool()) {
        out.is_release = releaseVal.toBool();
        out.release    = out.is_release
                            ? QStringLiteral("true")
                            : QStringLiteral("false");
    } else if (releaseVal.isString()) {
        const QString r = releaseVal.toString();
        out.release = r;
        // Inferred is_release: anything starting with the digit "v" or
        // containing "-release" / "-rc" is treated as a release/RC build.
        out.is_release = r.contains(QStringLiteral("release"))
                       || r.startsWith(QStringLiteral("v"))
                       || r.startsWith(QStringLiteral("V"));
    } else {
        // null / missing - leave at defaults.
        out.is_release = false;
        out.release.clear();
    }

    // -------------------------------------------------------------------------
    // Derive a human-readable semver string for known RPC version constants.
    // The Loki/Oxen RPC `version` integer is NOT a packed-semver value - it
    // is the RPC_VERSION_HF compile-time constant from the daemon/wallet-rpc
    // build. For v3.2.0 mainnet this is 65558. We maintain a small lookup
    // table for the builds we support; unknown versions fall back to the
    // `release` string verbatim (which for older forks is a human-readable
    // version string like "v3.1.0-release").
    // -------------------------------------------------------------------------
    static const std::map<quint64, QString> kRpcVersionToSemver = {
        // Judecoin v3.2.0 mainnet (2026-07 build)
        { 65558ULL, QStringLiteral("v3.2.0") },
        // Reserved hooks for adjacent Judecoin/Oxen/Loki builds. Add to
        // this map as new versions are verified against real binaries.
        { 65557ULL, QStringLiteral("v3.1.0") },
    };
    {
        const auto it = kRpcVersionToSemver.find(out.version);
        if (it != kRpcVersionToSemver.end()) {
            out.version_string = it->second;
        } else if (!out.release.isEmpty()
                   && (out.release.startsWith(QStringLiteral("v"))
                       || out.release.contains(QStringLiteral("-release")))) {
            // Fall back to the `release` string for older string-based
            // responses (e.g. legacy test mocks).
            out.version_string = out.release;
        }
        // else: leave version_string empty.
    }
    return true;
}

QJsonObject WalletVersion::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"),
              QJsonValue(static_cast<double>(version)));
    // Preserve the bool-or-string contract on the wire: prefer the bool
    // form (real v3.2.0 binary), fall back to string for older builds.
    if (is_release && release == QStringLiteral("true")) {
        o.insert(QStringLiteral("release"), QJsonValue(true));
    } else if (!is_release && release == QStringLiteral("false")) {
        o.insert(QStringLiteral("release"), QJsonValue(false));
    } else {
        o.insert(QStringLiteral("release"), release);
    }
    if (!version_string.isEmpty()) {
        o.insert(QStringLiteral("version_string"), version_string);
    }
    return o;
}

bool BlockHeight::parse(const QJsonObject& obj, BlockHeight& out)
{
    if (!obj.contains(QStringLiteral("height"))) {
        return false;
    }
    out.height = jsonToUint64(obj.value(QStringLiteral("height")));
    return true;
}

QJsonObject BlockHeight::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("height"),
              QJsonValue(static_cast<double>(height)));
    return o;
}

// =============================================================================
// TransferApi
// =============================================================================

QJsonObject TransferDestination::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("amount"), QJsonValue(static_cast<double>(amount)));
    o.insert(QStringLiteral("address"), address);
    return o;
}

bool TransferDestination::parse(const QJsonObject& obj, TransferDestination& out)
{
    if (!obj.contains(QStringLiteral("address"))) {
        return false;
    }
    out.amount  = jsonToUint64(obj.value(QStringLiteral("amount")));
    out.address = obj.value(QStringLiteral("address")).toString();
    return true;
}

QJsonObject TransferRequest::toJson() const
{
    QJsonObject o;
    QJsonArray destArr;
    for (const auto& d : destinations) {
        destArr.append(d.toJson());
    }
    o.insert(QStringLiteral("destinations"), destArr);
    o.insert(QStringLiteral("account_index"),
              QJsonValue(static_cast<double>(account_index)));
    if (!subaddr_indices.isEmpty()) {
        QJsonArray sa;
        for (quint64 i : subaddr_indices) {
            sa.append(QJsonValue(static_cast<double>(i)));
        }
        o.insert(QStringLiteral("subaddr_indices"), sa);
    }
    o.insert(QStringLiteral("priority"), priority);
    if (ring_size > 0) {
        o.insert(QStringLiteral("ring_size"),
                  QJsonValue(static_cast<double>(ring_size)));
    }
    o.insert(QStringLiteral("get_tx_key"), get_tx_key);
    o.insert(QStringLiteral("get_tx_hex"), get_tx_hex);
    o.insert(QStringLiteral("get_tx_metadata"), get_tx_metadata);
    return o;
}

bool TransferRequest::parse(const QJsonObject& obj, TransferRequest& out)
{
    out.destinations.clear();
    out.subaddr_indices.clear();
    const QJsonArray arr = obj.value(QStringLiteral("destinations")).toArray();
    for (const QJsonValue& v : arr) {
        TransferDestination d;
        if (TransferDestination::parse(v.toObject(), d)) {
            out.destinations.push_back(d);
        }
    }
    out.account_index = jsonToUint64(
        obj.value(QStringLiteral("account_index")));
    const QJsonArray sa = obj.value(QStringLiteral("subaddr_indices")).toArray();
    for (const QJsonValue& v : sa) {
        out.subaddr_indices.push_back(jsonToUint64(v));
    }
    out.priority        = obj.value(QStringLiteral("priority")).toInt(0);
    out.ring_size       = jsonToUint64(obj.value(QStringLiteral("ring_size")), 16);
    out.get_tx_key      = obj.value(QStringLiteral("get_tx_key")).toBool(false);
    out.get_tx_hex      = obj.value(QStringLiteral("get_tx_hex")).toBool(false);
    out.get_tx_metadata = obj.value(QStringLiteral("get_tx_metadata")).toBool(false);
    return true;
}

bool TransferResult::parse(const QJsonObject& obj, TransferResult& out)
{
    out.amount = jsonToUint64(obj.value(QStringLiteral("amount")));
    out.fee    = jsonToUint64(obj.value(QStringLiteral("fee")));
    out.tx_hash    = obj.value(QStringLiteral("tx_hash")).toString();
    out.tx_key     = obj.value(QStringLiteral("tx_key")).toString();
    out.tx_blob    = obj.value(QStringLiteral("tx_blob")).toString();
    out.tx_metadata = obj.value(QStringLiteral("tx_metadata")).toString();
    out.multisig_txset = obj.value(QStringLiteral("multisig_txset")).toString();
    out.amount_list.clear();
    const QJsonArray arr = obj.value(QStringLiteral("amount_list")).toArray();
    for (const QJsonValue& v : arr) {
        out.amount_list.push_back(jsonToUint64(v));
    }
    return true;
}

QJsonObject TransferResult::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("amount"),
              QJsonValue(static_cast<double>(amount)));
    o.insert(QStringLiteral("fee"),
              QJsonValue(static_cast<double>(fee)));
    o.insert(QStringLiteral("tx_hash"), tx_hash);
    o.insert(QStringLiteral("tx_key"), tx_key);
    o.insert(QStringLiteral("tx_blob"), tx_blob);
    o.insert(QStringLiteral("tx_metadata"), tx_metadata);
    QJsonArray arr;
    for (quint64 a : amount_list) {
        arr.append(QJsonValue(static_cast<double>(a)));
    }
    o.insert(QStringLiteral("amount_list"), arr);
    o.insert(QStringLiteral("multisig_txset"), multisig_txset);
    return o;
}

// =============================================================================
// HistoryApi
// =============================================================================

bool TransferEntry::parse(const QJsonObject& obj, TransferEntry& out)
{
    out.tx_hash      = obj.value(QStringLiteral("tx_hash")).toString();
    out.amount       = jsonToInt64(obj.value(QStringLiteral("amount")));
    out.block_height = jsonToUint64(obj.value(QStringLiteral("block_height")));
    out.confirmations = jsonToUint64(obj.value(QStringLiteral("confirmations")));
    out.unlocked     = obj.value(QStringLiteral("unlocked")).toBool(false);
    out.fee          = jsonToUint64(obj.value(QStringLiteral("fee")));
    out.timestamp    = jsonToUint64(obj.value(QStringLiteral("timestamp")));
    out.payment_id   = obj.value(QStringLiteral("payment_id")).toString();
    out.type         = obj.value(QStringLiteral("type")).toString();
    out.destinations.clear();
    // Some wallet-rpc versions wrap destinations under a top-level key;
    // tolerate either.
    QJsonValue dests = obj.value(QStringLiteral("destinations"));
    if (dests.isUndefined()) {
        dests = obj.value(QStringLiteral("address"));
    }
    if (dests.isArray()) {
        for (const QJsonValue& v : dests.toArray()) {
            out.destinations.push_back(v.toString());
        }
    } else if (dests.isString()) {
        out.destinations.push_back(dests.toString());
    }
    return true;
}

QJsonObject TransferEntry::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("tx_hash"), tx_hash);
    o.insert(QStringLiteral("amount"), QJsonValue(static_cast<double>(amount)));
    o.insert(QStringLiteral("block_height"),
              QJsonValue(static_cast<double>(block_height)));
    o.insert(QStringLiteral("confirmations"),
              QJsonValue(static_cast<double>(confirmations)));
    o.insert(QStringLiteral("unlocked"), unlocked);
    o.insert(QStringLiteral("fee"), QJsonValue(static_cast<double>(fee)));
    o.insert(QStringLiteral("timestamp"),
              QJsonValue(static_cast<double>(timestamp)));
    o.insert(QStringLiteral("payment_id"), payment_id);
    o.insert(QStringLiteral("type"), type);
    QJsonArray arr;
    for (const auto& d : destinations) arr.append(d);
    o.insert(QStringLiteral("destinations"), arr);
    return o;
}

bool TransferList::parse(const QJsonObject& obj, TransferList& out)
{
    out.entries.clear();
    // Wallet-rpc may emit either:
    //   { in: [...], out: [...], pending: [...], failed: [...], pool: [...] }
    // or a flat { transfers: [...] } for older forks.
    const QStringList subkeys = {
        QStringLiteral("in"),
        QStringLiteral("out"),
        QStringLiteral("pending"),
        QStringLiteral("failed"),
        QStringLiteral("pool"),
    };
    for (const QString& key : subkeys) {
        const QJsonArray arr = obj.value(key).toArray();
        for (const QJsonValue& v : arr) {
            TransferEntry e;
            if (TransferEntry::parse(v.toObject(), e)) {
                out.entries.push_back(e);
            }
        }
    }
    if (out.entries.isEmpty()) {
        // Fall back to flat list.
        const QJsonArray arr = obj.value(QStringLiteral("transfers")).toArray();
        for (const QJsonValue& v : arr) {
            TransferEntry e;
            if (TransferEntry::parse(v.toObject(), e)) {
                out.entries.push_back(e);
            }
        }
    }
    return true;
}

QJsonObject TransferList::toJson() const
{
    QJsonObject o;
    QJsonArray arr;
    for (const auto& e : entries) arr.append(e.toJson());
    o.insert(QStringLiteral("transfers"), arr);
    return o;
}

bool PaymentEntry::parse(const QJsonObject& obj, PaymentEntry& out)
{
    out.payment_id    = obj.value(QStringLiteral("payment_id")).toString();
    out.tx_hash       = obj.value(QStringLiteral("tx_hash")).toString();
    out.amount        = jsonToUint64(obj.value(QStringLiteral("amount")));
    out.block_height  = jsonToUint64(obj.value(QStringLiteral("block_height")));
    out.unlock_time   = jsonToUint64(obj.value(QStringLiteral("unlock_time")));
    out.locked        = obj.value(QStringLiteral("locked")).toBool(false);
    return true;
}

QJsonObject PaymentEntry::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("payment_id"), payment_id);
    o.insert(QStringLiteral("tx_hash"), tx_hash);
    o.insert(QStringLiteral("amount"), QJsonValue(static_cast<double>(amount)));
    o.insert(QStringLiteral("block_height"),
              QJsonValue(static_cast<double>(block_height)));
    o.insert(QStringLiteral("unlock_time"),
              QJsonValue(static_cast<double>(unlock_time)));
    o.insert(QStringLiteral("locked"), locked);
    return o;
}

bool PaymentList::parse(const QJsonObject& obj, PaymentList& out)
{
    out.payments.clear();
    const QJsonArray arr = obj.value(QStringLiteral("payments")).toArray();
    for (const QJsonValue& v : arr) {
        PaymentEntry p;
        if (PaymentEntry::parse(v.toObject(), p)) {
            out.payments.push_back(p);
        }
    }
    return true;
}

QJsonObject PaymentList::toJson() const
{
    QJsonObject o;
    QJsonArray arr;
    for (const auto& p : payments) arr.append(p.toJson());
    o.insert(QStringLiteral("payments"), arr);
    return o;
}

// =============================================================================
// AddressBookApi
// =============================================================================

bool AddressBookEntry::parse(const QJsonObject& obj, AddressBookEntry& out)
{
    out.index       = jsonToUint64(obj.value(QStringLiteral("index")));
    out.address     = obj.value(QStringLiteral("address")).toString();
    out.payment_id  = obj.value(QStringLiteral("payment_id")).toString();
    out.description = obj.value(QStringLiteral("description")).toString();
    return true;
}

QJsonObject AddressBookEntry::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("index"), QJsonValue(static_cast<double>(index)));
    o.insert(QStringLiteral("address"), address);
    o.insert(QStringLiteral("payment_id"), payment_id);
    o.insert(QStringLiteral("description"), description);
    return o;
}

bool AddressBookEntryList::parse(const QJsonObject& obj, AddressBookEntryList& out)
{
    out.entries.clear();
    const QJsonArray arr = obj.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& v : arr) {
        AddressBookEntry e;
        if (AddressBookEntry::parse(v.toObject(), e)) {
            out.entries.push_back(e);
        }
    }
    return true;
}

QJsonObject AddressBookEntryList::toJson() const
{
    QJsonObject o;
    QJsonArray arr;
    for (const auto& e : entries) arr.append(e.toJson());
    o.insert(QStringLiteral("entries"), arr);
    return o;
}

// =============================================================================
// UriApi
// =============================================================================

QJsonObject MakeUriRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("address"), address);
    if (amount > 0) {
        o.insert(QStringLiteral("amount"),
                  QJsonValue(static_cast<double>(amount)));
    }
    if (!payment_id.isEmpty()) {
        o.insert(QStringLiteral("payment_id"), payment_id);
    }
    if (!recipient_name.isEmpty()) {
        o.insert(QStringLiteral("recipient_name"), recipient_name);
    }
    if (!tx_description.isEmpty()) {
        o.insert(QStringLiteral("tx_description"), tx_description);
    }
    return o;
}

bool MakeUriRequest::parse(const QJsonObject& obj, MakeUriRequest& out)
{
    out.address         = obj.value(QStringLiteral("address")).toString();
    out.amount          = jsonToUint64(obj.value(QStringLiteral("amount")));
    out.payment_id      = obj.value(QStringLiteral("payment_id")).toString();
    out.recipient_name  = obj.value(QStringLiteral("recipient_name")).toString();
    out.tx_description  = obj.value(QStringLiteral("tx_description")).toString();
    return true;
}

bool ParsedUri::parse(const QJsonObject& obj, ParsedUri& out)
{
    out.uri            = obj.value(QStringLiteral("uri")).toString();
    out.address        = obj.value(QStringLiteral("address")).toString();
    out.amount         = jsonToUint64(obj.value(QStringLiteral("amount")));
    out.payment_id     = obj.value(QStringLiteral("payment_id")).toString();
    out.recipient_name = obj.value(QStringLiteral("recipient_name")).toString();
    out.tx_description = obj.value(QStringLiteral("tx_description")).toString();
    return true;
}

QJsonObject ParsedUri::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("uri"), uri);
    o.insert(QStringLiteral("address"), address);
    o.insert(QStringLiteral("amount"), QJsonValue(static_cast<double>(amount)));
    o.insert(QStringLiteral("payment_id"), payment_id);
    o.insert(QStringLiteral("recipient_name"), recipient_name);
    o.insert(QStringLiteral("tx_description"), tx_description);
    return o;
}

// =============================================================================
// TxProofApi
// =============================================================================

bool CheckTxKeyResult::parse(const QJsonObject& obj, CheckTxKeyResult& out)
{
    out.received      = jsonToUint64(obj.value(QStringLiteral("received")));
    out.in_pool       = obj.value(QStringLiteral("in_pool")).toBool(false);
    out.confirmations = jsonToUint64(obj.value(QStringLiteral("confirmations")));
    return true;
}

QJsonObject CheckTxKeyResult::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("received"),
              QJsonValue(static_cast<double>(received)));
    o.insert(QStringLiteral("in_pool"), in_pool);
    o.insert(QStringLiteral("confirmations"),
              QJsonValue(static_cast<double>(confirmations)));
    return o;
}

bool CheckTxProofResult::parse(const QJsonObject& obj, CheckTxProofResult& out)
{
    out.confirmations = jsonToUint64(obj.value(QStringLiteral("confirmations")));
    out.in_pool       = obj.value(QStringLiteral("in_pool")).toBool(false);
    out.good          = obj.value(QStringLiteral("good")).toBool(false);
    return true;
}

QJsonObject CheckTxProofResult::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("confirmations"),
              QJsonValue(static_cast<double>(confirmations)));
    o.insert(QStringLiteral("in_pool"), in_pool);
    o.insert(QStringLiteral("good"), good);
    return o;
}

// =============================================================================
// BalanceProofApi
// =============================================================================

bool CheckReserveProofResult::parse(const QJsonObject& obj, CheckReserveProofResult& out)
{
    out.total = jsonToUint64(obj.value(QStringLiteral("total")));
    out.spent = jsonToUint64(obj.value(QStringLiteral("spent")));
    out.good  = obj.value(QStringLiteral("good")).toBool(false);
    return true;
}

QJsonObject CheckReserveProofResult::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("total"), QJsonValue(static_cast<double>(total)));
    o.insert(QStringLiteral("spent"), QJsonValue(static_cast<double>(spent)));
    o.insert(QStringLiteral("good"), good);
    return o;
}

// =============================================================================
// MultisigApi
// =============================================================================

bool MultisigInfo::parse(const QJsonObject& obj, MultisigInfo& out)
{
    out.address       = obj.value(QStringLiteral("address")).toString();
    out.multisig_info = obj.value(QStringLiteral("multisig_info")).toString();
    return true;
}

QJsonObject MultisigInfo::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("address"), address);
    o.insert(QStringLiteral("multisig_info"), multisig_info);
    return o;
}

bool MultisigStatus::parse(const QJsonObject& obj, MultisigStatus& out)
{
    out.multisig  = obj.value(QStringLiteral("multisig")).toBool(false);
    out.ready     = obj.value(QStringLiteral("ready")).toBool(false);
    out.threshold = jsonToUint64(obj.value(QStringLiteral("threshold")));
    out.total     = jsonToUint64(obj.value(QStringLiteral("total")));
    return true;
}

QJsonObject MultisigStatus::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("multisig"), multisig);
    o.insert(QStringLiteral("ready"), ready);
    o.insert(QStringLiteral("threshold"),
              QJsonValue(static_cast<double>(threshold)));
    o.insert(QStringLiteral("total"),
              QJsonValue(static_cast<double>(total)));
    return o;
}

// =============================================================================
// FreezeApi
// =============================================================================

bool FreezeResult::parse(const QJsonObject& obj, FreezeResult& out)
{
    out.tx_hash = obj.value(QStringLiteral("tx_hash")).toString();
    out.tx_key  = obj.value(QStringLiteral("tx_key")).toString();
    return true;
}

QJsonObject FreezeResult::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("tx_hash"), tx_hash);
    o.insert(QStringLiteral("tx_key"), tx_key);   // deep-copied into envelope
    // P0 #3 (2026-07-23): tx_key is a private tx key (same sensitivity class
    // as TransferResult::tx_key). Scrub the original after the deep copy.
    scrubField(tx_key);
    return o;
}

// =============================================================================
// OtherApi
// =============================================================================

QJsonObject CreateWalletRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("filename"), filename);
    o.insert(QStringLiteral("password"), password);   // deep-copied into envelope
    // P0 #3 (2026-07-23): scrub the wallet password after the deep copy.
    scrubField(password);
    o.insert(QStringLiteral("language"), language);
    return o;
}

bool CreateWalletRequest::parse(const QJsonObject& obj, CreateWalletRequest& out)
{
    out.filename = obj.value(QStringLiteral("filename")).toString();
    out.password = obj.value(QStringLiteral("password")).toString();
    out.language = obj.value(QStringLiteral("language"))
                       .toString(QStringLiteral("English"));
    return true;
}

QJsonObject OpenWalletRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("filename"), filename);
    o.insert(QStringLiteral("password"), password);   // deep-copied into envelope
    // P0 #3 (2026-07-23): scrub the wallet password after the deep copy.
    scrubField(password);
    return o;
}

bool OpenWalletRequest::parse(const QJsonObject& obj, OpenWalletRequest& out)
{
    out.filename = obj.value(QStringLiteral("filename")).toString();
    out.password = obj.value(QStringLiteral("password")).toString();
    return true;
}

// -----------------------------------------------------------------------------
// QueryKeyRequest (st-151)
// -----------------------------------------------------------------------------

QJsonObject QueryKeyRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("key_type"), key_type);
    return o;
}

QJsonObject CloseWalletRequest::toJson() const
{
    QJsonObject o;
    if (!filename.isEmpty()) {
        o.insert(QStringLiteral("filename"), filename);
    }
    return o;
}

bool CloseWalletRequest::parse(const QJsonObject& obj, CloseWalletRequest& out)
{
    out.filename = obj.value(QStringLiteral("filename")).toString();
    return true;
}

QJsonObject ChangeWalletPasswordRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("old_password"), old_password);
    o.insert(QStringLiteral("new_password"), new_password);
    // P0 #3 (2026-07-23): scrub both passwords after the deep copies.
    scrubField(old_password);
    scrubField(new_password);
    if (!filename.isEmpty()) {
        o.insert(QStringLiteral("filename"), filename);
    }
    return o;
}

bool ChangeWalletPasswordRequest::parse(const QJsonObject& obj,
                                        ChangeWalletPasswordRequest& out)
{
    out.old_password = obj.value(QStringLiteral("old_password")).toString();
    out.new_password = obj.value(QStringLiteral("new_password")).toString();
    out.filename     = obj.value(QStringLiteral("filename")).toString();
    return true;
}

QJsonObject GenerateFromKeysRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("filename"), filename);
    o.insert(QStringLiteral("password"), password);
    // st-012: wallet-rpc v3.2.0 requires viewkey + address to be
    // present in the JSON body for ALL recovery modes (the server
    // checks field presence before branching on seed vs key logic).
    // Omitted fields trigger "field 'viewkey' is mandatory".
    // Always include them; wallet-rpc derives from seed when seed
    // is non-empty, uses key values otherwise. (st-130, 2026-07-17).
    o.insert(QStringLiteral("address"), address);
    o.insert(QStringLiteral("viewkey"), viewkey);
    // st-022: include spendkey only when non-empty. Forwarding an empty
    // "spendkey" key would fail strict wallet-rpc validation; skipping
    // it is the documented "view-only" path.
    if (!spendkey.isEmpty()) {
        o.insert(QStringLiteral("spendkey"), spendkey);
    }
    // st-012: include seed only when non-empty. Forwarding an empty
    // "seed" key is harmless to wallet-rpc (it ignores empty strings),
    // but skipping it entirely keeps backward-compatible payloads for
    // existing Case 4 (address+viewkey+spendkey) callers.
    if (!seed.isEmpty()) {
        o.insert(QStringLiteral("seed"), seed);
    }
    // P0 #3 (2026-07-23): all four fields above were deep-copied into the
    // envelope. Now scrub the originals (password / private view+spend keys
    // / mnemonic seed). `address` is a public address - not scrubbed.
    scrubField(password);
    scrubField(viewkey);
    scrubField(spendkey);
    scrubField(seed);
    // st-022: restore_height + autosave_current are always sent so the
    // wallet-rpc defaults match (0 = full chain scan, false = no
    // autosave). Wallet-rpc ignores these when seed is set.
    o.insert(QStringLiteral("restore_height"),
             static_cast<qint64>(restore_height));
    o.insert(QStringLiteral("autosave_current"), autosave_current);
    return o;
}

bool GenerateFromKeysRequest::parse(const QJsonObject& obj,
                                     GenerateFromKeysRequest& out)
{
    out.filename = obj.value(QStringLiteral("filename")).toString();
    out.password = obj.value(QStringLiteral("password")).toString();
    out.address  = obj.value(QStringLiteral("address")).toString();
    out.viewkey  = obj.value(QStringLiteral("viewkey")).toString();
    // st-022: spendkey is optional (only set on Case 4 B-template).
    out.spendkey = obj.value(QStringLiteral("spendkey")).toString();
    out.seed     = obj.value(QStringLiteral("seed")).toString();
    // st-022: restore_height + autosave_current. We accept either
    // numeric or string forms for restore_height (forward-compat with
    // old v3.2.0 envelope quirks where heights were sometimes quoted).
    const QJsonValue rh = obj.value(QStringLiteral("restore_height"));
    if (rh.isDouble()) {
        out.restore_height = static_cast<quint64>(rh.toDouble());
    } else if (rh.isString()) {
        out.restore_height = rh.toString().toULongLong();
    } else {
        out.restore_height = 0;
    }
    out.autosave_current =
        obj.value(QStringLiteral("autosave_current")).toBool(false);
    return true;
}

QJsonObject GenerateFromJsonRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("filename"), filename);
    o.insert(QStringLiteral("password"), password);
    o.insert(QStringLiteral("json"), json_string);
    // P0 #3 (2026-07-23): scrub the wallet password and the full JSON
    // snapshot after the deep copies.
    scrubField(password);
    scrubField(json_string);
    return o;
}

bool GenerateFromJsonRequest::parse(const QJsonObject& obj,
                                     GenerateFromJsonRequest& out)
{
    out.filename    = obj.value(QStringLiteral("filename")).toString();
    out.password    = obj.value(QStringLiteral("password")).toString();
    out.json_string = obj.value(QStringLiteral("json")).toString();
    return true;
}

// -----------------------------------------------------------------------------
// RestoreDeterministicRequest (st-150)
// -----------------------------------------------------------------------------

QJsonObject RestoreDeterministicRequest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("filename"), filename);
    o.insert(QStringLiteral("password"), password);
    o.insert(QStringLiteral("seed"), seed);
    o.insert(QStringLiteral("restore_height"),
             static_cast<qint64>(restore_height));
    o.insert(QStringLiteral("language"), language);
    // P0 #3 (2026-07-23): scrub the wallet password and the mnemonic seed
    // after the deep copies.
    scrubField(password);
    scrubField(seed);
    return o;
}

bool RestoreDeterministicRequest::parse(const QJsonObject& obj,
                                         RestoreDeterministicRequest& out)
{
    out.filename = obj.value(QStringLiteral("filename")).toString();
    out.password = obj.value(QStringLiteral("password")).toString();
    out.seed     = obj.value(QStringLiteral("seed")).toString();
    const QJsonValue rh = obj.value(QStringLiteral("restore_height"));
    if (rh.isDouble()) {
        out.restore_height = static_cast<quint64>(rh.toDouble());
    } else if (rh.isString()) {
        out.restore_height = rh.toString().toULongLong();
    } else {
        out.restore_height = 0;
    }
    out.language = obj.value(QStringLiteral("language")).toString(
        QStringLiteral("English"));
    return true;
}

} // namespace judecoin_wallet::core::rpc
