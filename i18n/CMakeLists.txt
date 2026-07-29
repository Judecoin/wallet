# =============================================================================
# Judecoin Wallet - i18n/ CMakeLists
# =============================================================================
#
# Implements st-016 (V5.4.9 / V5.4.12): 9 languages (en, zh_TW, de, ja, ko,
# it, nl, ru, es). Uses Qt Linguist via Qt6::LinguistTools.
#
# Workflow:
#   1. Developer edits C++ / QML strings; runs `cmake --build . --target
#      lupdate` (or `make lupdate`) to refresh the .ts files.
#   2. Translators fill the .ts files (or V1.0 ships with draft translations
#      for de/ja/ko/it/nl/ru/es per V5.4.9).
#   3. `cmake --build .` (or `make`) compiles each .ts into a .qm via
#      qt_add_lrelease and embeds it under the :/i18n Qt resource prefix.
#   4. At runtime, I18nManager (src/core/i18n/I18nManager.{h,cpp}) loads the
#      matching .qm based on SettingsManager.language (or system locale).
#
# Source-of-truth: docs/UI_DETAILS_TODO.md V5.4.9 + V5.4.11, plus
# docs/DECISIONS.md D011 (synchronized to 9 languages by V5.4.12).
# =============================================================================

find_package(Qt6 6.4 REQUIRED COMPONENTS LinguistTools)

# -----------------------------------------------------------------------------
# Translation source files (.ts) and their compiled outputs (.qm)
# -----------------------------------------------------------------------------
# V5.4.9 9 languages: en, zh_TW, de, ja, ko, it, nl, ru, es.
# zh_CN was explicitly removed.
# en + zh_TW are V1.0 complete translations; the other 7 ship as V1.0
# draft translations and get human review in V1.1.
# -----------------------------------------------------------------------------

set(JUDECOIN_I18N_TS_FILES
    # Paths are relative to the project root (where the top-level
    # CMakeLists.txt lives), so that qt_add_translations can find them
    # regardless of which directory add_subdirectory(i18n) is invoked from.
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_en.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_zh_TW.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_de.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_ja.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_ko.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_it.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_nl.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_ru.ts
    ${CMAKE_SOURCE_DIR}/i18n/judecoin_wallet_es.ts
)

# -----------------------------------------------------------------------------
# Create the i18n static library target first.
# -----------------------------------------------------------------------------
# qt_add_lrelease / qt_add_translations attach themselves to an *existing*
# target (they expect to call add_dependencies(<target> ...) on it). We
# create a thin static lib here that:
#   - Holds the .qm files compiled by lrelease
#   - Embeds them under :/i18n (RESOURCE_PREFIX "/i18n")
# The main executable links this lib, so the .qm data ends up baked in.
# -----------------------------------------------------------------------------

add_library(judecoin_wallet_i18n STATIC)

# qt_add_translations (Qt 6.4+) is the higher-level helper that wraps
# lupdate + lrelease + qt_add_resources into one call. It:
#   - generates the .qm files from the .ts files
#   - attaches them as resources at :/i18n/judecoin_wallet_<code>.qm
#   - registers them with the existing target's dependency graph
qt_add_translations(judecoin_wallet_i18n
    TS_FILES ${JUDECOIN_I18N_TS_FILES}
    RESOURCE_PREFIX "/i18n"
)

target_link_libraries(judecoin_wallet_i18n
    PUBLIC
        Qt6::Core
)

target_include_directories(judecoin_wallet_i18n
    PUBLIC
        # Public so consumers can see the embedded .qm paths.
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(judecoin_wallet_i18n PROPERTIES
    OUTPUT_NAME              "judecoin_wallet_i18n"
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC                  ON
)

# -----------------------------------------------------------------------------
# lupdate workflow (manual, run outside CMake)
# -----------------------------------------------------------------------------
# Developers regenerate the .ts files with:
#
#   /usr/lib/qt6/bin/lupdate \
#       $(find ../src -name '*.cpp' -o -name '*.h' -o -name '*.qml') \
#       -ts i18n/judecoin_wallet_en.ts \
#          i18n/judecoin_wallet_zh_TW.ts \
#          i18n/judecoin_wallet_de.ts \
#          ...
#
# This is intentionally not wired into CMake: qt_add_lupdate requires an
# existing target to attach to, and adding a stub target here would
# clutter the build graph for a tool that runs only when adding new UI
# strings. The .ts files in this directory are the source of truth;
# only re-run lupdate after editing UI strings in src/.
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Link into the main executable
# -----------------------------------------------------------------------------
# When included from the top-level CMakeLists.txt (the production path),
# judecoin-wallet-ui exists and the i18n static lib gets linked. If
# this CMakeLists.txt is reused standalone (e.g. for translator tooling),
# the if(TARGET) guard keeps it benign.
# -----------------------------------------------------------------------------
if(TARGET judecoin-wallet-ui)
    target_link_libraries(judecoin-wallet-ui
        PRIVATE
            judecoin_wallet_i18n
    )
    message(STATUS "  i18n: linked judecoin_wallet_i18n -> judecoin-wallet-ui")
endif()

# -----------------------------------------------------------------------------
# Summary banner
# -----------------------------------------------------------------------------
list(LENGTH JUDECOIN_I18N_TS_FILES JUDECOIN_I18N_LANG_COUNT)
message(STATUS "")
message(STATUS "  i18n target summary:")
message(STATUS "    languages:    ${JUDECOIN_I18N_LANG_COUNT} (V5.4.9)")
message(STATUS "    .ts sources:  ${JUDECOIN_I18N_TS_FILES}")
message(STATUS "    resource:     :/i18n/judecoin_wallet_<code>.qm")
message(STATUS "")
