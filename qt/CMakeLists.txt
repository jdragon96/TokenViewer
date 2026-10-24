cmake_minimum_required(VERSION 3.22)
project(TokenViewer VERSION 0.1.0 LANGUAGES CXX)
if(WIN32)
    # windres compiles the icon and version info into the exe.
    enable_language(RC)
endif()

# Release builds pass the tag (package-windows.ps1); empty means the project version.
set(TV_VERSION "" CACHE STRING "App version for the app and the exe's version info (empty: project version)")
if(TV_VERSION STREQUAL "")
    set(TV_APP_VERSION "${PROJECT_VERSION}")
else()
    string(REGEX REPLACE "^v" "" TV_APP_VERSION "${TV_VERSION}")
endif()
# VERSIONINFO holds four numbers; a suffix such as -beta1 only shows in the version strings.
set(TV_VERSION_RC "0,0,0,0")
if(TV_APP_VERSION MATCHES "^([0-9]+)(\\.([0-9]+))?(\\.([0-9]+))?")
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_3}")
    set(_patch "${CMAKE_MATCH_5}")
    foreach(_part _minor _patch)
        if("${${_part}}" STREQUAL "")
            set(${_part} 0)
        endif()
    endforeach()
    set(TV_VERSION_RC "${_major},${_minor},${_patch},0")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network Test)

# LGPLv3: Qt must stay a shared library.
foreach(_module Core Gui Widgets Network)
    get_target_property(_type Qt5::${_module} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Qt5::${_module} must be linked as a shared library (LGPLv3)")
    endif()
endforeach()

# Tests run against Qt's DLLs without the caller having Qt on PATH.
get_target_property(_qmake_location Qt5::qmake IMPORTED_LOCATION)
get_filename_component(TV_QT_BIN_DIR "${_qmake_location}" DIRECTORY)
file(TO_NATIVE_PATH "${TV_QT_BIN_DIR}" TV_QT_BIN_DIR_NATIVE)

set(TV_CORE_SOURCES
    src/UI/TrayIconPainter.cpp
    src/UI/Formatters.cpp
    src/UI/LimitMeterWidget.h
    src/UI/LimitMeterWidget.cpp
    src/UI/BreakdownListWidget.h
    src/UI/BreakdownListWidget.cpp
    src/UI/UsagePopup.h
    src/UI/UsagePopup.cpp
    src/UI/TrayIcon.h
    src/UI/TrayIcon.cpp
    src/UI/WindowsTheme.h
    src/UI/WindowsTheme.cpp
    src/UI/ChevronComboBox.h
    src/UI/ChevronComboBox.cpp
    src/Analysis/PriceTable.cpp
    src/Analysis/LogParser.cpp
    src/Analysis/TokenAggregator.cpp
    src/Analysis/LogStore.cpp
    src/Service/ClaudePaths.cpp
    src/Service/CredentialStore.cpp
    src/Service/UsageApiClient.cpp
    src/Service/WinHttpClient.cpp
    src/Service/RefreshPolicy.cpp
    src/Service/LoginItem.cpp
    src/UI/SettingsDialog.h
    src/UI/SettingsDialog.cpp
    src/UI/LicenseNoticesDialog.h
    src/UI/LicenseNoticesDialog.cpp
    src/Thread/UsageFetchThread.h
    src/Thread/UsageFetchThread.cpp
    src/Thread/LogScanThread.h
    src/Thread/LogScanThread.cpp
    src/Core/AppPaths.cpp
    src/Core/Settings.cpp
    src/Core/SystemStatus.cpp
    src/Core/ClockJumpDetector.cpp
    src/Core/SingleInstance.h
    src/Core/SingleInstance.cpp
    src/Core/Observers.h
    src/Core/Application.h
    src/Core/Application.cpp
)

add_library(TokenViewerCore STATIC ${TV_CORE_SOURCES})
target_include_directories(TokenViewerCore PUBLIC src)
target_link_libraries(TokenViewerCore PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
# WinHTTP: HTTPS through the OS, since official Qt 5.15 builds need OpenSSL 1.1.1 DLLs for it.
target_link_libraries(TokenViewerCore PRIVATE winhttp)
# DWM: rounded popup corners on Windows 11 and dark title bars.
target_link_libraries(TokenViewerCore PRIVATE dwmapi)
target_compile_definitions(TokenViewerCore PUBLIC TV_VERSION="${TV_APP_VERSION}")

# WIN32: a GUI subsystem binary, so no console window opens.
add_executable(TokenViewer WIN32 src/main.cpp resources/TokenViewer.qrc)
target_link_libraries(TokenViewer PRIVATE TokenViewerCore)
if(MINGW)
    # Release builds ship without the symbol table.
    target_link_options(TokenViewer PRIVATE $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()

if(WIN32)
    # The app icon is drawn at build time, so the repository holds no image files (spec 5.1).
    add_executable(MakeIcon tools/MakeIcon.cpp)
    target_link_libraries(MakeIcon PRIVATE Qt5::Gui)
    set(TV_ICON_FILE "${CMAKE_CURRENT_BINARY_DIR}/TokenViewer.ico")
    # The tool needs Qt's DLLs (and the MinGW runtime next to them) without the caller having Qt on PATH.
    add_custom_command(OUTPUT "${TV_ICON_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${TV_QT_BIN_DIR_NATIVE}" "$<TARGET_FILE:MakeIcon>" "${TV_ICON_FILE}"
        DEPENDS MakeIcon
        COMMENT "Drawing TokenViewer.ico"
        VERBATIM)
    set(TV_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/TokenViewer.rc")
    configure_file(resources/TokenViewer.rc.in "${TV_RC_FILE}" @ONLY)
    set_source_files_properties("${TV_RC_FILE}" PROPERTIES OBJECT_DEPENDS "${TV_ICON_FILE}")
    target_sources(TokenViewer PRIVATE "${TV_RC_FILE}" "${TV_ICON_FILE}")
endif()

enable_testing()
add_subdirectory(tests)
