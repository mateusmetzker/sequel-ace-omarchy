# Sequel Ace for Linux / Omarchy
#
# A Qt 6 port of the Sequel Ace MySQL/MariaDB client. The original application
# is macOS-only (AppKit/SwiftUI) and cannot be compiled on Linux; this tree
# re-implements it on top of Qt Widgets and libmariadb, reusing the portable
# pieces of the original (SQL editor lexer, themes, content-filter
# definitions, favorites file format).
#
# Licensed under the MIT license. See LICENSE.

cmake_minimum_required(VERSION 3.21)

project(SequelAce
    VERSION 0.1.0
    DESCRIPTION "Sequel Ace - MySQL and MariaDB database management for Linux"
    HOMEPAGE_URL "https://sequel-ace.com"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(SA_USE_LIBSECRET "Store passwords in the desktop keyring through libsecret (Secret Service API)" ON)
option(SA_BUILD_TESTS "Build the unit and integration tests" ON)
option(SA_BUILD_APP "Build the GUI application (off = core library and tests only)" ON)

include(GNUInstallDirs)

set(SA_QT_COMPONENTS Core Gui Network Concurrent)
if(SA_BUILD_APP)
    list(APPEND SA_QT_COMPONENTS Widgets Svg)
endif()
if(SA_BUILD_TESTS)
    list(APPEND SA_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${SA_QT_COMPONENTS})
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
pkg_check_modules(MARIADB IMPORTED_TARGET libmariadb)
if(NOT MARIADB_FOUND)
    # Debian/Ubuntu ship the same connector under this name on older releases.
    pkg_check_modules(MARIADB REQUIRED IMPORTED_TARGET mariadb)
endif()

set(SA_HAVE_LIBSECRET OFF)
if(SA_USE_LIBSECRET)
    pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
    if(LIBSECRET_FOUND)
        set(SA_HAVE_LIBSECRET ON)
    else()
        message(WARNING "libsecret-1 not found: passwords will only be kept in memory for the session")
    endif()
endif()

# --- SQL editor lexer (taken from the macOS app) ------------------------------
# The .l file is the source of truth. flex regenerates it when available;
# otherwise the committed generated file is used so packagers do not need flex.
set(SA_LEXER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/lexers/generated/SPEditorTokens.c")
find_package(FLEX)
if(FLEX_FOUND)
    flex_target(SPEditorTokens
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lexers/SPEditorTokens.l"
        "${CMAKE_CURRENT_BINARY_DIR}/SPEditorTokens.c")
    set(SA_LEXER_SOURCE ${FLEX_SPEditorTokens_OUTPUTS})
endif()

# --- Core library: everything that does not depend on Qt Widgets -------------
add_library(sequelace_core STATIC
    ${SA_LEXER_SOURCE}
    src/lexers/SPParserUtils.c
    src/core/SASQLTypes.cpp
    src/core/SAOmarchyTheme.cpp
    src/core/SAResult.cpp
    src/core/SAResultExport.cpp
    src/core/SAMySQLConnection.cpp
    src/core/SASQLClassifier.cpp
    src/core/SASQLSplitter.cpp
    src/core/SASQLTokens.cpp
    src/core/SAPlist.cpp
    src/core/SAConnectionInfo.cpp
    src/core/SAFavoritesStore.cpp
    src/core/SAPreferences.cpp
    src/core/SADatabaseSession.cpp
    src/core/SASchemaQueries.cpp
    src/core/SAUserManagerQueries.cpp
    src/core/SAContentEditing.cpp
    src/core/SAContentFilters.cpp
    src/core/SAFilterTree.cpp
    src/core/SAEditorTheme.cpp
    src/core/SASecretStore.cpp
    src/core/SASSHTunnel.cpp
    src/core/SAQueryHistory.cpp
    src/core/SAMCPToolDefinitions.cpp
    src/core/SAMCPReadOnlyGuard.cpp
    src/core/SAMCPServer.cpp
)
set_source_files_properties(${SA_LEXER_SOURCE} PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(sequelace_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lexers")
target_link_libraries(sequelace_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent
    PkgConfig::MARIADB)
target_compile_options(sequelace_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter>)
if(SA_HAVE_LIBSECRET)
    target_link_libraries(sequelace_core PUBLIC PkgConfig::LIBSECRET)
    target_compile_definitions(sequelace_core PUBLIC SA_HAVE_LIBSECRET=1)
endif()
target_compile_definitions(sequelace_core PUBLIC SA_VERSION_STRING="${PROJECT_VERSION}")
target_compile_definitions(sequelace_core PRIVATE QT_NO_KEYWORDS)

# Resources shared by the application and the tests (themes, filters, tokens).
qt_add_resources(sequelace_core "sequelace_assets"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        resources/ContentFilters.plist
        resources/icons/add.svg
        resources/icons/remove.svg
        resources/icons/refresh.svg
        resources/icons/edit.svg
        resources/icons/gear.svg
        resources/icons/key.svg
        resources/icons/chevron-left.svg
        resources/icons/chevron-right.svg
        resources/icons/filter.svg
        resources/icons/duplicate.svg
        resources/icons/add-folder.svg
        resources/icons/select-all.svg
        resources/icons/select-none.svg
        resources/icons/quick-connect-16.png
        resources/icons/quick-connect-24.png
        resources/icons/quick-connect-32.png
        resources/icons/quick-connect-48.png
        resources/CompletionTokens.json
        resources/ssh_config
        "resources/themes/Blue Dark.spTheme"
        "resources/themes/Dark Ace.spTheme"
        "resources/themes/Monokai.spTheme"
        "resources/themes/Nemo.spTheme"
        "resources/themes/Nord.spTheme"
        "resources/themes/Pixely Night.spTheme"
        "resources/themes/The Pixel.spTheme"
        packaging/icons/sequel-ace-16.png
        packaging/icons/sequel-ace-32.png
        packaging/icons/sequel-ace-128.png
        packaging/icons/sequel-ace-256.png
        packaging/icons/sequel-ace-512.png
)

# --- Application -------------------------------------------------------------
if(SA_BUILD_APP)
    add_library(sequelace_ui STATIC
        src/ui/SAMainWindow.cpp
        src/ui/SAIcons.cpp
        src/ui/SAConnectionView.cpp
        src/ui/SAFavoritesModel.cpp
        src/ui/SADatabaseDocument.cpp
        src/ui/SATablesList.cpp
        src/ui/SAResultModel.cpp
        src/ui/SAResultTableView.cpp
        src/ui/SATableContentView.cpp
        src/ui/SATableStructureView.cpp
        src/ui/SATableRelationsView.cpp
        src/ui/SATableTriggersView.cpp
        src/ui/SATableInfoView.cpp
        src/ui/SACustomQueryView.cpp
        src/ui/SASQLEditor.cpp
        src/ui/SASQLHighlighter.cpp
        src/ui/SAConsoleWindow.cpp
        src/ui/SAFieldEditorDialog.cpp
        src/ui/SAPreferencesDialog.cpp
        src/ui/SAServerDialogs.cpp
        src/ui/SAUserManagerDialog.cpp
        src/ui/SAFilterRuleWidget.cpp
        src/ui/SAExportDialog.cpp
        src/ui/SADialogs.cpp
    )
    target_link_libraries(sequelace_ui PUBLIC sequelace_core Qt6::Widgets Qt6::Svg)
    target_include_directories(sequelace_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/ui")
    target_compile_options(sequelace_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)

    qt_add_executable(sequel-ace src/main.cpp)
    target_link_libraries(sequel-ace PRIVATE sequelace_ui)
    target_compile_options(sequel-ace PRIVATE -Wall -Wextra -Wno-unused-parameter)

    install(TARGETS sequel-ace RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/com.sequel-ace.SequelAce.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/com.sequel-ace.SequelAce.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 16 32 128 256 512)
        install(FILES packaging/icons/sequel-ace-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME com.sequel-ace.SequelAce.png)
    endforeach()
endif()

# --- Tests -------------------------------------------------------------------
if(SA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Sequel Ace ${PROJECT_VERSION}: Qt ${Qt6_VERSION}, libmariadb ${MARIADB_VERSION}, libsecret: ${SA_HAVE_LIBSECRET}, flex: ${FLEX_FOUND}, app: ${SA_BUILD_APP}")
