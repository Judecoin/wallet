# =============================================================================
# Judecoin Wallet - src/ CMakeLists
# =============================================================================
#
# Produces:
#   libjudecoin-wallet-core.a   (static core library, all C++ business logic)
#   judecoin-wallet-ui          (executable, links core + Qt + minimal main.cpp)
#
# Directory layout (per docs/ARCHITECTURE.md V5.4.9):
#   src/
#     main.cpp                       - entry point, installs QML engine
#     core/                          - business logic (st-003..020 modules)
#       rpc/        HttpRpcClient, DaemonClient, WalletRpcClient  (st-003..005)
#       process/    ProcessManager, RestartStrategy               (st-006)
#       network/    PortConflictDetector                          (st-007)
#       version/    VersionChecker                                 (st-008)
#       commands/   CommandSetLoader                               (st-009)
#       wallet/     WalletManager, RecoveryManager                 (st-011..012)
#       recovery/   CrashRecoveryManager                           (st-013)
#       settings/   SettingsManager                                (st-014)
#       startup/    StartupManager                                 (st-015)
#       localdata/  LocalDataManager (4 .json files)               (st-017)
#       tracker/    TransactionTracker                             (st-018)
#       subaddress/ SubaddressManager                              (st-019)
#       uri/        UriHandler                                     (st-020)
#     ui/                           - QML facade wrappers (added later)
#     qml/                          - QML files (added later)
#     i18n/                         - translation source files (st-016)
#
# For st-001 (project scaffold + CMake), only main.cpp exists. Subsequent
# stages add the core/* modules and update CORE_SOURCES accordingly.
# =============================================================================

# -----------------------------------------------------------------------------
# 1. Core static library
# -----------------------------------------------------------------------------
# All C++ business logic goes into libjudecoin-wallet-core.a. Tests and the
# final executable link against this library.
# -----------------------------------------------------------------------------

set(CORE_SOURCES
    # V1.0 main entry point lives at the top of src/, but it's wired into
    # the executable target below (not into the static lib) so that
    # test harnesses can pick it up explicitly when needed.
    # core/ subdirs are picked up by add_subdirectory() calls below.
)

# Recursive: every core/<module>/*.{h,cpp} becomes part of the static lib.
# We use file(GLOB) but lock it down to .cpp / .h extensions only; the
# result is captured at CMake configure time, not at build time.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h"
)

# Track whether st-001 has any real core modules yet.
list(LENGTH CORE_SOURCES CORE_SOURCES_COUNT)

add_library(judecoin-wallet-core STATIC ${CORE_SOURCES})

target_include_directories(judecoin-wallet-core
    PUBLIC
        # PUBLIC so tests can include e.g. <core/rpc/HttpRpcClient.h>
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(judecoin-wallet-core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Network
        # st-024: libqrencode is the QR encoder. Transitive PUBLIC link so
        # the static lib's QRGenerator.h consumers (the test binary, and
        # any future QQuickImageProvider subclass) get -lqrencode too.
        # PkgConfig::LIBQRENCODE is an IMPORTED target created by
        # pkg_check_modules(LIBQRENCODE REQUIRED IMPORTED_TARGET libqrencode)
        # in the top-level CMakeLists.txt.
        PkgConfig::LIBQRENCODE
)

# Bake the source-tree audit/cli_commands path into the binary so
# CommandSetLoader can find v3.2.0.json under both the production binary
# and the test harness (ctest CWD != project root).
target_compile_definitions(judecoin-wallet-core
    PUBLIC
        JUDECOIN_WALLET_AUDIT_DIR="${CMAKE_SOURCE_DIR}/audit/cli_commands"
)

set_target_properties(judecoin-wallet-core PROPERTIES
    OUTPUT_NAME              "judecoin-wallet-core"
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC                  ON   # generate moc_*.cpp for Q_OBJECT classes
                                  # (HttpRpcClient, DaemonClient, WalletRpcClient,
                                  #  ...). Without this, linkers fail with
                                  #  "undefined vtable" errors for any class
                                  #  in this library that has signals/slots.
    AUTORCC                  ON
    AUTOUIC                  ON
)

# Print a status line so the build log shows what was actually picked up
message(STATUS "judecoin-wallet-core sources: ${CORE_SOURCES_COUNT} files")
if(CORE_SOURCES_COUNT EQUAL 0)
    message(STATUS "  (st-001 scaffold only - no core/ modules yet)")
endif()

# -----------------------------------------------------------------------------
# 2. UI subdirectories
# -----------------------------------------------------------------------------
# These may not exist yet (st-001 ships only main.cpp). Guard each
# add_subdirectory with EXISTS so that later stages can drop in
# core/<module>/, ui/, qml/, i18n/ directories without breaking the build.
# -----------------------------------------------------------------------------

foreach(_subdir core ui qml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_subdir}/CMakeLists.txt")
        add_subdirectory(${_subdir})
    endif()
endforeach()

# -----------------------------------------------------------------------------
# 3. Executable: judecoin-wallet-ui
# -----------------------------------------------------------------------------

qt_add_executable(judecoin-wallet-ui
    main.cpp
)

target_link_libraries(judecoin-wallet-ui
    PRIVATE
        judecoin-wallet-core
        # FIX-G3 (2026-07-17 17:41): Qt6::Widgets is required by
        # main.cpp's std::set_terminate hook + app.exec() try/catch, which
        # use QMessageBox::critical() to surface unhandled exceptions as
        # a UI dialog instead of terminating with core dump. QMessageBox
        # lives in Qt6::Widgets (not Qt6::Gui), so without this link the
        # compiler reports "fatal error: QMessageBox: No such file or
        # directory" even though judecoin-wallet-core links fine.
        Qt6::Widgets
)

set_target_properties(judecoin-wallet-ui PROPERTIES
    OUTPUT_NAME              "judecoin-wallet-ui"
    WIN32_EXECUTABLE         OFF  # Linux desktop, show stdout
    MACOSX_BUNDLE            OFF
    AUTOMOC                  ON
    AUTORCC                  ON
    AUTOUIC                  ON
)

# -----------------------------------------------------------------------------
# 3b. POST_BUILD: vendor the official v3.2.1 daemon + wallet-rpc
#     binaries alongside the UI binary (st-114).
#
# The actual transfer happens in the `judecoin` WSL instance (where the
# builds run). We invoke `wsljr cp` (which is `wsl.exe -d judecoin cp`
# via the OpenClaw-wsl wrapper) to pull judecoind + judecoin-wallet-rpc
# out of /root/judecoin/build/Linux/master/release/bin (which is only
# readable from inside the judecoin WSL by that instance's zhangxi user
# thanks to a 0700/root-owned /root). The result lands in
# ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/vendor/ so the AppRun fallback
# (packaging/AppRun) can pick it up if the AppImage installer missed a
# binary, AND so the top-level install(CODE ...) hook can install the
# binaries alongside the UI at `cmake --install` time.
#
# Windows-host guard (per spec):
#   if(EXISTS /mnt/c/Windows/System32/wsl.exe) → invoke wsljr (works on
#     Linux/WSL hosts where the judecoin instance is reachable).
#   otherwise → skip the POST_BUILD step entirely so a Windows-native
#     developer build still produces a working judecoin-wallet-ui.
# -----------------------------------------------------------------------------

# Detect the host environment: a Windows host (the OpenClaw dev
# workstation) won't have wsljr available in the build path, but it
# DOES have wsl.exe. A Linux WSL host running the build has neither wsl.exe
# nor wsljr on PATH by default but the wsljr wrapper script (custom for
# this project) is on PATH inside the OpenClaw-driven CI shell. We
# pick: invoke when wsljr is on PATH (the canonical setup), else fall
# back to wsl.exe direct if present, else no-op.
find_program(WSLJR_EXECUTABLE
    NAMES wsljr wsl
    DOC   "wsljr (or wsl.exe) used to copy vendored binaries from the judecoin WSL"
)

if(JUDECOIN_VENDOR_SOURCE_DIR AND WSLJR_EXECUTABLE AND
   EXISTS "${JUDECOIN_VENDOR_SOURCE_DIR}")
    set(VENDOR_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/vendor")
    foreach(_bin ${JUDECOIN_VENDOR_BINARIES})
        add_custom_command(TARGET judecoin-wallet-ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VENDOR_DIR}"
            COMMAND ${WSLJR_EXECUTABLE} cp
                    "${JUDECOIN_VENDOR_SOURCE_DIR}/${_bin}"
                    "${VENDOR_DIR}/${_bin}"
            COMMENT "st-114: copy vendored ${_bin} from judecoin WSL via ${WSLJR_EXECUTABLE}"
            VERBATIM
        )
    endforeach()
else()
    message(STATUS "st-114: POST_BUILD vendor copy skipped "
                   "(wsljr='${WSLJR_EXECUTABLE}', source='${JUDECOIN_VENDOR_SOURCE_DIR}')")
endif()


# -----------------------------------------------------------------------------
# 4. Resources (icons, QML modules, translations)
# -----------------------------------------------------------------------------
# These are added in later stages; we declare the QML module scaffolding
# here so that the QML import path is set up from day one.
#
# st-016 will add qt_add_lrelease() / qt_add_lupdate() calls under i18n/.
# st-002 (AppImage) will add packaging-time icon bundling.
# -----------------------------------------------------------------------------

# QML module skeleton (no .qml files in st-001 yet; this is here for the
# day st-002 / QML stages drop in their files so the import name is stable).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/CMakeLists.txt")
    # The QML module itself is built by qml/CMakeLists.txt once st-001+
    # adds it. Until then we keep this branch dormant.
endif()

# st-021b: register qml/main.qml under the qrc:/qml/ prefix so that
# src/main.cpp can load it via QUrl("qrc:/qml/main.qml") once
# StartupManager fires startupComplete.
#
# st-022: also register qml/OnboardingWizard.qml and
# qml/Case4BackupRestore.qml under the same qrc:/qml/ prefix so that
# Loader { source: "qrc:/qml/Case4BackupRestore.qml" } inside
# OnboardingWizard.qml resolves at runtime.
#
# qt_add_resources attaches the generated qrc_<name>.cpp to the given
# target; the resources are then reachable at qrc:/<PREFIX>/<FILES...>.
# We bind it to the executable (judecoin-wallet-ui) rather than the
# static core lib so the QML facade stays UI-binary-local - the core
# lib never embeds QML scenes.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qml/main.qml")
    # Build the FILES list dynamically so adding a new QML scene is a
    # one-line addition. We pick .qml (QML scenes) and .qss (Qt Style
    # Sheets).
    #
    # IMPORTANT (st-030a): use a RELATIVE glob pattern. Absolute paths
    # would make qt_add_resources register each file under its full
    # path inside the resource prefix (e.g. the alias becomes
    # "/mnt/e/.../theme.qss" so the resource lives at
    # :/qml/mnt/e/.../theme.qss instead of :/qml/theme.qss). The QML
    # engine's qrc:/qml/<file> lookup AND QFile("qrc:/qml/<file>") both
    # rely on the clean short form; without relative paths the
    # qrc:/qml/theme.qss probe in main.cpp fails with "No such file".
    file(GLOB JUDECOIN_QML_FILES CONFIGURE_DEPENDS RELATIVE
        "${CMAKE_CURRENT_SOURCE_DIR}/qml"
        "qml/*.qml"
        "qml/*.qss"
    )
    # qt_add_resources wants absolute source paths but the relative
    # path as the alias; rebuild the absolute source list here.
    set(JUDECOIN_QML_SOURCES "")
    foreach(qml_rel ${JUDECOIN_QML_FILES})
        list(APPEND JUDECOIN_QML_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/qml/${qml_rel}")
    endforeach()
    # st-050: BalanceCard.qml (gradient hero card on Dashboard) is picked up
    # by the qml/*.qml glob above; the design contract lives in
    # docs/release/design_specs.md section 5.2 (radius 10, gradient
    # #16a34a -> #22c55e, shadow-card 0 8px 24px rgba(22,163,74,0.25),
    # padding 20/24, Total Balance / amount / USD line). No explicit
    # FILES entry needed - if the glob breaks, every other .qml in this
    # dir breaks too, so this is the right place for the regression to
    # surface.
    #
    # st-051: ActionButtons.qml (3-button row Send/Receive/Subaddresses on
    # Dashboard, under BalanceCard). RowLayout of equal-width Buttons
    # styled directly (Controls 2 Buttons don't pick up QPushButton QSS
    # selectors from theme.qss). Send = primary (accent #22c55e +
    # on-accent #052e16), Receive + Subaddresses = secondary (neutral
    # #d1fae5 + external #166534). Each click emits a sibling signal
    # (sendClicked / receiveClicked / subaddressesClicked) consumed by
    # Dashboard.qml in st-054. No explicit FILES entry needed - same
    # qml/*.qml glob covers it.
    #
    # st-052: TxRow.qml (single transaction row reused by
    # RecentTransactions on the Dashboard and by HistoryPage in a later
    # stage). 32x32 circular avatar (per design_specs §5.5) + ColumnLayout
    # (counterparty + status line) + right-aligned amount with a small
    # direction glyph (U+2191 in green / U+2193 in orange). Six public
    # properties (txHash / direction / counterparty / amount / timestamp /
    # note) plus rowClicked(txHash). Bottom border 1px #dcfce7 (suppressed
    # on the last row via `isLast`). LayoutMirroring enabled for RTL. No
    # explicit FILES entry needed - same qml/*.qml glob covers it.
    #
    # st-053: RecentTransactions.qml (Dashboard "recent transactions"
    # panel: 5-row ListView bound to transactionTrackerObj.recentTransfers,
    # TxRow delegate, "No transactions yet" empty state, transactionClicked
    # signal). The C++ side (TransactionTracker::kRecentTransfersLimit == 5)
    # caps the slice; the QML side renders up to 5 TxRow delegates with
    # useDerivedAvatarColor=true so adjacent rows differ visually. No
    # explicit FILES entry needed - same qml/*.qml glob covers it.
    #
    # st-054: Dashboard.qml integration of BalanceCard (st-050) +
    # ActionButtons (st-051) + RecentTransactions (st-053) under a
    # TopBar (Dashboard title + sync status pill). The Dashboard root
    # re-emits sendClicked/receiveClicked/subaddressesClicked (from
    # ActionButtons) + transactionClicked (from RecentTransactions).
    # main.qml's inline Dashboard{} Component wires each one into
    # root._navigateTo(<pageName>) which StackView.replace's the right
    # page Component. No explicit FILES entry needed - same qml/*.qml
    # glob covers Dashboard.qml.
    #
    # st-060: SendPage.qml (Send form - From / To / Amount / Priority).
    # Reachable from the Dashboard ActionButtons (st-051) "Send" CTA +
    # the Sidebar (st-041) tab #2 (Send). The page renders a four-field
    # form under a TopBar (page title + back affordance):
    #   - From ComboBox: model = subaddressManagerObj.entries;
    #     displayString = "Subaddress #N - Label" / "Primary" via
    #     SubaddressManager.displayString() (st-019 V5.4.4 #5 B).
    #   - To TextField: placeholder "Enter destination address" + a
    #     "Paste URI" button that routes through
    #     uriHandlerObj.parseUriLocally() (st-020 V5.4.4 #6 A) once a
    #     clipboard context property lands in main.cpp.
    #   - Amount TextField: realtime JDE display; dust < 0.0001 surfaces
    #     a red "Amount too small (dust)" label under the field + blocks
    #     Submit. Conversion to atomic units (JDE * 1000) happens at
    #     submitClicked fire time so the consumer of the signal can feed
    #     the integer straight into TransferRequest::amount.
    #   - Priority ComboBox: Low=0 / Normal=1 / Elevated=2 / High=3
    #     (matches wallet-rpc transfer_split `priority` arg semantics);
    #     default Normal (1).
    #   - Submit button: enabled iff 4 fields filled (subaddresses loaded
    #     AND fromIndex in range AND toAddress non-empty AND amount
    #     positive AND not dust); on click emits
    #     submitClicked(fromSubaddress, toAddress, amountAtomic, priority).
    # V5.4.7 compliance: no key/seed/password/privateKey/payment_id is
    # ever bound as a QML property - only address + amount + priority,
    # all of which flow straight from TextField to the submitClicked
    # signal. The Sidebar Send tab (Send.qml placeholder) is kept
    # around as a stable routing surface; SendPage.qml is the real form
    # the Dashboard's sendClicked signal lands on. No explicit FILES
    # entry needed - same qml/*.qml glob covers SendPage.qml.
    #
    # st-061: ConfirmModal.qml (password-confirm modal for SendPage +
    # future transfer flows). Mounted in the parent (main.qml post-
    # st-062/063 wiring) inside Overlay.overlay with modal: true and a
    # dim backdrop; click-outside / X / Cancel / Esc all emit
    # cancelled() and close the modal. Confirm click runs the local
    # password verifier (walletManagerObj.verifyPassword, the new
    # Q_INVOKABLE on WalletManager that wraps open_wallet + converts
    # the wrong-password RpcException to a false return) and emits
    # confirmClicked(string password) on success. On a wrong password
    # the modal shows a red qsTr("Incorrect password") label and
    # leaves Confirm enabled so the user can retry. V5.4.7 #7C: the
    # password is held only in the modal root's transient `password`
    # property, forwarded verbatim to wallet-rpc, and wiped on close
    # / on success / on reopen -- never logged, never cached. The
    # confirmButton's enable binding (`visible && password.length >=
    # 1`) prevents an empty-password submit. No explicit FILES entry
    # needed - same qml/*.qml glob covers ConfirmModal.qml.
    # st-062: HistoryPage.qml (full transaction history - search box
    # 200ms debounce + 4-tab TabBar All/Incoming/Outgoing/Pending +
    # ListView bound to transactionTrackerObj.transfers) +
    # TxDetailDialog.qml (modal showing per-tx metadata + editable
    # note that round-trips through localDataManagerObj.saveTxNote).
    # Reachable from the Dashboard per-tx click (Dashboard.qml
    # transactionClicked signal -> main.qml _openTxDetail) and from
    # the Sidebar tab #3 (HistoryPage Component in main.qml). The
    # dialog is mounted at app scope in main.qml so it survives
    # StackView transitions. The C++ side adds:
    #   - TransactionTracker::transfers (Q_PROPERTY, full list) +
    #     transfersChanged() NOTIFY signal (mirrors recentTransfers
    #     but uncapped; the 5-row slice is Dashboard-only).
    #   - LocalDataManager::saveTxNote / getTxNote / removeTxNote
    #     (Q_INVOKABLEs that wrap the TxNote facade + write to
    #     tx_notes.json on disk; emitted via txNotesChanged() so any
    #     QML binding refreshes).
    #   - main.cpp exposes localDataManagerObj as a context property
    #     so the dialog can call the Q_INVOKABLEs without a
    #     property-less facade. V5.4.7 #7B: notes are public-chain
    #     tx_hash + user-supplied text only; no key/seed/password
    #     ever crosses this surface. V5.4.9: every visible string
    #     is qsTr() wrapped. No explicit FILES entry needed - same
    #     qml/*.qml glob covers HistoryPage.qml and TxDetailDialog.qml.
    # st-063: AddressBookPage.qml (named addresses page: ListView of
    #     label + address + description, Add/Edit/Delete row buttons,
    #     toolbar Add/Import/Export, delete confirmation Dialog) +
    #     EditAddressDialog.qml (5-field modal: Address / Label /
    #     Description / Payment ID + read-only id; advanced toggle
    #     reveals payment_id per V5.4.4 #6 decision A; Esc / X / Cancel
    #     all emit cancelled(); Save button enabled iff address is
    #     non-empty after trim). The QML side calls into
    #     localDataManagerObj.addressBookEntries / addressBookSize /
    #     addAddressEntry / updateAddressEntry / removeAddressEntry /
    #     importAddressBook / exportAddressBook (the Q_INVOKABLE
    #     bridge in src/core/localdata/LocalDataManager.{h,cpp}, the
    #     st-017 + st-063 contract). Import / Export wire up
    #     QtQuick.Dialogs FileDialog (selectedFile -> strip
    #     file:// prefix -> localDataManagerObj.import|export). V5.4.7
    #     compliance: no key/seed/password/privateKey is ever bound;
    #     the address book is public-chain addresses + user-supplied
    #     labels only. V5.4.9: every visible string is qsTr() wrapped.
    #     No explicit FILES entry needed - same qml/*.qml glob
    #     covers AddressBookPage.qml and EditAddressDialog.qml.
    #
    # st-064: ReceivePage.qml sidebar upgrade. The page root stays an
    # Item (was already an Item in st-024); the change is (a) the top
    # of the page now renders a Forest Green (#16a34a) TopBar with a
    # "<- Back to Dashboard" button that emits backClicked(), and (b)
    # main.qml now StackView.push(ReceivePage.qml) instead of
    # mainStack.replace(...) so the Back button can mainStack.pop()
    # back to the parent page (normally Dashboard, the page whose
    # receiveClicked signal pushed us here). ReceivePage also gains
    # objectName: "ReceivePage" + pageName: "Receive" for static-source
    # contract checks (same pattern AddressBookPage st-063 uses for
    # test_address_book case 12 + 14). All existing st-024 QR / form /
    # SubaddressManager bindings are preserved verbatim so the V5.4.14
    # receive flow continues to work end-to-end. main.qml wires the
    # receivePage Component's onBackClicked to mainStack.pop() so the
    # push/pop pairing is symmetric. V5.4.7 compliance: the new Back
    # button + signal only emit a UI event; no key / seed / password /
    # private-key material is ever bound by ReceivePage. V5.4.9: every
    # visible string is qsTr() wrapped. V5.4.15: single Forest Green
    # theme baked in (no theme switch). V5.4.21: mainnet-only. No
    # explicit FILES entry needed - the same qml/*.qml glob covers
    # ReceivePage.qml.
    #
    # st-065: Subaddresses.qml sidebar upgrade. Same pattern as
    # ReceivePage st-064: (a) the top of the page now renders a Forest
    # Green (#16a34a) TopBar with a "<- Back to Dashboard" button that
    # emits backClicked(), and (b) main.qml now StackView.push(
    # subaddressPage) instead of mainStack.replace(subaddressPage) so
    # the Back button can mainStack.pop() back to the parent page
    # (normally Dashboard, the page whose subaddressesClicked signal
    # pushed us here). Subaddresses also gains objectName:
    # "Subaddresses" + pageName: "Subaddresses" + backClicked() signal
    # for static-source contract checks (same pattern ReceivePage
    # st-064 + AddressBookPage st-063 use). All existing st-024
    # ListView of SubaddressManager.entries / per-row QR button / QR
    # preview Dialog / "Refresh from wallet" button / SubaddressManager
    # bindings are preserved verbatim so the V5.4.14 receive flow
    # continues to work end-to-end. main.qml now wires the
    # subaddressPage Component's onBackClicked to mainStack.pop() so the
    # push/pop pairing is symmetric. The Sidebar's onTabClicked handler
    # also routes Subaddress via push (with a no-op guard against
    # double-push when already on Subaddress) so clicking the Sidebar
    # Subaddress tab yields the same push behavior as Dashboard-driven
    # navigation. _navigateTo("Subaddress") mirrors the same push
    # branch. V5.4.7 compliance: the new Back button + signal only
    # emit a UI event; no key / seed / password / private-key material
    # is ever bound by Subaddresses.qml. V5.4.9: every visible string
    # is qsTr() wrapped. V5.4.15: single Forest Green theme baked in
    # (no theme switch). V5.4.21: mainnet-only. No explicit FILES entry
    # needed - the same qml/*.qml glob covers Subaddresses.qml.
    # st-070: Case1Create.qml (OnboardingWizard Case 1 detail page).
    # Implements the 4-step create-new-wallet wizard (WelcomePage /
    # CreateOrRestorePage / SeedPage / SyncPage) per the st-070 spec.
    # Loaded by OnboardingWizard.qml via qrc:/qml/Case1Create.qml when
    # the user clicks the "Create new wallet" CaseCard (Case 1) on the
    # wizard home grid. The page emits back() to bounce the user back
    # to the wizard home, and walletReady(filename) on successful
    # walletManagerObj.createWallet(...) so main.qml can flip
    # root.walletReady = true and mount the sidebar shell. The 25
    # words are a fixed demo wordlist (first 25 of monero English
    # wordlist); production hardening (round-trip the generated seed
    # through a follow-up RPC) is a future st-070b / st-082 follow-up.
    # V5.4.7 compliance: no key / seed / password / private-key is
    # ever bound as a QML property - the password is a transient
    # property on the page root that is wiped on Submit success, on
    # Back-out, and on Component.onDestruction. V5.4.9: every
    # visible string is qsTr() wrapped. V5.4.15: single Forest Green
    # theme baked in (#16a34a top bar + green step indicators). V5.4.21:
    # mainnet-only. No explicit FILES entry needed - the same
    # qml/*.qml glob covers Case1Create.qml.
    #
    # st-071: Case2Open.qml (OnboardingWizard Case 2 detail page).
    # Implements the single-step open-existing-wallet flow per the
    # st-071 spec: 1 PasswordField (placeholder "Wallet password"
    # with show/hide "show"/"hide" eye toggle that flips
    # TextInput.Password <-> TextInput.Normal on the SAME in-memory
    # password string), filename TextField (placeholder
    # "wallet-name.keys"), Submit button that calls
    # walletManagerObj.openWallet(filename, password) and on success
    # emits walletReady(filename), a red qsTr("Incorrect password")
    # label on any failure (generic -- never leaks RpcException
    # internals), and a "Recover from seed instead" Button that emits
    # switchToRecoverClicked() to bubble up to OnboardingWizard.qml
    # which can then hand the user over to the Case 3 / RecoveryManager
    # flow. Loaded by OnboardingWizard.qml via qrc:/qml/Case2Open.qml
    # when the user clicks the "Open existing wallet" CaseCard (Case
    # 2) on the wizard home grid. The page emits back() to bounce the
    # user back to the wizard home, walletReady(filename) on
    # successful walletManagerObj.openWallet(...) so main.qml can
    # flip root.walletReady = true and mount the sidebar shell, and
    # switchToRecoverClicked() to hand the user over to the
    # recover-from-seed flow (Case 3) without re-prompting for the
    # forgotten password. V5.4.7 compliance: the password lives only
    # as a transient property on the page root that is wiped on
    # Submit success, on Submit failure, on Back-out (top + bottom
    # bar), on switchToRecoverClicked(), on focus loss (Alt-Tab), and
    # on Component.onDestruction. The filename is not a secret so it
    # is preserved across navigations within the page's life. The
    # showPassword flag only flips the TextField echoMode; it does
    # NOT change what is stored in the password property. We never
    # log the password bytes; the only console.log lines emitted are
    # the st-071 markers used by the upcoming real-test harness.
    # V5.4.9: every visible string is qsTr() wrapped. V5.4.15: single
    # Forest Green theme baked in (#16a34a top bar + bottom bar +
    # green primary CTA + #f0fdf4 step indicator). V5.4.21:
    # mainnet-only. No explicit FILES entry needed - the same
    # qml/*.qml glob covers Case2Open.qml.
    #
    # st-072: Case3Recover.qml (OnboardingWizard Case 3 detail page).
    # Implements the single-step recover-from-seed flow per the st-072
    # spec: 1 TextArea (placeholder "word1 word2 word3 word4 word5 ...",
    # monospace, 6 rows), live "N / 25 words" counter label under the
    # TextArea, Submit button that becomes enabled only when (a) the
    # pasted text resolves to exactly 25 whitespace-separated tokens
    # AND (b) each token matches the monero English wordlist shape
    # (3..8 lowercase ASCII letters, matching
    # MnemonicWordList::english() in src/core/wallet/MnemonicWordList.cpp).
    # On submit the page emits walletReady(filename) so main.qml can flip
    # root.walletReady = true and mount the sidebar shell. A red
    # qsTr("Please enter 25 words") label covers the wrong-count path
    # and a red qsTr("Word %1 is not in the wordlist") label covers the
    # first-bad-word path; submit-fail surfaces a generic
    # qsTr("Restore failed") red label (V5.4.7: never leak the
    # underlying RpcException::toString() to the user). A
    # ProgressBar + "Scanning block N / total" status row simulates
    # the wallet-rpc chain-scan progress; when recoveryManagerObj is
    # wired as a context property in a future sub_task the local Timer
    # will be replaced with a real
    # recoveryManagerObj.startRecoveryAsync({filename, password,
    # mnemonic}) call and the ProgressBar.value will bind to
    # recoveryManagerObj.progressCurrent / progressTotal via the
    # progressUpdated signal. For st-072 modify the local Timer
    # simulation is the gate so the page is exercisable end-to-end
    # without a live wallet-rpc. A "Have an existing .keys file? ->
    # Open it instead" Button emits switchToOpenClicked() to bubble up
    # to OnboardingWizard.qml so the user can swap to Case2Open.qml
    # without re-typing their seed. Loaded by a future
    # OnboardingWizard.qml Loader via qrc:/qml/Case3Recover.qml when
    # the user clicks the "Recover from seed" CaseCard (Case 3) on
    # the wizard home grid. The page emits back() to bounce the user
    # back to the wizard home, walletReady(filename) on successful
    # scan completion so main.qml can flip root.walletReady = true and
    # mount the sidebar shell, and switchToOpenClicked() to hand the
    # user over to the open-existing-wallet flow (Case 2).
    # V5.4.7 compliance: the seed string lives only as a transient
    # property on the page root (page.seedText) that is wiped on
    # Submit success (right before walletReady emission), on Submit
    # failure, on Back-out (top + bottom bar), on switchToOpenClicked(),
    # and on Component.onDestruction. We never log the seed bytes; the
    # only console.log lines emitted are the st-072 markers used by the
    # upcoming real-test harness ("submit", "scan complete",
    # "walletReady", "back -> wizard home"). V5.4.9: every visible
    # string is qsTr() wrapped. V5.4.15: single Forest Green theme
    # baked in (#16a34a top bar + green primary CTA + #f0fdf4 step
    # indicator). V5.4.21: mainnet-only. No explicit FILES entry
    # needed - the same qml/*.qml glob covers Case3Recover.qml.
    #
    # st-080: Settings.qml 8 categories (Network full + 7 placeholder UI).
    # Settings.qml renders an 8-tab picker (Network / Language / Auto-Lock /
    # Transfer Confirm / Appearance / Advanced / Reset / About). Each tab
    # routes via a single Loader to its dedicated sub-page:
    #   - NetworkSettings.qml    : st-023 full remote-node picker + custom
    #                              host:port (uses settingsManagerObj +
    #                              remoteNodeObj context properties).
    #   - LanguageSettings.qml   : ComboBox of the 9 supported V1.0
    #                              locales (en / zh_TW / de / ja / ko / it /
    #                              nl / ru / es) plus "Follow system (auto)"
    #                              sentinel (V5.4.4 A). Two-way bind via
    #                              settingsManager.languageLocale.
    #   - AutoLockSettings.qml   : ComboBox of the 5 locked tiers (5 / 15 /
    #                              30 / 60 minutes + Never) per
    #                              SettingsSchema::allowedAutoLockSeconds().
    #   - TransferConfirmSettings.qml : two independent Switches bound to
    #                              settingsManager.transferConfirmDialog /
    #                              transferConfirmPassword (V5.4.4 C).
    #   - AppearanceSettings.qml : read-only summary (locked Forest Green
    #                              theme, Fusion style, accent #22c55e).
    #                              V5.4.15: no theme switcher in V1.0.
    #   - AdvancedSettings.qml   : two collapsible GroupBoxes (Daemon
    #                              endpoint + Log level) showing what the
    #                              wallet is talking to. Read-only in V1.0.
    #   - ResetSettings.qml      : red "Reset wallet data" Button + a
    #                              single Dialog with Cancel / "Yes, reset"
    #                              pair. On confirm calls
    #                              settingsManager.reset() AND emits
    #                              resetRequested() so the page router can
    #                              drop in-memory caches.
    #   - AboutSettings.qml      : 2x4 GridLayout of version labels
    #                              (wallet / Qt / daemon) + mainnet label.
    # All 8 sub-pages are picked up by the qml/*.qml glob above; the design
    # contract is the Settings.qml _subPageFor(category) switch which maps
    # the 8 category ids to the 8 qrc:/qml/<Name>.qml URLs.
    # V5.4.7 compliance: no sub-page stores key / seed / password in a
    # QML property. V5.4.9 compliance: every visible string is qsTr()
    # wrapped. V5.4.15: AppearanceSettings is read-only. V5.4.21: no
    # regtest / testnet / stagenet UI (only "Mainnet" labels).
    #
    qt_add_resources(judecoin-wallet-ui "qml_files"
        PREFIX "/qml"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
        FILES ${JUDECOIN_QML_SOURCES}
    )
    message(STATUS "qml_files registered: ${JUDECOIN_QML_SOURCES}")

    # st-120 (2026-07-16): register docs/judecoin-logo.png as a QRC
    # resource so the Onboarding header (and any future top-level
    # brand surface) can render the Judecoin logo via
    # Image { source: "qrc:/judecoin-logo.png" }.
    # PREFIX "/" + BASE docs/ keeps the brand asset addressable
    # without colliding with the qml/ namespace. The Onboarding
    # headerBar Image element is the first consumer; the main.qml
    # sidebar logo will pick this up in a follow-up.
    #
    # st-160 (2026-07-18): register docs/homepage-bg.png as a
    # QRC resource so the OnboardingWizard homepage (st-160 redesign)
    # can render the custom background image via
    # Image { source: "qrc:/homepage-bg.png" }. Same PREFIX "/" +
    # BASE docs/ scheme as the logo so the address space stays
    # flat under qrc:/.
    qt_add_resources(judecoin-wallet-ui "brand_assets"
        PREFIX "/"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/../docs"
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/../docs/judecoin-logo.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/../docs/homepage-bg.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/../docs/banner-bg.png"
    )
    message(STATUS "brand_assets registered: qrc:/judecoin-logo.png (st-120), "
                   "qrc:/homepage-bg.png (st-160), "
                   "qrc:/banner-bg.png (st-161)")
endif()

# -----------------------------------------------------------------------------
# 5. Summary banner
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "  src/ target summary:")
message(STATUS "    library:   libjudecoin-wallet-core.a (${CORE_SOURCES_COUNT} sources)")
message(STATUS "    executable: judecoin-wallet-ui")
message(STATUS "")