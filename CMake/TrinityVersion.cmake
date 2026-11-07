# The engine's identity and Trinity.trversion, the manifest that makes a folder of builds an engine version. The version is project(VERSION). The commit, whether the tree had changes and the commit's date are read from git as each build starts, by CMake/TrinityWriteVersion.cmake, which rewrites the engine's TrinityBuildInfo.hpp only when they change, so a commit made after configuring is never missed and only Version.cpp compiles again. Once a configuration's programs are built, the same script writes Trinity.trversion beside the configurations' folders, so a build tree's bin is a version too
set(TRINITY_MANIFEST_FORMAT 1)
set(TRINITY_MANIFEST_FILE "Trinity.trversion")

# The file formats this version reads and writes, which Hub compares between versions. Version.cpp checks each against the engine's own constant, so raising one there fails to compile until it is raised here too
set(TRINITY_FORMAT_PROJECT 10)
set(TRINITY_FORMAT_SCENE 2)
set(TRINITY_FORMAT_PREFAB 2)
set(TRINITY_FORMAT_INPUT_ACTIONS 2)
set(TRINITY_FORMAT_AUDIO_MIXER 1)
set(TRINITY_FORMAT_SOUND_EVENT 1)
set(TRINITY_FORMAT_META 2)
set(TRINITY_FORMAT_PACK 1)

find_package(Git QUIET)

# The .NET SDK a project's C# needs with this version, as global.json pins it, or none when the version was built without C#
set(TRINITY_VERSION_DOTNET_SDK "")
set(TRINITY_VERSION_DOTNET_ROLL_FORWARD "")
if(TRINITY_DOTNET_FOUND)
    file(READ "${CMAKE_SOURCE_DIR}/global.json" global_json)
    string(JSON TRINITY_VERSION_DOTNET_SDK ERROR_VARIABLE json_error GET "${global_json}" sdk version)
    if(json_error)
        message(FATAL_ERROR "global.json names no sdk version: ${json_error}")
    endif()

    string(JSON TRINITY_VERSION_DOTNET_ROLL_FORWARD ERROR_VARIABLE json_error GET "${global_json}" sdk rollForward)
    if(json_error)
        set(TRINITY_VERSION_DOTNET_ROLL_FORWARD "")
    endif()
endif()

# Each folder of Templates holding a Template.yaml, by its name
file(GLOB template_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Templates/*/Template.yaml")
set(TRINITY_VERSION_TEMPLATES "")
foreach(template_file IN LISTS template_files)
    cmake_path(GET template_file PARENT_PATH template_folder)
    cmake_path(GET template_folder FILENAME template_name)
    list(APPEND TRINITY_VERSION_TEMPLATES "${template_name}")
endforeach()
list(SORT TRINITY_VERSION_TEMPLATES)

# Every configuration this tree can build, of which the manifest names those whose player is there
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(is_multi_config)
    set(TRINITY_VERSION_CONFIGURATIONS ${CMAKE_CONFIGURATION_TYPES})
else()
    set(TRINITY_VERSION_CONFIGURATIONS ${CMAKE_BUILD_TYPE})
endif()

set(TRINITY_VERSION_DIRECTORY "${CMAKE_BINARY_DIR}/Version")
set(TRINITY_VERSION_SETTINGS "${TRINITY_VERSION_DIRECTORY}/TrinityVersionSettings.cmake")
set(TRINITY_VERSION_SCRIPT "${CMAKE_SOURCE_DIR}/CMake/TrinityWriteVersion.cmake")

file(CONFIGURE OUTPUT "${TRINITY_VERSION_SETTINGS}" CONTENT [==[# Written by CMake/TrinityVersion.cmake as Trinity configures, for CMake/TrinityWriteVersion.cmake
set(TRINITY_SOURCE_DIRECTORY [=[@CMAKE_SOURCE_DIR@]=])
set(TRINITY_GIT [=[@GIT_EXECUTABLE@]=])
set(TRINITY_VERSION_DIRECTORY [=[@TRINITY_VERSION_DIRECTORY@]=])
if(NOT DEFINED TRINITY_VERSION_ROOT)
    set(TRINITY_VERSION_ROOT [=[@TRINITY_BIN_DIRECTORY@]=])
endif()
set(TRINITY_VERSION "@PROJECT_VERSION@")
set(TRINITY_PROGRAM_FOLDER "@PROJECT_NAME@")
set(TRINITY_PLAYER_FILE "Trinity-Runtime@CMAKE_EXECUTABLE_SUFFIX@")
set(TRINITY_MANIFEST_FORMAT @TRINITY_MANIFEST_FORMAT@)
set(TRINITY_MANIFEST_FILE "@TRINITY_MANIFEST_FILE@")
set(TRINITY_VERSION_PLATFORM "@TRINITY_PLATFORM@")
set(TRINITY_VERSION_SYSTEM "@CMAKE_SYSTEM_NAME@")
set(TRINITY_VERSION_ARCHITECTURE "@ARCH_NAME@")
set(TRINITY_VERSION_CONFIGURATIONS "@TRINITY_VERSION_CONFIGURATIONS@")
set(TRINITY_VERSION_COMPILER_ID "@CMAKE_CXX_COMPILER_ID@")
set(TRINITY_VERSION_COMPILER_VERSION "@CMAKE_CXX_COMPILER_VERSION@")
set(TRINITY_VERSION_DOTNET_SDK "@TRINITY_VERSION_DOTNET_SDK@")
set(TRINITY_VERSION_DOTNET_ROLL_FORWARD "@TRINITY_VERSION_DOTNET_ROLL_FORWARD@")
set(TRINITY_VERSION_TEMPLATES [=[@TRINITY_VERSION_TEMPLATES@]=])
set(TRINITY_FORMAT_PROJECT @TRINITY_FORMAT_PROJECT@)
set(TRINITY_FORMAT_SCENE @TRINITY_FORMAT_SCENE@)
set(TRINITY_FORMAT_PREFAB @TRINITY_FORMAT_PREFAB@)
set(TRINITY_FORMAT_INPUT_ACTIONS @TRINITY_FORMAT_INPUT_ACTIONS@)
set(TRINITY_FORMAT_AUDIO_MIXER @TRINITY_FORMAT_AUDIO_MIXER@)
set(TRINITY_FORMAT_SOUND_EVENT @TRINITY_FORMAT_SOUND_EVENT@)
set(TRINITY_FORMAT_META @TRINITY_FORMAT_META@)
set(TRINITY_FORMAT_PACK @TRINITY_FORMAT_PACK@)
]==] @ONLY)

# Runs as every build starts, before the engine compiles. TrinityBuildInfo.hpp is written for each configuration apart, so no two configurations' builds write one file
add_custom_target(Trinity-Version
    COMMAND "${CMAKE_COMMAND}" "-DTRINITY_VERSION_SETTINGS=${TRINITY_VERSION_SETTINGS}" -DTRINITY_VERSION_STAGE=BuildInfo "-DTRINITY_VERSION_CONFIGURATION=$<CONFIG>" -P "${TRINITY_VERSION_SCRIPT}"
    BYPRODUCTS "${TRINITY_VERSION_DIRECTORY}/$<CONFIG>/TrinityBuildInfo.hpp" "${TRINITY_VERSION_DIRECTORY}/$<CONFIG>/TrinityBuildInfo.cmake"
    COMMENT "Reading the commit Trinity is built from"
    VERBATIM
)

# The engine target's Version.cpp includes TrinityBuildInfo.hpp
function(trinity_use_version_info target)
    add_dependencies(${target} Trinity-Version)
    target_include_directories(${target} PRIVATE "${TRINITY_VERSION_DIRECTORY}/$<CONFIG>")
endfunction()

# Writes Trinity.trversion once every given target of the configuration is built, naming each configuration whose player is there
function(trinity_add_version_manifest target)
    add_custom_target(${target} ALL
        COMMAND "${CMAKE_COMMAND}" "-DTRINITY_VERSION_SETTINGS=${TRINITY_VERSION_SETTINGS}" -DTRINITY_VERSION_STAGE=Manifest "-DTRINITY_VERSION_CONFIGURATION=$<CONFIG>" -P "${TRINITY_VERSION_SCRIPT}"
        COMMENT "Writing ${TRINITY_MANIFEST_FILE}"
        VERBATIM
    )
    add_dependencies(${target} Trinity-Version ${ARGN})
endfunction()