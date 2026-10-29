# The package a game module builds against: TrinityConfig.cmake, which find_package(Trinity) reads, and TrinityToolchain.cmake, which hands the module's build the compilers the engine was built with. Both are written beside the shared engine of each configuration that has one, from the templates in CMake/Package, so Debug Forge builds its module against the Debug engine. A static engine cannot load a module, so Distribution's TrinityConfig.cmake is the game package's, beside the same toolchain
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

# Every target the given ones link, through every target those link, privately too, since a static library's private links are its users' to link, and those named inside a generator expression as well. Imported targets go in imported_out instead, as their users find them themselves
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
                # Each generator expression's opening and separators split the list, so the names inside are items of their own, and a namespaced name keeps its ::
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

# The package a game's player builds against in Distribution, beside the static engine: TrinityConfig.cmake from TrinityGameConfig.cmake.in, with the static engine and every target it links exported as imported targets into a file it reads, and the runtime's objects as the player's main, so a game's build compiles only the game. export refuses a set that lacks a target one of them links, so nothing a player needs can be left out
function(trinity_write_game_package engine runtime)
    trinity_collect_link_targets(targets imported ${engine})
    # The one imported target the engine's libraries link is the threads library, which TrinityConfig.cmake finds as they did
    list(REMOVE_ITEM imported Threads::Threads)
    if(imported)
        message(FATAL_ERROR "The static engine links ${imported}, which the game package does not find for a player's build. Find it in CMake/Package/TrinityGameConfig.cmake.in as Threads is")
    endif()

    set(TRINITY_GAME_TARGETS_FILE "${CMAKE_BINARY_DIR}/Package/TrinityGameTargets.cmake")
    export(TARGETS ${targets} NAMESPACE TrinityGame:: FILE "${TRINITY_GAME_TARGETS_FILE}")

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