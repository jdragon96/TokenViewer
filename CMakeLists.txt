cmake_minimum_required(VERSION 3.21)
project(TokenViewer VERSION 0.1.0 LANGUAGES CXX)

option(TV_NATIVE_TRAY "Use the Objective-C++ NSStatusItem tray icon" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network Test)

# LGPLv3: Qt must stay a shared library (docs/LicenseCompliance.md).
foreach(_module Core Gui Widgets Network)
    get_target_property(_type Qt5::${_module} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Qt5::${_module} must be linked as a shared library (LGPLv3)")
    endif()
endforeach()

set(TV_CORE_SOURCES
    src/UI/TrayIconPainter.cpp
    src/Analysis/PriceTable.cpp
    src/Analysis/LogParser.cpp
    src/Analysis/TokenAggregator.cpp
    src/Analysis/LogStore.cpp
    src/Service/CredentialStore.cpp
    src/Service/UsageApiClient.cpp
    src/Service/RefreshPolicy.cpp
    src/Thread/UsageFetchThread.h
    src/Thread/UsageFetchThread.cpp
    src/Thread/LogScanThread.h
    src/Thread/LogScanThread.cpp
)
if(TV_NATIVE_TRAY)
    enable_language(OBJCXX)
    list(APPEND TV_CORE_SOURCES src/UI/TrayIcon.h src/UI/TrayIconMac.mm)
    set_source_files_properties(src/UI/TrayIconMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    list(APPEND TV_CORE_SOURCES src/UI/TrayIcon.h src/UI/TrayIcon.cpp)
endif()

add_library(TokenViewerCore STATIC ${TV_CORE_SOURCES})
target_include_directories(TokenViewerCore PUBLIC src)
target_link_libraries(TokenViewerCore PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
target_link_libraries(TokenViewerCore PUBLIC "-framework Security" "-framework CoreFoundation")
if(TV_NATIVE_TRAY)
    target_link_libraries(TokenViewerCore PUBLIC "-framework AppKit")
endif()
target_compile_definitions(TokenViewerCore PUBLIC TV_VERSION="${PROJECT_VERSION}")

add_executable(TokenViewer MACOSX_BUNDLE src/main.cpp)
set_target_properties(TokenViewer PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/resources/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER com.tokenviewer.TokenViewer
    MACOSX_BUNDLE_BUNDLE_NAME TokenViewer
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(TokenViewer PRIVATE TokenViewerCore)

enable_testing()
add_subdirectory(tests)
