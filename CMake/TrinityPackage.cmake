# The packages a version holds, so a module or a player builds against it wherever its folder is

# Each include folder of the engine's interface, in its order, then where a version has it
set(TRINITY_PACKAGE_INCLUDE_ROOTS
    "${CMAKE_SOURCE_DIR}/Trinity-Engine/Include" "Include"
    "${CMAKE_SOURCE_DIR}/Vendor/imgui" "Include/Vendor/imgui"
    "${CMAKE_SOURCE_DIR}/Vendor/glm" "Include/Vendor/glm"
    "${CMAKE_SOURCE_DIR}/Vendor/entt/src" "Include/Vendor/entt"
)

# The include folders a package's targets name, from the version's folder, found as TRINITY_ROOT
set(TRINITY_PACKAGE_INCLUDE_DIRECTORIES "")
list(LENGTH TRINITY_PACKAGE_INCLUDE_ROOTS include_root_count)
math(EXPR last_include_root "${include_root_count} - 1")
foreach(index RANGE 1 ${last_include_root} 2)
    list(GET TRINITY_PACKAGE_INCLUDE_ROOTS ${index} include_directory)
    list(APPEND TRINITY_PACKAGE_INCLUDE_DIRECTORIES "\${TRINITY_ROOT}/${include_directory}")
endforeach()

function(trinity_write_engine_package target)
    set(staging "${CMAKE_CURRENT_BINARY_DIR}/Package")

    configure_file("${CMAKE_SOURCE_DIR}/CMake/Package/TrinityConfig.cmake.in" "${staging}/TrinityConfig.cmake.in" @ONLY)
    file(GENERATE
        OUTPUT "${OUTPUT_BASE}/TrinityConfig.cmake"
        INPUT "${staging}/TrinityConfig.cmake.in"
        CONDITION "${TRINITY_ENGINE_SHARED_CONFIG}"
        TARGET ${target}
    )

    configure_file("${CMAKE_SOURCE_DIR}/CMake/Package/TrinityToolchain.cmake.in" "${staging}/TrinityToolchain.cmake.in" @ONLY)
    file(GENERATE
        OUTPUT "${OUTPUT_BASE}/TrinityToolchain.cmake"
        INPUT "${staging}/TrinityToolchain.cmake.in"
        CONDITION "$<OR:${TRINITY_ENGINE_SHARED_CONFIG},$<CONFIG:Distribution>>"
        TARGET ${target}
    )
endfunction()

# Every target the given ones link, privately or in a generator expression too. Imported go apart
function(trinity_collect_link_targets out imported_out)
    set(pending ${ARGN})
    set(found "")
    set(imported_found "")
    while(pending)
        list(POP_FRONT pending name)
        if(NOT TARGET "${name}")
            continue()
        endif()

        get_target_property(aliased "${name}" ALIASED_TARGET)
        if(aliased)
            set(name "${aliased}")
        endif()

        get_target_property(imported "${name}" IMPORTED)
        if(imported)
            list(APPEND imported_found "${name}")
            continue()
        endif()

        if(name IN_LIST found)
            continue()
        endif()

        list(APPEND found "${name}")
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(libraries "${name}" ${property})
            if(libraries)
                # A generator expression's opening and separators split the list. A namespaced name keeps its ::
                string(REGEX REPLACE "\\$<[A-Za-z_]+:" ";" names "${libraries}")
                string(REGEX REPLACE "[$<>,]" ";" names "${names}")
                list(APPEND pending ${names})
            endif()
        endforeach()
    endwhile()

    list(REMOVE_DUPLICATES imported_found)
    set(${out} "${found}" PARENT_SCOPE)
    set(${imported_out} "${imported_found}" PARENT_SCOPE)
endfunction()

# A Distribution player's package: the static engine, all it links and the runtime's objects
function(trinity_write_game_package engine runtime)
    trinity_collect_link_targets(targets imported ${engine})
    # The engine links one imported target, the threads library, which TrinityConfig.cmake finds
    list(REMOVE_ITEM imported Threads::Threads)
    if(imported)
        message(FATAL_ERROR "The static engine links ${imported}, which the game package does not find. Add it to TrinityGameConfig.cmake.in as Threads is")
    endif()

    export(TARGETS ${targets} NAMESPACE TrinityGame:: FILE "${CMAKE_BINARY_DIR}/Package/TrinityGameExport.cmake")

    set(TRINITY_ENGINE_STATIC_TARGET ${engine})
    set(TRINITY_RUNTIME_TARGET ${runtime})
    set(staging "${CMAKE_CURRENT_BINARY_DIR}/Package")
    configure_file("${CMAKE_SOURCE_DIR}/CMake/Package/TrinityGameConfig.cmake.in" "${staging}/TrinityGameConfig.cmake.in" @ONLY)
    file(GENERATE
        OUTPUT "${OUTPUT_BASE}/TrinityConfig.cmake"
        INPUT "${staging}/TrinityGameConfig.cmake.in"
        CONDITION "$<CONFIG:Distribution>"
        TARGET ${runtime}
    )
endfunction()

# Copies into the version what its packages name, once a configuration's given targets are built
function(trinity_add_version_package target engine runtime)
    # Libraries from outside Trinity's build, added by TrinityDotNet.cmake and TrinityGameInput.cmake
    get_property(libraries GLOBAL PROPERTY TRINITY_PACKAGE_LIBRARIES)
    list(REMOVE_DUPLICATES libraries)
    # The RHI's test shaders, which only the Sandbox draws, so Scripts/Package.cmake leaves them out
    get_property(test_shaders GLOBAL PROPERTY TRINITY_TEST_SHADERS)

    # A path outside Trinity's build is one the game package must not name once it is written
    set(foreign_roots "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    foreach(library IN LISTS libraries)
        cmake_path(GET library PARENT_PATH library_directory)
        list(APPEND foreign_roots "${library_directory}")
    endforeach()
    list(REMOVE_DUPLICATES foreign_roots)

    set(player_checks "${CMAKE_SOURCE_DIR}/CMake/TrinityEditorLibraries.cmake" "${CMAKE_SOURCE_DIR}/CMake/TrinityCheckEditorLibraries.cmake" "${CMAKE_SOURCE_DIR}/CMake/TrinityCheckPlayerImports.cmake")
    set(files "${CMAKE_BINARY_DIR}/Package/$<CONFIG>/TrinityPackageFiles.cmake")
    file(GENERATE OUTPUT "${files}" CONTENT "# Written by CMake/TrinityPackage.cmake as Trinity generates, for CMake/TrinityWritePackage.cmake
set(TRINITY_PACKAGE_ROOT [==[${TRINITY_BIN_DIRECTORY}]==])
set(TRINITY_PACKAGE_DIRECTORY [==[${OUTPUT_BASE}]==])
set(TRINITY_PACKAGE_CONFIGURATION \"$<CONFIG>\")
set(TRINITY_PACKAGE_INCLUDE_ROOTS [==[${TRINITY_PACKAGE_INCLUDE_ROOTS}]==])
set(TRINITY_PACKAGE_ENGINE_INCLUDES [==[$<TARGET_PROPERTY:${engine},INTERFACE_INCLUDE_DIRECTORIES>]==])
set(TRINITY_PACKAGE_HEADERS
    [==[${CMAKE_SOURCE_DIR}/Trinity-Engine/Include]==] [==[Include]==] ON
    [==[${CMAKE_SOURCE_DIR}/Vendor/glm/glm]==] [==[Include/Vendor/glm/glm]==] ON
    [==[${CMAKE_SOURCE_DIR}/Vendor/entt/src/entt]==] [==[Include/Vendor/entt/entt]==] ON
    [==[${CMAKE_SOURCE_DIR}/Vendor/imgui]==] [==[Include/Vendor/imgui]==] OFF
    [==[${CMAKE_SOURCE_DIR}/Vendor/imgui/misc/cpp]==] [==[Include/Vendor/imgui/misc/cpp]==] OFF
)
set(TRINITY_PACKAGE_SOURCES [==[${CMAKE_SOURCE_DIR}/Trinity-Engine/Source/Trinity/Core/GlobalAllocations.cpp]==])
set(TRINITY_PACKAGE_TEMPLATES [==[${CMAKE_SOURCE_DIR}/Templates]==])
set(TRINITY_PACKAGE_GAME $<BOOL:$<CONFIG:Distribution>>)
set(TRINITY_PACKAGE_OBJECTS [==[$<$<CONFIG:Distribution>:$<TARGET_OBJECTS:${runtime}>>]==])
set(TRINITY_PACKAGE_PLAYER_CHECKS [==[${player_checks}]==])
set(TRINITY_PACKAGE_LIBRARIES [==[${libraries}]==])
set(TRINITY_PACKAGE_EXPORT [==[${CMAKE_BINARY_DIR}/Package/TrinityGameExport.cmake]==])
set(TRINITY_PACKAGE_FOREIGN_ROOTS [==[${foreign_roots}]==])
set(TRINITY_PACKAGE_TEST_SHADERS [==[${test_shaders}]==])
" TARGET ${runtime})

    add_custom_target(${target} ALL
        COMMAND "${CMAKE_COMMAND}" "-DTRINITY_PACKAGE_FILES=${files}" -P "${CMAKE_SOURCE_DIR}/CMake/TrinityWritePackage.cmake"
        COMMENT "Copying what the version's packages name into it"
        VERBATIM
    )
    add_dependencies(${target} ${ARGN})
endfunction()