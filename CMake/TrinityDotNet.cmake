# The .NET SDK that builds Trinity-ScriptCore and the host pack the engine starts .NET with, found the way the Vulkan SDK is: where DOTNET_ROOT points, in the default install folders, or on PATH. global.json at the root pins the SDK, so an SDK it does not take counts as none
option(TRINITY_ENABLE_DOTNET "Build Trinity-ScriptCore and host .NET for C# scripts, when a .NET 10 SDK is found" ON)

set(TRINITY_DOTNET_FOUND OFF)

if(TRINITY_ENABLE_DOTNET)
    find_program(TRINITY_DOTNET dotnet HINTS "$ENV{DOTNET_ROOT}" "$ENV{ProgramFiles}/dotnet" "/usr/local/share/dotnet" "/usr/share/dotnet" "/usr/lib/dotnet")

    set(TRINITY_DOTNET_REASON "")
    if(NOT TRINITY_DOTNET)
        set(TRINITY_DOTNET_REASON "no dotnet was found")
    else()
        execute_process(COMMAND "${TRINITY_DOTNET}" --version WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE TRINITY_DOTNET_VERSION RESULT_VARIABLE dotnet_result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT dotnet_result EQUAL 0)
            set(TRINITY_DOTNET_REASON "${TRINITY_DOTNET} has no SDK that global.json takes")
        endif()
    endif()

    # The host pack of this architecture with the newest runtime, whose folder ends in the runtime identifier, such as win-x64, linux-x64 or ubuntu.24.04-x64
    if(TRINITY_DOTNET_REASON STREQUAL "")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
            set(dotnet_architecture "arm64")
        else()
            set(dotnet_architecture "x64")
        endif()

        file(REAL_PATH "${TRINITY_DOTNET}" dotnet_executable)
        cmake_path(GET dotnet_executable PARENT_PATH dotnet_root)
        file(GLOB nethost_headers "${dotnet_root}/packs/Microsoft.NETCore.App.Host.*-${dotnet_architecture}/*/runtimes/*-${dotnet_architecture}/native/nethost.h")
        list(SORT nethost_headers COMPARE NATURAL ORDER DESCENDING)
        if(nethost_headers)
            list(GET nethost_headers 0 nethost_header)
            cmake_path(GET nethost_header PARENT_PATH TRINITY_NETHOST_DIRECTORY)
        else()
            set(TRINITY_DOTNET_REASON "the SDK at ${dotnet_root} has no host pack for ${dotnet_architecture}")
        endif()
    endif()

    if(TRINITY_DOTNET_REASON STREQUAL "")
        set(TRINITY_DOTNET_FOUND ON)
        message(STATUS "Trinity: .NET SDK ${TRINITY_DOTNET_VERSION}, nethost from ${TRINITY_NETHOST_DIRECTORY}")
    else()
        message(WARNING "C# scripting is off, since ${TRINITY_DOTNET_REASON}. Install the .NET 10 SDK and point DOTNET_ROOT at it, or set TRINITY_ENABLE_DOTNET to OFF to hide this.")
    endif()
endif()

# The engine hosts .NET through nethost, which finds the installed runtime's hostfxr. It links statically except on Windows, where the static library is built for the static C runtime the engine does not use, so nethost.dll goes beside the executables instead
function(trinity_link_dotnet_host target)
    if(NOT TRINITY_DOTNET_FOUND)
        return()
    endif()

    target_compile_definitions(${target} PRIVATE TR_DOTNET)
    target_include_directories(${target} SYSTEM PRIVATE "${TRINITY_NETHOST_DIRECTORY}")
    if(WIN32)
        target_link_libraries(${target} PRIVATE "${TRINITY_NETHOST_DIRECTORY}/nethost.lib")
    else()
        target_link_libraries(${target} PRIVATE "${TRINITY_NETHOST_DIRECTORY}/libnethost.a")
    endif()
endfunction()

# nethost.dll to bin/<configuration>/Trinity, beside every executable, on Windows
function(trinity_add_dotnet_host_runtime target)
    if(NOT TRINITY_DOTNET_FOUND OR NOT WIN32)
        add_custom_target(${target})

        return()
    endif()

    add_custom_command(OUTPUT "${OUTPUT_BASE}/nethost.dll"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_BASE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TRINITY_NETHOST_DIRECTORY}/nethost.dll" "${OUTPUT_BASE}/nethost.dll"
        DEPENDS "${TRINITY_NETHOST_DIRECTORY}/nethost.dll"
        COMMENT "Copying nethost.dll"
        VERBATIM
    )
    add_custom_target(${target} ALL DEPENDS "${OUTPUT_BASE}/nethost.dll")
endfunction()