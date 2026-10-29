# Run by cmake -P after a build, with BINARY, NAMES joined by '+', EXPECT absent or present, and CONFIGURATION. Only Distribution is checked
if(NOT CONFIGURATION STREQUAL "Distribution")
    return()
endif()

# What a library's code leaves in a binary that links it. By default its name, in its RTTI, symbols and messages. A Windows binary keeps no symbols, so for a library whose name its code holds nowhere else, it is a message of code that is always linked with it: the window ImGui::NewFrame begins each frame, the angles ImGuizmo labels as it rotates, and the basis compressor's own messages. Slang's is its RTTI, in MSVC's names and in the Itanium ABI's
set(pattern_ImGui "Debug##Default")
set(pattern_ImGuizmo "deg .5.2f rad")
set(pattern_BasisEncoder "basis_compressor::")
set(pattern_Slang "@Slang@@|N5Slang")

string(REPLACE "+" ";" names "${NAMES}")
get_filename_component(binary_name "${BINARY}" NAME)
foreach(name IN LISTS names)
    set(pattern "${name}")
    if(DEFINED pattern_${name})
        set(pattern "${pattern_${name}}")
    endif()

    file(STRINGS "${BINARY}" found REGEX "${pattern}" LIMIT_COUNT 1)
    if(EXPECT STREQUAL "absent" AND found)
        list(GET found 0 example)
        message(FATAL_ERROR "${binary_name} carries code from ${name}, which a game never needs, such as \"${example}\". Something the game links has pulled it in")
    elseif(EXPECT STREQUAL "present" AND NOT found)
        message(FATAL_ERROR "${binary_name} links ${name}, yet what its code leaves was not found in the binary, so this check would not notice it in a game either")
    endif()
endforeach()