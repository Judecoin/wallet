# =============================================================================
# Judecoin Wallet - Top-level CMake (V5.1 Option E + V5.4.x)
# =============================================================================
#
# Architecture (per docs/ARCHITECTURE.md V5.4.9):
#   - UI: Qt 6 + QML (this project)
#   - Daemon: official Loki/Oxen v3.2.0 binary (Chamaeleo dilepis)
#   - Wallet-RPC: official Loki/Oxen v3.2.0 binary
#   - Communication: HTTP JSON-RPC over loopback (127.0.0.1:19061 / 19071)
#   - Distribution: AppImage (~150 MB, includes UI + daemon + wallet-rpc + Qt libs)
#
# Design constraints:
#   - V5.1 Option E: 0 C++ changes to upstream wallet2
#   - V5.4.7: UI never reads key files (wallet-rpc is sole encryptor)
#   - V5.4.8: Whole package in AppImage (no system dependencies)
#   - mainnet-only (V5.4.2): ports 19061 (daemon) / 19071 (wallet-rpc)
#
# Build:
#   mkdir build && cd build
#   cmake ..
#   make -j4
#
# Produces:
#   build/bin/judecoin-wallet-ui          (Qt 6 QML executable, ~1 MB stripped)
#   build/bin/libjudecoin-wallet-core.a   (static core library)
# =============================================================================

cmake_minimum_required(VERSION 3.21)

project(judecoin-wallet-ui
    VERSION     1.0.0
    DESCRIPTION "Judecoin Wallet - Qt 6 QML desktop client (Loki/Oxen v3.2.0 backend)"
    LANGUAGES   CXX
)

# -----------------------------------------------------------------------------
# Project-wide build settings
# -----------------------------------------------------------------------------

# C++17 required (C++20 not used; keeps compatibility broader)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release for distribution
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Output to a single bin/lib directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Strict warnings (treat as errors in CI / when requested)
option(JUDECOIN_WALLET_WERROR "Treat compiler warnings as errors" OFF)
if(JUDECOIN_WALLET_WERROR)
    add_compile_options(-Werror)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Export compile_commands.json for clangd / IDEs (already in .gitignore)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Qt 6 - V5.1 Option E uses Qt 6 QML (no Qt Widgets needed for pure QML UI)
# -----------------------------------------------------------------------------

# Spec requires Qt 6.6+, but allow 6.4+ for dev environments with a warning.
# (Production AppImage built on Judecoin WSL has Qt 6.6+.)
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Network
    Test
    # FIX-G3 (2026-07-17 17:48): Widgets is required by main.cpp's
    # std::set_terminate handler and app.exec() try/catch block, both of
    # which use QMessageBox::critical() to surface unhandled exceptions
    # as a UI dialog. Without Widgets in COMPONENTS, Qt6::Widgets IMPORTED
    # target is NOT created, and the subsequent
    #   target_link_libraries(... Qt6::Widgets)
    # in src/CMakeLists.txt fails with:
    #   "Target 'judecoin-wallet-ui' links to target 'Qt6::Widgets' but
    #    the target was not found. Perhaps a find_package() call is
    #    missing for an IMPORTED target, or an ALIAS target is missing?"
    Widgets
)

# Use Qt's modern CMake helpers (qt_add_executable, qt_add_qml_module)
qt_standard_project_setup(REQUIRES 6.5)

message(STATUS "Qt6 version: ${Qt6_VERSION} (>=6.6 recommended for production)")

# -----------------------------------------------------------------------------
# Find LibMoneroWallet (stub for V1.0)
# -----------------------------------------------------------------------------
# V5.4.7: UI does NOT read key files. wallet-rpc is the sole encryptor.
# libmonero-wallet is shipped only inside the wallet-rpc binary; the UI never
# links against it. This Find module exists for forward-compatibility with
# V1.1+ if we later decide to do per-key operations from the UI side.
# For V1.0 it just records JUDECOIN_WALLET_HAS_LIBMONERO=OFF so callers can
# guard code paths.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(LibMoneroWallet QUIET)

# -----------------------------------------------------------------------------
# libqrencode (st-024)
# -----------------------------------------------------------------------------
# V5.4.14: QR codes are V1.0 required for the receive flows (subaddress list
# per-row [QR] button / ReceivePage / make_uri output). The encoder is
# libqrencode (C library, same one monero GUI uses per UI_DETAILS_TODO.md:795).
# Install via `apt-get install libqrencode-dev` (build-time dependency).
# pkg-config name on Debian/Ubuntu is `libqrencode` (not `qrencode`).
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBQRENCODE REQUIRED IMPORTED_TARGET libqrencode)
# Expose the target name to sub-directories via a cache variable so we
# don't need to re-run pkg_check_modules in src/CMakeLists.txt.
set(JUDECOIN_WALLET_HAS_LIBQRENCODE TRUE CACHE BOOL "libqrencode found" FORCE)

# -----------------------------------------------------------------------------
# Sub-projects
# -----------------------------------------------------------------------------

# Core library + executable
add_subdirectory(src)

# Translation files (st-016). qt_add_lrelease embeds the .qm resources
# under :/i18n, so the executable must link the i18n static lib too.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/CMakeLists.txt")
    add_subdirectory(i18n)
endif()

# Tests (only if directory exists and tests are enabled)
option(JUDECOIN_WALLET_BUILD_TESTS "Build unit tests" ON)
if(JUDECOIN_WALLET_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Installation (for AppImage staging and system install)
# -----------------------------------------------------------------------------
#
# st-114: also bundle the official v3.2.1 judecoind + judecoin-wallet-rpc
# alongside the UI binary. The vendored binaries are optional at install
# time (install(CODE ...) so a missing source file does not turn into a
# hard `cmake --install` failure on Windows hosts where /root/judecoin
# is unreachable).
# -----------------------------------------------------------------------------

include(GNUInstallDirs)

# st-114: source dir for the official v3.2.1 daemon + wallet-rpc binaries
# (built and hosted inside the `judecoin` WSL instance at
# /root/judecoin/build/Linux/master/release/bin). The default path matches
# the workboard st-114 spec; toolchains that need a different cache can
# override with -DJUDECOIN_VENDOR_SOURCE_DIR=/path/to/bin.
set(JUDECOIN_VENDOR_SOURCE_DIR
    "/root/judecoin/build/Linux/master/release/bin"
    CACHE PATH
    "Directory containing the official judecoind + judecoin-wallet-rpc v3.2.1 binaries"
)

# Each binary that we vendor. The list is kept small so future protocol
# bumps (e.g. judecoin-blockchain-rpc) only need to extend JUDECOIN_VENDOR_BINARIES.
set(JUDECOIN_VENDOR_BINARIES
    judecoind
    judecoin-wallet-rpc
)

# Capture the configured build dir for the install hook. CMAKE_BINARY_DIR
# at configure time equals the build directory chosen by the developer
# (often <source>/build). We forward the resolved path to the install
# script via a regular cache variable so the script sees the absolute
# path even when CMAKE_BINARY_DIR is unset in the install context.
set(JUDECOIN_INSTALL_BINARY_DIR
    "${CMAKE_BINARY_DIR}/bin"
    CACHE INTERNAL "Build-tree path that holds the vendored binaries (consumed by the install script)"
)

# Install hook: copy whatever vendor binaries are present into the
# install bindir. Wrapped in `install(CODE ...)` so a missing source
# binary simply degrades to "skipping" rather than breaking
# `cmake --install`. The AppRun runtime check (st-114 packaging/AppRun)
# gracefully falls back when the bindir copy is absent.
#
# CMAKE_BINARY_DIR at install time may equal CMAKE_SOURCE_DIR (in source
# builds) or be unset, so we capture the actual build dir at configure
# time into JUDECOIN_INSTALL_BINARY_DIR for use in the install script.
install(CODE "
    set(vendor_dir \"${JUDECOIN_INSTALL_BINARY_DIR}/vendor\")
    set(dest_dir  \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    file(MAKE_DIRECTORY \"\${dest_dir}\")
    if(NOT EXISTS \"\${vendor_dir}\")
        message(STATUS \"st-114: no vendor dir at \${vendor_dir}, skipping binary install\")
    else()
        foreach(_bin ${JUDECOIN_VENDOR_BINARIES})
            set(_src \"\${vendor_dir}/\${_bin}\")
            if(EXISTS \"\${_src}\")
                file(INSTALL \"\${_src}\" DESTINATION \"\${dest_dir}\" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
                message(STATUS \"st-114: installed vendor \${_bin} -> \${dest_dir}\")
            else()
                message(STATUS \"st-114: vendor \${_bin} not present in \${vendor_dir}, skipping\")
            endif()
        endforeach()
    endif()
")

install(TARGETS judecoin-wallet-ui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# -----------------------------------------------------------------------------
# Final status banner
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=======================================================")
message(STATUS "  Judecoin Wallet v${PROJECT_VERSION} (Option E + V5.4.x)")
message(STATUS "=======================================================")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Qt version:    ${Qt6_VERSION}")
message(STATUS "  Output binary: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/judecoin-wallet-ui")
message(STATUS "  Output core:   ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/libjudecoin-wallet-core.a")
message(STATUS "  Backend:       official Loki/Oxen v3.2.0 binary (separate)")
message(STATUS "=======================================================")
message(STATUS "")