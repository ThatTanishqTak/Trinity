# Libraries of the editor alone. A game's Distribution build must carry none of their code
set(TRINITY_EDITOR_LIBRARIES fastgltf simdjson Assimp)
# What else a player must not carry: editor UI, texture encoder, Slang, overlay, RmlUi debugger
set(TRINITY_PLAYER_EXCLUDED_LIBRARIES ${TRINITY_EDITOR_LIBRARIES} ImGui ImGuizmo BasisEncoder Slang DevelopmentOverlay RmlUiDebugger)
# What every player must carry: RmlUi, which draws a game's UI, and the FreeType it draws text with
set(TRINITY_PLAYER_REQUIRED_LIBRARIES RmlUi FreeType)
set(TRINITY_EDITOR_LIBRARIES_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/TrinityCheckEditorLibraries.cmake")
set(TRINITY_PLAYER_IMPORTS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/TrinityCheckPlayerImports.cmake")

# dumpbin, which reads the DLLs a Windows binary imports, comes beside the compiler
if(MSVC)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _trinity_compiler_directory)
    find_program(TRINITY_DUMPBIN dumpbin HINTS "${_trinity_compiler_directory}")
endif()

# Checks a Distribution binary for the named libraries. Names join with '+': cmd pipes a '|'
function(trinity_check_editor_libraries target expect)
    set(libraries ${ARGN})
    if(NOT libraries)
        set(libraries ${TRINITY_EDITOR_LIBRARIES})
    endif()

    string(JOIN "+" names ${libraries})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:${target}>" "-DNAMES=${names}" "-DEXPECT=${expect}" "-DCONFIGURATION=$<CONFIG>" -P "${TRINITY_EDITOR_LIBRARIES_SCRIPT}"
        VERBATIM
    )
endfunction()

# After each Distribution build on Windows: the program must import no DLL but Windows' own
function(trinity_check_imports target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:${target}>" "-DDUMPBIN=${TRINITY_DUMPBIN}" "-DCONFIGURATION=$<CONFIG>" -P "${TRINITY_PLAYER_IMPORTS_SCRIPT}"
            VERBATIM
        )
    endif()
endfunction()

# After a player's Distribution build: no editor code, all a game needs, and only Windows' own DLLs
function(trinity_check_player target)
    trinity_check_editor_libraries(${target} absent ${TRINITY_PLAYER_EXCLUDED_LIBRARIES})
    trinity_check_editor_libraries(${target} required ${TRINITY_PLAYER_REQUIRED_LIBRARIES})
    trinity_check_imports(${target})
endfunction()