# Run by cmake -P after a Distribution build, with BINARY, NAMES joined by '+', and EXPECT
if(NOT CONFIGURATION STREQUAL "Distribution")
    return()
endif()

# What a library's code leaves in a binary: its name, or a message where Windows keeps no symbols
set(pattern_ImGui "Debug##Default")
set(pattern_ImGuizmo "deg .5.2f rad")
set(pattern_BasisEncoder "basis_compressor::")
set(pattern_Slang "@Slang@@|N5Slang")
# The keys a Development player's overlay names, and the id of the hook RmlUi's debugger leaves
set(pattern_DevelopmentOverlay "F3 stats and log")
set(pattern_RmlUiDebugger "rmlui-debug-hook")
# RmlUi's warning for a missing font, which layout always links, and FreeType's auto-hinter name
set(pattern_RmlUi "No font face defined")
set(pattern_FreeType "autofitter")

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
    elseif(EXPECT STREQUAL "required" AND NOT found)
        message(FATAL_ERROR "${binary_name} carries no code from ${name}, which every game needs to draw its UI")
    endif()
endforeach()