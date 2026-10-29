# Libraries that belong to the editor alone. A game's Distribution build must carry none of their code, which their names in its RTTI and messages would show
set(TRINITY_EDITOR_LIBRARIES fastgltf simdjson Assimp)
# What a player must carry none of besides: the editor's UI and the texture encoder, which Forge links, and Slang, which only compiles shaders as Trinity builds. The script knows each by what its code leaves in a binary
set(TRINITY_PLAYER_EXCLUDED_LIBRARIES ${TRINITY_EDITOR_LIBRARIES} ImGui ImGuizmo BasisEncoder Slang)
set(TRINITY_EDITOR_LIBRARIES_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/TrinityCheckEditorLibraries.cmake")
set(TRINITY_PLAYER_IMPORTS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/TrinityCheckPlayerImports.cmake")

# dumpbin, which reads the DLLs a Windows binary imports, comes beside the compiler
if(MSVC)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _trinity_compiler_directory)
    find_program(TRINITY_DUMPBIN dumpbin HINTS "${_trinity_compiler_directory}")
endif()

# After each Distribution build, reads the target's binary for the libraries named after expect, or the editor's own without any. "absent" fails the build when one is found, and "present" when one is missing, which Forge uses to show the check finds them where they are. The names go joined by '+', which no shell treats specially, since the Windows command line splits a quoted '|' into a pipe, and what the script looks for is kept in it for the same reason
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

# After each Distribution build of a player: none of the code a game never needs, and on Windows no DLL imported but Windows' own, so it starts on a machine with nothing installed
function(trinity_check_player target)
    trinity_check_editor_libraries(${target} absent ${TRINITY_PLAYER_EXCLUDED_LIBRARIES})
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:${target}>" "-DDUMPBIN=${TRINITY_DUMPBIN}" "-DCONFIGURATION=$<CONFIG>" -P "${TRINITY_PLAYER_IMPORTS_SCRIPT}"
            VERBATIM
        )
    endif()
endfunction()