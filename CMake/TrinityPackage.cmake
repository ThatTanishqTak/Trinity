# The package a game module builds against: TrinityConfig.cmake, which find_package(Trinity) reads, and TrinityToolchain.cmake, which hands the module's build the compilers the engine was built with. Both are written beside the shared engine of each configuration that has one, from the templates in CMake/Package, so Debug Forge builds its module against the Debug engine. A static engine cannot load a module, so Distribution has no package
function(trinity_write_engine_package target)
    set(staging "${CMAKE_CURRENT_BINARY_DIR}/Package")

    foreach(file IN ITEMS TrinityConfig.cmake TrinityToolchain.cmake)
        configure_file("${CMAKE_SOURCE_DIR}/CMake/Package/${file}.in" "${staging}/${file}.in" @ONLY)
        file(GENERATE
            OUTPUT "${OUTPUT_BASE}/${file}"
            INPUT "${staging}/${file}.in"
            CONDITION "${TRINITY_ENGINE_SHARED_CONFIG}"
            TARGET ${target}
        )
    endforeach()
endfunction()