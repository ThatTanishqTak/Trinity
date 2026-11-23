# Run by cmake -P after a Distribution build on Windows: each imported DLL must be Windows' own
if(NOT CONFIGURATION STREQUAL "Distribution")
    return()
endif()

get_filename_component(binary_name "${BINARY}" NAME)
if(NOT DUMPBIN)
    message(FATAL_ERROR "dumpbin, from Visual Studio's C++ tools, was not found beside the compiler, so ${binary_name}'s imports cannot be checked")
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
    message(FATAL_ERROR "${binary_name} imports ${text}, so it would not start on a machine without them. Something it links has pulled them in")
endif()