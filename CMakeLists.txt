cmake_minimum_required(VERSION 3.20)

# version.txt is the one place the version lives: release-please rewrites it when a
# release lands, and project(), the PP_VERSION_* macros and the VERSIONINFO resource all
# derive from it. Nothing else may carry a version number.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" PP_VERSION_RAW)
string(STRIP "${PP_VERSION_RAW}" PP_VERSION)
if(NOT PP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.txt must contain a bare MAJOR.MINOR.PATCH version, "
                        "but holds '${PP_VERSION}'.")
endif()

project(PowerPeek
    VERSION ${PP_VERSION}
    DESCRIPTION "Fluent-styled Xbox controller battery monitor for Windows"
    LANGUAGES CXX)

# file(READ) runs at configure time only, so an edit to version.txt would otherwise leave
# a stale version compiled in until the next unrelated reconfigure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/version.txt")

if(NOT WIN32)
    message(FATAL_ERROR "PowerPeek targets Windows only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "PowerPeek requires MSVC (the Windows SDK headers it uses are MSVC-specific).")
endif()

enable_language(RC)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(WindowsSdk)

option(PP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Off by default because it is the only part of the build that reaches the network: the
# test binary needs doctest, fetched at configure time. A plain build of the application
# stays dependency-free and works offline, which is the point.
option(PP_BUILD_TESTS "Build the unit tests" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake 3.20 has no /std:c++20 branch for MSVC and falls back to /std:c++latest, which on
# 14.44 means C++23-and-drafts semantics: a moving target that accepts code the standard
# this project targets does not. Point the mapping at the real flag.
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.29)
    set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "-std:c++20")
    set(CMAKE_CXX20_EXTENSION_COMPILE_OPTION "-std:c++20")
endif()

# Single self-contained executable: static CRT means no VC++ redistributable to ship. Set
# as a directory variable rather than per target, so the test binary cannot end up on the
# dynamic CRT and then fail to link against the static one everything else uses.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# app.rc includes this; CMake passes target_include_directories to rc.exe as well, which
# is how the generated header is found from the source tree.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/pp_version.h"
    @ONLY)

# Everything compiled from this repository is compiled the same way. Held on one interface
# target rather than repeated per target, because a test binary built with different
# language settings than the code under test is testing something else.
add_library(pp_compile_settings INTERFACE)

target_include_directories(pp_compile_settings INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_options(pp_compile_settings INTERFACE
    /utf-8 /permissive- /EHsc /bigobj
    /Zc:__cplusplus /Zc:inline /Zc:throwingNew /Zc:externConstexpr
    /diagnostics:caret
    $<$<CONFIG:Debug>:/JMC>
    $<$<NOT:$<CONFIG:Debug>>:/Gy /Gw /Oi>)

target_compile_definitions(pp_compile_settings INTERFACE
    UNICODE
    _UNICODE
    STRICT
    # Xinput.h hides XInputGetBatteryInformation and the BATTERY_* constants behind
    # _WIN32_WINNT >= _WIN32_WINNT_WIN8, and several DWM attributes need the same gate.
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A000000
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NOSERVICE
    NOMCX
    NOIME
    PP_VERSION_STRING="${PROJECT_VERSION}"
    PP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PP_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# The layer with no window, no device and no message loop: parsing, persistence, the rules
# that decide when to notify, and the easing maths. It is a library rather than part of the
# executable so that a test binary can link it without spinning up Direct2D, XAudio2 or a
# window -- and so that "this part carries no UI", which the architecture notes claim, is a
# fact the build enforces rather than a promise a later commit can quietly break.
#
# Listed rather than globbed, unlike the rest of the sources: the boundary is the whole
# point, so a file joins this side of it only deliberately.
set(PP_CORE_SOURCES
    core/AppPaths.cpp
    core/Json.cpp
    core/Logger.cpp
    core/Settings.cpp
    core/Strings.cpp
    core/Win.cpp
    battery/BatteryHistory.cpp
    battery/DeviceInfo.cpp
    battery/DeviceMerge.cpp
    battery/EventDetector.cpp
    audio/WavReader.cpp
    notify/ToastLayout.cpp
    ui/Animation.cpp
    ui/Quantise.cpp)
list(TRANSFORM PP_CORE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/")

add_library(PowerPeekCore STATIC ${PP_CORE_SOURCES})

target_link_libraries(PowerPeekCore PUBLIC pp_compile_settings)
target_link_libraries(PowerPeekCore PRIVATE
    shell32     # SHGetKnownFolderPath, for %LOCALAPPDATA%
    ole32)      # CoTaskMemFree, which frees what that returns

pp_set_warnings(PowerPeekCore ${PP_WARNINGS_AS_ERRORS})
pp_add_cppwinrt_includes(PowerPeekCore)

# The rest is globbed so that adding a file to src/ needs no CMake edit. CONFIGURE_DEPENDS
# makes the generator re-run the glob when the directory contents change. The core sources
# are then removed again: they are already compiled into PowerPeekCore, and compiling them
# a second time here would be a second definition of every symbol in them.
file(GLOB_RECURSE PP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
list(REMOVE_ITEM PP_SOURCES ${PP_CORE_SOURCES})

add_executable(PowerPeek WIN32
    ${PP_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc")

target_link_libraries(PowerPeek PRIVATE
    pp_compile_settings
    PowerPeekCore

    d2d1            # Direct2D 1.3 rendering
    dwrite          # DirectWrite text layout
    d3d11           # D3D11 device backing the Direct2D device
    dxgi            # composition swap chains
    dcomp           # DirectComposition visual tree (per-pixel alpha windows)
    windowscodecs   # WIC, used to rasterise the tray icon
    dxguid          # CLSID_D2D1Shadow and the other built-in effect ids; d2d1.lib has none of them
    xaudio2         # notification sound playback
    mf mfplat mfreadwrite mfuuid   # decoding non-WAV sounds to PCM
    dwmapi          # dark title bar / Win11 backdrop probing
    uxtheme         # dark mode for the few system-drawn surfaces (menus)
    shell32         # tray icon, shell links
    shlwapi
    propsys         # AppUserModelID on the Start Menu shortcut
    comctl32
    winmm
    user32 gdi32 ole32 oleaut32 uuid advapi32 version

    # Must stay last. windowsapp.lib re-exports XInputGetBatteryInformation, XAudio2Create,
    # D2D1CreateFactory and DWriteCreateFactory; listed first, it rebinds them to the UWP
    # DLLs (XINPUTUAP.dll instead of XINPUT1_4.dll) -- which links, runs, and is the wrong
    # contract for a desktop application.
    windowsapp)     # C++/WinRT projection: WinRT activation and IInspectable plumbing

pp_set_warnings(PowerPeek ${PP_WARNINGS_AS_ERRORS})
pp_add_cppwinrt_includes(PowerPeek)

# The manifest lives in app.rc; stop the linker from generating a second one.
target_link_options(PowerPeek PRIVATE /MANIFEST:NO /ENTRY:wWinMainCRTStartup)

set_target_properties(PowerPeek PROPERTIES OUTPUT_NAME "PowerPeek")

# Keep the source tree layout in the IDE.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src"
    FILES ${PP_SOURCES} ${PP_CORE_SOURCES})

install(TARGETS PowerPeek RUNTIME DESTINATION .)

if(PP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
