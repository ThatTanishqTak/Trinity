# Run by cmake -P after a player's build on Windows, with BINARY, DUMPBIN and CONFIGURATION. Only Distribution is checked. Every DLL the player imports, delay-loaded ones too, must be Windows' own, in System32 or one of its API sets, and none of the C runtime's, which Distribution links statically, so the player starts on a machine with nothing installed but Windows and a graphics driver. The Agility SDK's D3D12Core.dll is loaded by d3d12.dll, never imported
if(NOT CONFIGURATION STREQUAL "Distribution")
    return()
endif()

get_filename_component(binary_name "${BINARY}" NAME)
if(NOT DUMPBIN)
    message(FATAL_ERROR "dumpbin, which comes with Visual Studio's C++ tools, was not found beside the compiler, so the DLLs ${binary_name} imports cannot be checked")
endif()

execute_process(COMMAND "${DUMPBIN}" /nologo /dependents "${BINARY}" OUTPUT_VARIABLE output RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dumpbin could not read the DLLs ${binary_name} imports")
endif()

# dumpbin lists each DLL alone on a line, indented
string(REGEX MATCHALL "\n[ \t]+[^ \t\r\n]+\\.[dD][lL][lL]" dlls "${output}")

set(windows "$ENV{SystemRoot}")
if(NOT windows)
    set(windows "C:/Windows")
endif()
cmake_path(CONVERT "${windows}/System32" TO_CMAKE_PATH_LIST system)

set(refused "")
foreach(dll IN LISTS dlls)
    string(STRIP "${dll}" dll)
    string(TOLOWER "${dll}" lower)
    if(lower MATCHES "^(vcruntime|msvcp|ucrtbase|concrt|vccorlib|api-ms-win-crt-)")
        list(APPEND refused "${dll}, of the C runtime Distribution links statically")
    elseif(lower MATCHES "^(api-ms-win-|ext-ms-win-)")
        continue()
    elseif(NOT EXISTS "${system}/${dll}")
        list(APPEND refused "${dll}, which is not one of Windows' own")
    endif()
endforeach()

if(refused)
    list(JOIN refused "; " text)
    message(FATAL_ERROR "${binary_name} imports ${text}, so it would not start on a machine without them. Something the player links has pulled them in")
endif()