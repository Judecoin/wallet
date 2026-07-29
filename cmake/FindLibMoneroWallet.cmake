# =============================================================================
# FindLibMoneroWallet.cmake - Locate libmonero-wallet (STUB for V1.0)
# =============================================================================
#
# V5.4.7 (UI never reads key files): the Judecoin Wallet UI does NOT link
# against libmonero-wallet. The wallet-rpc binary is the sole encryptor of
# key material; the UI talks to it exclusively over loopback JSON-RPC.
#
# This Find module is provided for forward-compatibility: if a future V1.1+
# feature needs to perform per-key operations from the UI side (e.g. local
# address derivation for an offline air-gapped flow), it would link against
# libmonero-wallet at that point. For V1.0 we define IMPORTED targets only
# when the library is found, and always set JUDECOIN_WALLET_HAS_LIBMONERO=OFF
# on V1.0 builds so callers can guard their code paths.
#
# Result variables (always set, even when not found):
#   JUDECOIN_WALLET_HAS_LIBMONERO    - TRUE if found, FALSE otherwise
#   LibMoneroWallet_FOUND            - standard CMake find result
#   LibMoneroWallet_VERSION          - library version if found
#   LibMoneroWallet_INCLUDE_DIRS     - include directories
#   LibMoneroWallet_LIBRARIES        - libraries to link
#
# Hints (env vars / cache vars):
#   LIBMONERO_WALLET_ROOT  - root directory of libmonero-wallet install
#   LIBMONERO_WALLET_INCLUDE_DIR
#   LIBMONERO_WALLET_LIBRARY
# =============================================================================

# 1. Honor explicit hint from user / env
set(LibMoneroWallet_ROOT_HINT "${LIBMONERO_WALLET_ROOT}" CACHE PATH
    "Root directory of libmonero-wallet (V5.4.7: not used by V1.0 UI)")

if(LibMoneroWallet_ROOT_HINT)
    set(_libmw_search_paths
        "${LibMoneroWallet_ROOT_HINT}"
        "${LibMoneroWallet_ROOT_HINT}/lib"
        "${LibMoneroWallet_ROOT_HINT}/include")
else()
    set(_libmw_search_paths
        "/usr/local"
        "/usr"
        "/opt/libmonero-wallet")
endif()

# 2. Look for the header
find_path(LibMoneroWallet_INCLUDE_DIR
    NAMES
        wallet_api.h
        monero_wallet.h
    HINTS
        ${_libmw_search_paths}
    PATH_SUFFIXES
        include
        include/monero
        include/libmonero
    DOC "libmonero-wallet include directory"
)

# 3. Look for the library
find_library(LibMoneroWallet_LIBRARY
    NAMES
        monero-wallet
        libmonero-wallet
        monero_wallet
    HINTS
        ${_libmw_search_paths}
    PATH_SUFFIXES
        lib
        lib/x86_64-linux-gnu
        lib64
    DOC "libmonero-wallet library"
)

# 4. Standard find_package conventions
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibMoneroWallet
    REQUIRED_VARS
        LibMoneroWallet_LIBRARY
        LibMoneroWallet_INCLUDE_DIR
    VERSION_VAR LibMoneroWallet_VERSION
)

# 5. Set convenience variables
if(LibMoneroWallet_FOUND)
    set(JUDECOIN_WALLET_HAS_LIBMONERO TRUE
        CACHE BOOL "Whether libmonero-wallet is available" FORCE)
    set(LibMoneroWallet_INCLUDE_DIRS "${LibMoneroWallet_INCLUDE_DIR}")
    set(LibMoneroWallet_LIBRARIES "${LibMoneroWallet_LIBRARY}")

    # If the caller wants an imported target (for target_link_libraries usage)
    if(NOT TARGET LibMoneroWallet::LibMoneroWallet AND
       NOT TARGET libmonero-wallet AND
       NOT TARGET monero-wallet)
        add_library(LibMoneroWallet::LibMoneroWallet UNKNOWN IMPORTED)
        set_target_properties(LibMoneroWallet::LibMoneroWallet PROPERTIES
            IMPORTED_LOCATION "${LibMoneroWallet_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LibMoneroWallet_INCLUDE_DIR}")
    endif()

    message(STATUS "Found libmonero-wallet: ${LibMoneroWallet_LIBRARY} "
                   "(version ${LibMoneroWallet_VERSION})")
else()
    # V1.0: this is the expected outcome. We do NOT error out, but we
    # record the fact so guard macros like `#if JUDECOIN_WALLET_HAS_LIBMONERO`
    # can disable local key-handling code paths cleanly.
    set(JUDECOIN_WALLET_HAS_LIBMONERO FALSE
        CACHE BOOL "Whether libmonero-wallet is available" FORCE)
    set(LibMoneroWallet_INCLUDE_DIRS "")
    set(LibMoneroWallet_LIBRARIES    "")

    if(LibMoneroWallet_FIND_REQUIRED)
        # Only error if the caller explicitly asked for REQUIRED
        message(FATAL_ERROR
            "libmonero-wallet was REQUIRED but not found. "
            "Set LIBMONERO_WALLET_ROOT or pass "
            "-DLibMoneroWallet_DIR=/path/to/libmonero-wallet.\n"
            "Note: V1.0 does NOT link libmonero-wallet (see V5.4.7). "
            "If you are building V1.0 UI, do not request REQUIRED.")
    else()
        message(STATUS "libmonero-wallet NOT found - using wallet-rpc over JSON-RPC (V5.4.7)")
    endif()
endif()

# 6. Mark these as advanced so they don't clutter cmake -L output
mark_as_advanced(
    LibMoneroWallet_INCLUDE_DIR
    LibMoneroWallet_LIBRARY
    LibMoneroWallet_ROOT_HINT
)