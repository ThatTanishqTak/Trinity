set(TRINITY_CONFIGURATIONS Debug Release Distribution)

function(trinity_declare_configurations)
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
        unset(CMAKE_DEFAULT_BUILD_TYPE CACHE)
        unset(CMAKE_DEFAULT_BUILD_TYPE)
    endif()

    get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(is_multi_config)
        set(CMAKE_CONFIGURATION_TYPES "${TRINITY_CONFIGURATIONS}" CACHE STRING "Build configurations" FORCE)
    else()
        if(NOT CMAKE_BUILD_TYPE)
            set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
        endif()
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${TRINITY_CONFIGURATIONS})
        if(NOT CMAKE_BUILD_TYPE IN_LIST TRINITY_CONFIGURATIONS)
            message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of: ${TRINITY_CONFIGURATIONS} (got '${CMAKE_BUILD_TYPE}')")
        endif()
    endif()
endfunction()

# Call after project(): Distribution starts as a copy of Release so third-party code is built the same way.
function(trinity_setup_configuration_flags)
    # project() caches these as empty for every custom configuration, so a plain set(CACHE) would be ignored.
    foreach(lang C CXX)
        if(NOT CMAKE_${lang}_FLAGS_DISTRIBUTION)
            set(CMAKE_${lang}_FLAGS_DISTRIBUTION "${CMAKE_${lang}_FLAGS_RELEASE}"
                CACHE STRING "Flags used by the ${lang} compiler during Distribution builds" FORCE)
        endif()
    endforeach()
    foreach(kind EXE SHARED MODULE STATIC)
        if(NOT CMAKE_${kind}_LINKER_FLAGS_DISTRIBUTION)
            set(CMAKE_${kind}_LINKER_FLAGS_DISTRIBUTION "${CMAKE_${kind}_LINKER_FLAGS_RELEASE}"
                CACHE STRING "Linker flags for ${kind} targets during Distribution builds" FORCE)
        endif()
    endforeach()
    mark_as_advanced(CMAKE_C_FLAGS_DISTRIBUTION CMAKE_CXX_FLAGS_DISTRIBUTION CMAKE_EXE_LINKER_FLAGS_DISTRIBUTION CMAKE_SHARED_LINKER_FLAGS_DISTRIBUTION CMAKE_MODULE_LINKER_FLAGS_DISTRIBUTION CMAKE_STATIC_LINKER_FLAGS_DISTRIBUTION)

    set(CMAKE_MAP_IMPORTED_CONFIG_DISTRIBUTION "Distribution;Release;RelWithDebInfo;MinSizeRel;" PARENT_SCOPE)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Release>:ProgramDatabase>" PARENT_SCOPE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" PARENT_SCOPE)
endfunction()