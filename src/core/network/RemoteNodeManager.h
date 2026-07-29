// =============================================================================
// Judecoin Wallet - RemoteNodeManager (st-023)
//
// Resolves the user's remote-node selection (preset OR custom) into a
// concrete (host, port) pair that the StartupManager hands to the
// DaemonClient. Pure data + signal class - no network I/O, no daemon
// handshake, no QSettings writes.
//
// Why a separate class instead of folding the logic into SettingsManager
// ---------------------------------------------------------------------
// SettingsManager is the *persistence* layer (QSettings read/write +
// signal pulse). RemoteNodeManager is the *resolution* layer: it knows
// the preset table and the "preset vs custom" rules. Keeping them apart
// means SettingsManager can stay I/O-focused and unit-testable with a
// mocked QSettings, while RemoteNodeManager can be exercised with no
// QSettings at all (just feed it a preset id or custom host/port).
//
// Signal contract:
//   nodeChanged(host, port, source) - emitted whenever the active node
//                                     changes (preset switch, custom
//                                     host/port edit, "use local"
//                                     toggle). `source` is one of
//                                     {"preset", "custom", "local"} so
//                                     the UI can render a "Source:
//                                     Official Judecoin Node" label.
//
// Lifetime:
//   QObject parent / heap-allocated. Single instance per application;
//   StartupManager owns it and rebroadcasts its nodeChanged() signal to
//   QML as the application-wide "current daemon target".
//
// V5.4.2 mainnet-only: every resolution returns a port from the mainnet
// range (19061). The struct still carries a nettype field so future
// releases can branch on testnet/stagenet without an ABI break.
// =============================================================================

#ifndef JUDECOIN_WALLET_CORE_NETWORK_REMOTE_NODE_MANAGER_H
#define JUDECOIN_WALLET_CORE_NETWORK_REMOTE_NODE_MANAGER_H

#include "core/settings/SettingsManager.h"
#include "core/version/VersionChecker.h"

#include <QObject>
#include <QString>

namespace judecoin_wallet::core::network {

class RemoteNodeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activeHost READ activeHost NOTIFY nodeChanged)
    Q_PROPERTY(int     activePort READ activePort NOTIFY nodeChanged)
    Q_PROPERTY(QString activeSource READ activeSourceString NOTIFY nodeChanged)
    Q_PROPERTY(QString activeLabel READ activeLabel NOTIFY nodeChanged)

public:
    enum class Source {
        Local,    // no remote node -> StartupManager spawns judecoind
        Preset,   // presetNode() matched in the preset table
        Custom,   // isCustomRemote() == true -> use raw remoteDaemonHost/Port
    };
    Q_ENUM(Source)

    // st-111: NetworkSettings.qml's "Test Connection" button calls
    // remoteNodeObj.testConnection(host, port) and matches the returned
    // enum to one of four user-visible banners (per V5.4 #7 decision).
    //
    //   Ok                  - daemon responded AND its version_string
    //                         is in the supported range (>= v3.2.0).
    //   ConnectionRefused   - TCP connection refused (no daemon
    //                         listening, or wrong port).
    //   Timeout             - TCP connected but no RPC response
    //                         inside the configured timeout window
    //                         (kTestConnectionTimeoutMs = 1500ms).
    //   VersionMismatch     - daemon responded but its version_string
    //                         is older than v3.2.0 (or unparseable),
    //                         so the wallet can not drive it.
    //
    // The 4-way enum is what the spec calls "error classification"; QML does an
    // if/else over the result and renders the matching banner.
    enum class ConnectionTestResult {
        Ok                  = 0,
        ConnectionRefused   = 1,
        Timeout             = 2,
        VersionMismatch     = 3,
    };
    Q_ENUM(ConnectionTestResult)

    // st-111: total budget for a single testConnection() call.
    // 1500ms is short enough that the UI stays responsive on a slow
    // custom remote, and long enough that a healthy local daemon
    // replies well within it.
    static constexpr int kTestConnectionTimeoutMs = 1500;

    explicit RemoteNodeManager(judecoin_wallet::core::settings::SettingsManager* settings,
                               QObject* parent = nullptr);

    // -- Current resolution --------------------------------------------------

    /// Resolved host the StartupManager should use for the daemon RPC.
    /// Empty string means "no remote node selected" -> local daemon.
    QString activeHost() const { return activeHost_; }
    int     activePort() const { return activePort_; }

    /// Where activeHost/activePort came from. Matches one of the
    /// three enum values above.
    Source activeSource() const { return activeSource_; }

    /// QString view of activeSource() for QML.
    QString activeSourceString() const;

    /// Human-readable label for the current node, e.g. "Official
    /// Judecoin Node" or "Custom: node.example.com:19061".
    QString activeLabel() const;

    // -- Imperative refresh --------------------------------------------------
    //
    // Re-reads SettingsManager and re-resolves. Cheap (no I/O, just
    // in-memory lookups) so callers can invoke this after every settings
    // write or on demand.

    void refresh();

    // -- Convenience helpers (used by tests + QML binding) -------------------

    /// Construct the JSON-RPC URL the StartupManager / DaemonClient should
    /// hit for this resolved node. Always uses http:// - mainnet RPC is
    /// plaintext on localhost / a trusted remote.
    QUrl endpointUrl() const;

    // -- Connection probe (st-111) ------------------------------------------
    //
    // NetworkSettings.qml's "Test Connection" button calls
    // remoteNodeObj.testConnection(host, port) (Q_INVOKABLE so QML can
    // reach it). The function makes one best-effort attempt to GET
    // /json_rpc and categorises the outcome into one of the four
    // ConnectionTestResult enum values. The host/port arguments are
    // the user-typed values (custom OR preset OR local 127.0.0.1) -
    // we do NOT silently substitute activeHost()/activePort() so the
    // UI can probe arbitrary typed strings before saving.
    //
    // Behaviour summary (categorisation rules - documented here so
    // both the QML banner and the unit tests agree):
    //
    //   - TCP connect() refused       -> ConnectionRefused
    //   - TCP connect() succeeded but
    //     no HTTP response inside
    //     kTestConnectionTimeoutMs    -> Timeout
    //   - HTTP response received but
    //     not parseable JSON          -> ConnectionRefused
    //     (we treat unparseable as
    //      "the daemon is wrong" -
    //      same UX class as refused
    //      from a user's perspective)
    //   - HTTP response is JSON but
    //     version_string missing OR
    //     fails VersionChecker::isSupported
    //                                  -> VersionMismatch
    //   - HTTP response is JSON AND
    //     version_string parses + is supported
    //                                  -> Ok
    //
    // Synchronous. The QML caller wraps the call in a WorkerScript or
    // runs it from a non-UI slot; the 1500ms ceiling keeps the UI
    // snappy. Returns immediately on success, on refused, or on
    // unparseable body. Returns Timeout only when the HTTP layer
    // itself never produced a response.
    Q_INVOKABLE ConnectionTestResult testConnection(const QString& host,
                                                     int port) const;

signals:
    void nodeChanged(const QString& host, int port, const QString& source);

private:
    judecoin_wallet::core::settings::SettingsManager* settings_ = nullptr;

    QString activeHost_;
    int     activePort_   = 0;
    Source  activeSource_ = Source::Local;
};

} // namespace judecoin_wallet::core::network

#endif // JUDECOIN_WALLET_CORE_NETWORK_REMOTE_NODE_MANAGER_H
