// =============================================================================
// Judecoin Wallet - PendingTxState implementation (st-013)
//
// Pure data + state-machine transitions. No Qt signals, no QObject parent
// (this class is intentionally NOT a QObject - it is owned as a value
// inside QHash inside CrashRecoveryManager, and we don't want to leak
// QObject parent trees). The owning class emits UI-facing signals.
// =============================================================================

#include "core/recovery/PendingTxState.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>

namespace judecoin_wallet::core::recovery {

// -----------------------------------------------------------------------------
// State name helpers
// -----------------------------------------------------------------------------

QString PendingTxState::stateName(State s)
{
    switch (s) {
        case State::NotGenerated:          return QStringLiteral("not_generated");
        case State::GeneratedNotBroadcast: return QStringLiteral("generated_not_broadcast");
        case State::GeneratedAndBroadcast: return QStringLiteral("generated_and_broadcast");
    }
    return QStringLiteral("unknown");
}

PendingTxState::State PendingTxState::stateFromName(const QString& name)
{
    if (name == QStringLiteral("not_generated"))            return State::NotGenerated;
    if (name == QStringLiteral("generated_not_broadcast"))   return State::GeneratedNotBroadcast;
    if (name == QStringLiteral("generated_and_broadcast"))   return State::GeneratedAndBroadcast;
    return State::NotGenerated;
}

// -----------------------------------------------------------------------------
// Factories
// -----------------------------------------------------------------------------

PendingTxState PendingTxState::init(const QString& transferId,
                                    quint64 amountAtomic,
                                    const QString& primaryAddress,
                                    qint64 startedAtMs)
{
    PendingTxState out;
    out.transferId_      = transferId;
    out.amountAtomic_    = amountAtomic;
    out.primaryAddress_  = primaryAddress;
    out.startedAtMs_     = (startedAtMs > 0) ? startedAtMs : QDateTime::currentMSecsSinceEpoch();
    out.state_           = State::NotGenerated;
    return out;
}

PendingTxState PendingTxState::fromJson(const QJsonObject& obj)
{
    PendingTxState out;
    out.transferId_      = obj.value(QStringLiteral("id")).toString();
    out.amountAtomic_    = static_cast<quint64>(obj.value(QStringLiteral("amount")).toDouble(0.0));
    out.primaryAddress_  = obj.value(QStringLiteral("address")).toString();
    out.txHash_          = obj.value(QStringLiteral("tx_hash")).toString();
    out.startedAtMs_     = static_cast<qint64>(obj.value(QStringLiteral("started_at_ms")).toDouble(0.0));
    out.generatedAtMs_   = static_cast<qint64>(obj.value(QStringLiteral("generated_at_ms")).toDouble(0.0));
    out.broadcastAtMs_   = static_cast<qint64>(obj.value(QStringLiteral("broadcast_at_ms")).toDouble(0.0));
    out.orphaned_        = obj.value(QStringLiteral("orphaned")).toBool(false);
    out.acknowledged_   = obj.value(QStringLiteral("acknowledged")).toBool(false);
    out.state_           = stateFromName(obj.value(QStringLiteral("state")).toString());
    return out;
}

// -----------------------------------------------------------------------------
// Transitions
// -----------------------------------------------------------------------------

bool PendingTxState::updateGenerated(const QString& txHash, qint64 atMs)
{
    if (txHash.isEmpty()) {
        return false;
    }
    if (state_ == State::GeneratedNotBroadcast || state_ == State::GeneratedAndBroadcast) {
        // Already past NotGenerated - re-confirmation. Update tx_hash only
        // if the caller is providing a different one (defensive).
        if (txHash_ != txHash) {
            txHash_ = txHash;
        }
        return true;
    }
    if (state_ != State::NotGenerated) {
        return false;
    }
    txHash_         = txHash;
    state_          = State::GeneratedNotBroadcast;
    generatedAtMs_  = (atMs > 0) ? atMs : QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool PendingTxState::updateBroadcast(qint64 atMs)
{
    if (state_ != State::GeneratedNotBroadcast) {
        return false;
    }
    state_          = State::GeneratedAndBroadcast;
    broadcastAtMs_  = (atMs > 0) ? atMs : QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool PendingTxState::markOrphaned()
{
    if (state_ != State::GeneratedNotBroadcast) {
        return false;
    }
    if (orphaned_) {
        return false;  // idempotent: already marked orphaned
    }
    orphaned_ = true;
    return true;
}

bool PendingTxState::markAcknowledged()
{
    if (state_ != State::GeneratedAndBroadcast) {
        return false;
    }
    if (acknowledged_) {
        return false;  // idempotent: already acknowledged
    }
    acknowledged_ = true;
    return true;
}

// -----------------------------------------------------------------------------
// Serialisation
// -----------------------------------------------------------------------------

QJsonObject PendingTxState::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"),               transferId_);
    obj.insert(QStringLiteral("amount"),           static_cast<double>(amountAtomic_));
    obj.insert(QStringLiteral("address"),          primaryAddress_);
    obj.insert(QStringLiteral("tx_hash"),          txHash_);
    obj.insert(QStringLiteral("state"),            stateName(state_));
    obj.insert(QStringLiteral("started_at_ms"),    static_cast<double>(startedAtMs_));
    obj.insert(QStringLiteral("generated_at_ms"),  static_cast<double>(generatedAtMs_));
    obj.insert(QStringLiteral("broadcast_at_ms"),  static_cast<double>(broadcastAtMs_));
    obj.insert(QStringLiteral("orphaned"),         orphaned_);
    obj.insert(QStringLiteral("acknowledged"),     acknowledged_);
    return obj;
}

QVector<PendingTxState> PendingTxState::allFromJsonArray(const QJsonArray& arr)
{
    QVector<PendingTxState> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        PendingTxState p = fromJson(v.toObject());
        if (p.isValid()) {
            out.append(p);
        }
    }
    return out;
}

QJsonArray PendingTxState::allToJsonArray(const QVector<PendingTxState>& list)
{
    QJsonArray arr;
    for (const PendingTxState& p : list) {
        if (p.isValid()) {
            arr.append(p.toJson());
        }
    }
    return arr;
}

} // namespace judecoin_wallet::core::recovery
