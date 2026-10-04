# Copies files beside the executables, to bin/<configuration>/Trinity/<destination>, and copies them again when a source changes
function(trinity_add_assets target destination)
    set(outputs "")
    set(directory "${OUTPUT_BASE}/${destination}")

    foreach(source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        cmake_path(GET source FILENAME name)
        set(output "${directory}/${name}")

        add_custom_command(OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${output}"
            DEPENDS "${source}"
            COMMENT "Copying ${destination}/${name}"
            VERBATIM
        )
        list(APPEND outputs "${output}")
    endforeach()

    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()