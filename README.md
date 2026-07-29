# Judecoin Wallet

A Qt 6 QML desktop wallet client for the Judecoin (Loki/Oxen) cryptocurrency. Communicates with a local `judecoind` daemon and `judecoin-wallet-rpc` via HTTP JSON-RPC over loopback.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  judecoin-wallet-ui                     │
│                        (Qt 6 QML)                       │
│  ┌────────────────────┬──────────────────────────────┐  │
│  │       QML UI       │      C++ Core Library        │  │
│  │  (src/qml/)       │  (src/core/, static .a)      │  │
│  │  main.qml         │  - RPC Clients (daemon,      │  │
│  │  Dashboard         │    wallet, HTTP)              │  │
│  │  Send / Receive    │  - Process Management         │  │
│  │  History / Tx      │  - Wallet Manager             │  │
│  │  Onboarding        │  - Transaction Tracker        │  │
│  │  Settings          │  - Settings Manager           │  │
│  │  Sidebar           │  - Local Data Store           │  │
│  └─────────┬──────────┴──────────────┬───────────────┘  │
│            │                         │                  │
│         HTTP JSON-RPC            (IPC / process)         │
│            │                         │                  │
│  ┌─────────▼──────────┐  ┌──────────▼──────────────┐  │
│  │  judecoin-         │  │   judecoind              │  │
│  │  wallet-rpc        │  │   (RPC daemon, v3.2+)   │  │
│  │  (wallet backend)  │  └─────────────────────────┘  │
│  └────────────────────┘                                │
└─────────────────────────────────────────────────────────┘
```

## Directory Structure

```
├── CMakeLists.txt              # Top-level build config
├── src/
│   ├── CMakeLists.txt          # Build config for src/
│   ├── main.cpp                # Entry point, QML engine setup
│   ├── core/                   # C++ business logic (static library)
│   │   ├── commands/           # CLI command set loader (v3.2.0+)
│   │   ├── i18n/               # Internationalization manager
│   │   ├── localdata/          # Local data: address book, tx notes, tags
│   │   ├── lock/               # Lock manager header
│   │   ├── network/            # Port conflict detection, remote node mgmt
│   │   ├── process/            # Daemon / wallet-rpc process lifecycle
│   │   ├── recovery/           # Crash recovery, pending tx state
│   │   ├── rpc/                # HTTP RPC clients (daemon, wallet, address)
│   │   ├── settings/           # Settings manager, schema definition
│   │   ├── startup/            # Startup sequence coordinator
│   │   ├── subaddress/         # Subaddress list management
│   │   ├── tracker/            # Balance & transaction tracking
│   │   ├── uri/                # URI parser (address + amount)
│   │   ├── util/               # Utilities (XMR conversion, misc)
│   │   ├── version/            # Version checker st-008
│   │   └── wallet/             # Wallet manager, recovery manager
│   └── qml/                    # QML UI (Qt Quick)
│       ├── main.qml            # Root component
│       ├── OnboardingWizard    # Wallet setup wizard (create / open / recover)
│       ├── Dashboard           # Main dashboard with balance + actions
│       ├── SendPage            # Send transaction form
│       ├── ReceivePage         # Receive page with QR code
│       ├── HistoryPage         # Full transaction history
│       ├── Settings            # 8-category settings (network, language, etc.)
│       ├── Sidebar             # Navigation sidebar
│       ├── ConfirmModal        # Password confirmation dialog
│       ├── TxDetailDialog      # Transaction detail view
│       └── ...                 # Supporting components & dialogs
├── cmake/                      # CMake modules (FindLibMoneroWallet.cmake)
├── packaging/                  # AppImage / distribution packaging
└── i18n/                       # Translation source files (.ts, .qm)
```

## Dependencies

| Dependency       | Minimum Version | Notes                           |
|-----------------|----------------|---------------------------------|
| Qt              | 6.6+ (dev: 6.4+) | Core, Gui, Qml, Quick,        |
|                 |                | QuickControls2, Network, Test,  |
|                 |                | Widgets (for error dialogs)     |
| CMake           | 3.21+          |                                 |
| C++ Standard    | C++17          |                                 |
| libqrencode     | (any)          | QR code generation (dev only)   |
| PkgConfig       | (any)          | Build-time dependency discovery |

## Building

### Quick Start

```bash
# Install Qt 6 (if not already installed, e.g., via apt)
sudo apt install qt6-base-dev qt6-quick-dev qt6-quickcontrols2-dev \
                 qt6-tools-dev qt6-l10n-tools libqrencode-dev \
                 pkg-config cmake g++

# Clone and build
mkdir build && cd build
cmake ..
make -j$(nproc)

# The binary lands at:
#   build/bin/judecoin-wallet-ui
#   build/bin/libjudecoin-wallet-core.a
```

### CMake Options

| Option                       | Default | Description                        |
|------------------------------|---------|------------------------------------|
| `-DCMAKE_BUILD_TYPE=Debug`   | Release | Debug build with symbols           |
| `-DJUDECOIN_WALLET_WERROR=ON`| OFF     | Treat compiler warnings as errors  |
| `-DJUDECOIN_VENDOR_SOURCE_DIR=...` | /root/judecoin/build/... | Path to vendored daemon + wallet-rpc binaries |

### Build Artifacts

- `build/bin/judecoin-wallet-ui` — Qt 6 QML executable (~1 MB stripped)
- `build/bin/libjudecoin-wallet-core.a` — static C++ core library

## Running

### Prerequisites

The wallet communicates with two external binaries:

1. **judecoind** — Judecoin full node daemon (v3.2+)
2. **judecoin-wallet-rpc** — Wallet RPC backend (v3.2+)

These binaries are **not** included in this repository. You must:

- Download the official Judecoin binaries from the [Judecoin releases page](https://github.com/Judecoin/judecoin/releases), or
- Build them from [source](https://github.com/Judecoin/judecoin).

### Launch

```bash
# Start the daemon (if not already running)
judecoind --data-dir /path/to/data --rpc-bind-port 19061

# Start the wallet RPC
judecoin-wallet-rpc --daemon-address 127.0.0.1:19061 \
                    --rpc-bind-port 19071 \
                    --wallet-dir /path/to/wallets

# Launch the UI
./build/bin/judecoin-wallet-ui
```

The UI connects to `127.0.0.1` via HTTP JSON-RPC:
- Daemon RPC: port **19061**
- Wallet RPC: port **19071**

## Features

- **Onboarding Wizard**: Create, open, or recover wallets from seed
- **Dashboard**: Balance overview, recent transactions, quick actions (Send, Receive, Subaddresses)
- **Send**: Multi-field form with address validation, amount input, priority selection
- **Receive**: QR code generation, subaddress management
- **History**: Full transaction history with search, filtering (All/Incoming/Outgoing/Pending), transaction details, and editable notes
- **Address Book**: Labeled address management with import/export
- **Settings**: 8 categories (Network, Language, Auto-Lock, Transfer Confirm, Appearance, Advanced, Reset, About)
- **Internationalization**: 9 languages (en, zh_TW, de, ja, ko, it, nl, ru, es)
- **Security**: Per V5.4.7 compliance — no key/seed/password material exposed as QML properties; all secrets handled server-side by wallet-rpc

## Known Limitations

This project was developed as a reference implementation. Since the Judecoin cryptocurrency is not currently in active circulation, the following wallet features have **not been tested** against a live network:

| Feature | Limitation |
|---------|-----------|
| **Transfers** (Send / Receive) | Not tested — no live network available to execute or confirm transactions |
| **Account balance** | Not tested — balance display and synchronization against actual wallet state not verified |
| **Transaction history sync** | Not tested — blockchain scanning and transaction record reconciliation not performed on a live daemon |

These features are structurally complete in the codebase and follow the documented API contracts of the Judecoin daemon and wallet RPC. However, end-to-end testing with real network conditions has not been performed. Use at your own risk.

---

## Platforms

- **Primary target**: Linux (AppImage distribution)
- **Build host**: WSL (Ubuntu), cross-build for x86_64 Linux
- **Other**: Builds on native Linux; Windows/macOS builds not currently supported

## License

This project is licensed under the BSD 3-Clause License. See [LICENSE](LICENSE) for details.

## Development Notes

- The project uses Qt 6 QML with `qt_add_executable` and `qt_add_qml_module` (Qt 6.5+).
- Core business logic lives in `src/core/` as a static library. The UI links against it.
- The daemon + wallet-rpc binaries are vendored into the AppImage at build time (via `JUDECOIN_VENDOR_SOURCE_DIR`).
- Translations use Qt Linguist (.ts → .qm). Update with `make lupdate` / `make lrelease`.
- `AUTOMOC` is enabled for the core library, so Q_OBJECT classes generate moc files automatically.
- The project enforces C++17 with strict warnings (`-Wall -Wextra -Wno-unused-parameter`).
