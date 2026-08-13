# Locating the C++/WinRT projection headers that ship inside the Windows SDK.
#
# A developer command prompt puts "<sdk>/Include/<ver>/cppwinrt" on INCLUDE, so a build
# started from vcvars64.bat already resolves <winrt/base.h>. Builds driven by an IDE that
# composes the environment differently may not, so the directory is located and added
# explicitly. cppwinrt.exe is never invoked: this application only consumes Windows types,
# it authors none, and the SDK ships the generated projection headers prebuilt.

function(pp_add_cppwinrt_includes target)
    if(NOT DEFINED CACHE{PP_CPPWINRT_INCLUDE_DIR})
        set(_hints "")

        # Whatever SDK the current environment selected wins over anything on disk.
        if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
            string(REGEX REPLACE "\\$" "" _ver "$ENV{WindowsSDKVersion}")
            list(APPEND _hints "$ENV{WindowsSdkDir}Include/${_ver}/cppwinrt")
        endif()

        file(GLOB _installed
            "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt"
            "C:/Program Files/Windows Kits/10/Include/*/cppwinrt")
        list(SORT _installed)
        list(REVERSE _installed) # newest SDK first
        list(APPEND _hints ${_installed})

        find_path(PP_CPPWINRT_INCLUDE_DIR
            NAMES "winrt/base.h"
            HINTS ${_hints}
            PATHS ENV INCLUDE
            DOC "Windows SDK directory containing the C++/WinRT projection headers")
    endif()

    if(NOT PP_CPPWINRT_INCLUDE_DIR)
        message(FATAL_ERROR
            "Could not find <winrt/base.h>. Install the 'Windows 10/11 SDK' component of "
            "Visual Studio, or configure from a Developer Command Prompt.")
    endif()

    target_include_directories(${target} SYSTEM PRIVATE "${PP_CPPWINRT_INCLUDE_DIR}")
endfunction()
