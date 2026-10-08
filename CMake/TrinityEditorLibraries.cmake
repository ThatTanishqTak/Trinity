# Libraries that belong to the editor alone. A game's Distribution build must carry none of their code, which their names in its RTTI and messages would show
set(TRINITY_EDITOR_LIBRARIES fastgltf simdjson Assimp)
set(TRINITY_EDITOR_LIBRARIES_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/TrinityCheckEditorLibraries.cmake")

# After each Distribution build, reads the target's binary for those names. "absent" fails the build when one is found, and "present" when one is missing, which Forge uses to show the check finds them where they are. The names go joined by '+', which no shell treats specially, since the Windows command line splits a quoted '|' into a pipe
function(trinity_check_editor_libraries target expect)
    string(JOIN "+" names ${TRINITY_EDITOR_LIBRARIES})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:${target}>" "-DNAMES=${names}" "-DEXPECT=${expect}" "-DCONFIGURATION=$<CONFIG>" -P "${TRINITY_EDITOR_LIBRARIES_SCRIPT}"
        VERBATIM
    )
endfunction()