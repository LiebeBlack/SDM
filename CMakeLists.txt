cmake_minimum_required(VERSION 3.25)

# ============================================================================
#  S-DOWNLOAD MANAGER (SDM)
#  Blueprint: OSDM.md  -  https://sdownload.local
#
#  Objetivos:
#    SDM            -> Aplicacion de escritorio (Qt 6 QML + motor de descargas)
#    SDMNativeHost  -> Puente Native Messaging (STDIN/STDOUT <-> WebSocket local)
#    sdm_tests      -> Pruebas unitarias (CTest)
# ============================================================================

project(SDM
    VERSION 1.0.0
    DESCRIPTION "S-Download Manager (SDM) - Sucesor definitivo de IDM"
    HOMEPAGE_URL "https://sdownload.local"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# 1. Deteccion del estandar C++ mas alto soportado (C++26 -> C++23 -> C++20)
#    El codigo fuente es C++20 portable; el blueprint exige C++26 cuando el
#    compilador lo permite.
# ---------------------------------------------------------------------------
include(CheckCXXCompilerFlag)

set(SDM_CXX_STANDARD 20)
if(MSVC)
    check_cxx_compiler_flag("/std:c++latest" SDM_MSVC_STD_LATEST)
    if(SDM_MSVC_STD_LATEST)
        add_compile_options(/std:c++latest)
        set(SDM_CXX_STANDARD 26)
    else()
        add_compile_options(/std:c++20)
    endif()
else()
    set(SDM_CXX_FLAG_DETECTED "")
    foreach(_flag IN ITEMS "-std=c++26" "-std=c++2c" "-std=c++23" "-std=c++2b" "-std=c++20")
        string(MAKE_C_IDENTIFIER "SDM_HAS${_flag}" _flag_var)
        check_cxx_compiler_flag("${_flag}" ${_flag_var})
        if(${_flag_var})
            set(SDM_CXX_FLAG_DETECTED "${_flag}")
            break()
        endif()
    endforeach()
    if(SDM_CXX_FLAG_DETECTED STREQUAL "-std=c++26" OR SDM_CXX_FLAG_DETECTED STREQUAL "-std=c++2c")
        set(SDM_CXX_STANDARD 26)
    elseif(SDM_CXX_FLAG_DETECTED MATCHES "c\\+\\+23|c\\+\\+2b")
        set(SDM_CXX_STANDARD 23)
    else()
        set(SDM_CXX_STANDARD 20)
    endif()
    if(SDM_CXX_FLAG_DETECTED)
        add_compile_options("${SDM_CXX_FLAG_DETECTED}")
    endif()
endif()
message(STATUS "SDM: usando estandar C++${SDM_CXX_STANDARD}")

# ---------------------------------------------------------------------------
# 2. Opciones de compilacion
# ---------------------------------------------------------------------------
option(SDM_BUILD_TESTS      "Construir las pruebas unitarias"              ON)
option(SDM_BUILD_UI         "Construir la interfaz Qt Quick (GUI)"          ON)
option(SDM_WARNINGS_AS_ERRORS "Tratar advertencias como errores"            OFF)
option(SDM_ENABLE_SANITIZERS  "Habilitar Address/UB Sanitizers (GCC/Clang)"  OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
    if(SDM_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(SDM_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
    if(SDM_ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# 3. Dependencias
#    Qt 6.5+ (Core, Gui, Qml, Quick, QuickControls2, Network, WebSockets)
#    libcurl 7.61+ / SQLite3 / nlohmann-json / fmt / Threads
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network WebSockets)
find_package(CURL 7.61 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Threads REQUIRED)

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

# ---------------------------------------------------------------------------
# 4. Fuentes de la aplicacion principal
# ---------------------------------------------------------------------------
set(SDM_CORE_SOURCES
    src/database/Models.h
    src/core/Log.h
    src/core/Log.cpp
    src/core/FileAllocator.h
    src/core/FileAllocator.cpp
    src/core/BrowserEmulator.h
    src/core/BrowserEmulator.cpp
    src/core/SegmentWorker.h
    src/core/SegmentWorker.cpp
    src/core/DownloadEngine.h
    src/core/DownloadEngine.cpp
    src/core/StreamDownloader.h
    src/core/StreamDownloader.cpp
    src/database/DatabaseManager.h
    src/database/DatabaseManager.cpp
    src/bridge/ProtocolHandler.h
    src/bridge/ProtocolHandler.cpp
    src/bridge/WebSocketServer.h
    src/bridge/WebSocketServer.cpp
)

set(SDM_UI_SOURCES
    src/ui/GuiManager.h
    src/ui/GuiManager.cpp
    src/ui/resources.qrc
)

set(SDM_UI_QML_FILES
    src/ui/qml/main.qml
    src/ui/qml/theme/qmldir
    src/ui/qml/theme/Theme.qml
    src/ui/qml/theme/Style.qml
    src/ui/qml/components/SdmToolButton.qml
    src/ui/qml/components/TopToolbar.qml
    src/ui/qml/components/Sidebar.qml
    src/ui/qml/components/DownloadTable.qml
    src/ui/qml/components/SegmentBar.qml
    src/ui/qml/components/NewTaskModal.qml
    src/ui/qml/components/SettingsDialog.qml
)

set(SDM_UI_ASSET_FILES
    src/ui/assets/logo.svg
    src/ui/assets/icon.svg
    src/ui/assets/download.svg
    src/ui/assets/folder.svg
)

# Biblioteca estatica con el motor completo: la comparten el binario SDM y
# las pruebas unitarias (evita recompilar el core por cada test).
add_library(sdm_core STATIC ${SDM_CORE_SOURCES})

target_include_directories(sdm_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(sdm_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::WebSockets
    CURL::libcurl
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
    fmt::fmt
    Threads::Threads
)

add_library(sdm_bridge STATIC
    src/bridge/ProtocolHandler.h
    src/bridge/ProtocolHandler.cpp
)

target_link_libraries(sdm_bridge PRIVATE sdm_core)

add_executable(SDMNativeHost
    src/bridge/NativeMessagingHost.h
    src/bridge/NativeMessagingHost.cpp
)

target_include_directories(SDMNativeHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SDMNativeHost PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    sdm_bridge
)

add_executable(SDM WIN32
    src/main.cpp
    ${SDM_UI_SOURCES}
    ${SDM_UI_QML_FILES}
    ${SDM_UI_ASSET_FILES}
)

target_link_libraries(SDM PRIVATE
    sdm_core
    sdm_bridge
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

# ---------------------------------------------------------------------------
# 6. Plataforma
# ---------------------------------------------------------------------------
if(WIN32)
    target_sources(SDM PRIVATE packaging/windows/app.manifest)
    target_link_libraries(SDM PRIVATE dwmapi psapi shell32 ole32 ws2_32 advapi32)
    # Nombre del binario en Windows
    set_target_properties(SDM PROPERTIES OUTPUT_NAME "SDM" WIN32_EXECUTABLE TRUE)
    set_target_properties(SDMNativeHost PROPERTIES OUTPUT_NAME "SDMNativeHost")
    # Subsistema consola solo para el host (necesita STDIN/STDOUT)
    set_target_properties(SDMNativeHost PROPERTIES WIN32_EXECUTABLE FALSE)
elseif(APPLE)
    set_target_properties(SDM PROPERTIES OUTPUT_NAME "SDM" MACOSX_BUNDLE TRUE)
else()
    set_target_properties(SDM PROPERTIES OUTPUT_NAME "SDM")
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(SDM PRIVATE ${CMAKE_DL_LIBS})
endif()

# ---------------------------------------------------------------------------
# 7. Pruebas unitarias
# ---------------------------------------------------------------------------
# (tests directory does not exist - see CI configuration)

# ---------------------------------------------------------------------------
# 8. Instalacion
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS SDM SDMNativeHost sdm_bridge
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "."
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/sdm.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES src/ui/assets/logo.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME "sdm.svg")
    install(FILES packaging/native-host/com.sdm.bridge.chrome.json
        DESTINATION "${CMAKE_INSTALL_DATADIR}/com.sdm.bridge")
endif()

message(STATUS "SDM ${PROJECT_VERSION} configurado (tests: ${SDM_BUILD_TESTS}, gui: ${SDM_BUILD_UI})")
