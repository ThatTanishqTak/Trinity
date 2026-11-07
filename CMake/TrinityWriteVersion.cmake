# Run with cmake -P by the build, given TRINITY_VERSION_SETTINGS, which CMake/TrinityVersion.cmake writes, TRINITY_VERSION_CONFIGURATION and TRINITY_VERSION_STAGE. BuildInfo, as a build starts, reads from git the commit Trinity is built from, whether the tree has changes, and the commit's date, and writes them with the version into TrinityBuildInfo.hpp for Version.cpp. Manifest, once the configuration's programs are built, writes Trinity.trversion into TRINITY_VERSION_ROOT, which is bin unless given, naming each configuration whose folder there holds its player. Each file is written only when its text changes, so nothing builds again for a build at the same commit
cmake_minimum_required(VERSION 3.30...4.2)

include("${TRINITY_VERSION_SETTINGS}")

set(build_info_directory "${TRINITY_VERSION_DIRECTORY}/${TRINITY_VERSION_CONFIGURATION}")

function(trinity_write_if_different path text)
    if(EXISTS "${path}")
        file(READ "${path}" existing)
        if(existing STREQUAL text)
            return()
        endif()
    endif()

    file(WRITE "${path}" "${text}")
endfunction()

# A double-quoted YAML scalar, escaped as yaml-cpp's emitter escapes one
function(trinity_yaml_quote out text)
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    set(${out} "\"${text}\"" PARENT_SCOPE)
endfunction()

# A flow sequence, as yaml-cpp writes one: [Debug, Release], or [] when empty, each name quoted when asked
function(trinity_yaml_sequence out quoted)
    set(items "")
    foreach(name IN LISTS ARGN)
        if(quoted)
            trinity_yaml_quote(name "${name}")
        endif()
        list(APPEND items "${name}")
    endforeach()

    list(JOIN items ", " joined)
    set(${out} "[${joined}]" PARENT_SCOPE)
endfunction()

# A C++ string literal
function(trinity_cpp_quote out text)
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    set(${out} "\"${text}\"" PARENT_SCOPE)
endfunction()

if(TRINITY_VERSION_STAGE STREQUAL "BuildInfo")
    # A tree that is not a git checkout, or a git that cannot read it, gives no commit and no date. Untracked files count as changes, since the build globs its sources, and so do submodules at another commit or with tracked files changed. --no-optional-locks keeps git from taking the index's lock while another git uses it
    set(commit "")
    set(modified OFF)
    set(date "")
    if(TRINITY_GIT)
        execute_process(COMMAND "${TRINITY_GIT}" -C "${TRINITY_SOURCE_DIRECTORY}" rev-parse --verify --quiet HEAD OUTPUT_VARIABLE head RESULT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(result EQUAL 0 AND head MATCHES "^[0-9a-f]+$")
            set(commit "${head}")
            execute_process(COMMAND "${TRINITY_GIT}" -C "${TRINITY_SOURCE_DIRECTORY}" --no-optional-locks status --porcelain --ignore-submodules=untracked OUTPUT_VARIABLE changes RESULT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT result EQUAL 0 OR NOT changes STREQUAL "")
                set(modified ON)
            endif()

            # The commit's time as seconds, which string(TIMESTAMP) turns into a UTC date through SOURCE_DATE_EPOCH, the same on every machine
            execute_process(COMMAND "${TRINITY_GIT}" -C "${TRINITY_SOURCE_DIRECTORY}" show --no-patch --format=%ct HEAD OUTPUT_VARIABLE seconds RESULT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(result EQUAL 0 AND seconds MATCHES "^[0-9]+$")
                set(ENV{SOURCE_DATE_EPOCH} "${seconds}")
                string(TIMESTAMP date "%Y-%m-%dT%H:%M:%SZ" UTC)
            endif()
        endif()
    endif()

    if(modified)
        set(modified_number 1)
    else()
        set(modified_number 0)
    endif()

    trinity_cpp_quote(version_literal "${TRINITY_VERSION}")
    trinity_cpp_quote(commit_literal "${commit}")
    trinity_cpp_quote(date_literal "${date}")
    trinity_write_if_different("${build_info_directory}/TrinityBuildInfo.hpp" "// Written by CMake/TrinityWriteVersion.cmake as each build starts, from project(VERSION) and git\n#pragma once\n\n#define TR_BUILD_VERSION ${version_literal}\n#define TR_BUILD_COMMIT ${commit_literal}\n#define TR_BUILD_MODIFIED ${modified_number}\n#define TR_BUILD_DATE ${date_literal}\n\n#define TR_FORMAT_PROJECT ${TRINITY_FORMAT_PROJECT}\n#define TR_FORMAT_SCENE ${TRINITY_FORMAT_SCENE}\n#define TR_FORMAT_PREFAB ${TRINITY_FORMAT_PREFAB}\n#define TR_FORMAT_INPUT_ACTIONS ${TRINITY_FORMAT_INPUT_ACTIONS}\n#define TR_FORMAT_AUDIO_MIXER ${TRINITY_FORMAT_AUDIO_MIXER}\n#define TR_FORMAT_SOUND_EVENT ${TRINITY_FORMAT_SOUND_EVENT}\n#define TR_FORMAT_META ${TRINITY_FORMAT_META}\n#define TR_FORMAT_PACK ${TRINITY_FORMAT_PACK}\n")
    trinity_write_if_different("${build_info_directory}/TrinityBuildInfo.cmake" "set(TRINITY_BUILD_COMMIT \"${commit}\")\nset(TRINITY_BUILD_MODIFIED ${modified})\nset(TRINITY_BUILD_DATE \"${date}\")\n")
elseif(TRINITY_VERSION_STAGE STREQUAL "Manifest")
    if(NOT EXISTS "${build_info_directory}/TrinityBuildInfo.cmake")
        message(FATAL_ERROR "${build_info_directory}/TrinityBuildInfo.cmake is missing. Build the Trinity-Version target first")
    endif()

    include("${build_info_directory}/TrinityBuildInfo.cmake")

    set(configurations "")
    foreach(configuration IN LISTS TRINITY_VERSION_CONFIGURATIONS)
        if(EXISTS "${TRINITY_VERSION_ROOT}/${configuration}-${TRINITY_VERSION_SYSTEM}-${TRINITY_VERSION_ARCHITECTURE}/${TRINITY_PROGRAM_FOLDER}/${TRINITY_PLAYER_FILE}")
            list(APPEND configurations "${configuration}")
        endif()
    endforeach()

    if(TRINITY_BUILD_MODIFIED)
        set(modified "true")
    else()
        set(modified "false")
    endif()

    # The text EngineManifest's writer makes, key for key, so a manifest the build writes reads back and saves again as the same bytes
    trinity_yaml_quote(version "${TRINITY_VERSION}")
    trinity_yaml_quote(commit "${TRINITY_BUILD_COMMIT}")
    trinity_yaml_quote(date "${TRINITY_BUILD_DATE}")
    trinity_yaml_sequence(exports OFF "${TRINITY_VERSION_PLATFORM}")
    trinity_yaml_sequence(configurations OFF ${configurations})
    trinity_yaml_quote(compiler_id "${TRINITY_VERSION_COMPILER_ID}")
    trinity_yaml_quote(compiler_version "${TRINITY_VERSION_COMPILER_VERSION}")
    trinity_yaml_quote(dotnet_sdk "${TRINITY_VERSION_DOTNET_SDK}")
    trinity_yaml_quote(dotnet_roll_forward "${TRINITY_VERSION_DOTNET_ROLL_FORWARD}")
    trinity_yaml_sequence(templates ON ${TRINITY_VERSION_TEMPLATES})

    string(CONCAT manifest
        "Format: ${TRINITY_MANIFEST_FORMAT}\n"
        "Version: ${version}\n"
        "Commit: ${commit}\n"
        "Modified: ${modified}\n"
        "Date: ${date}\n"
        "Platform: ${TRINITY_VERSION_PLATFORM}\n"
        "System: ${TRINITY_VERSION_SYSTEM}\n"
        "Architecture: ${TRINITY_VERSION_ARCHITECTURE}\n"
        "Exports: ${exports}\n"
        "Configurations: ${configurations}\n"
        "Compiler:\n"
        "  Id: ${compiler_id}\n"
        "  Version: ${compiler_version}\n"
        "DotNet:\n"
        "  Sdk: ${dotnet_sdk}\n"
        "  RollForward: ${dotnet_roll_forward}\n"
        "Formats:\n"
        "  Project: ${TRINITY_FORMAT_PROJECT}\n"
        "  Scene: ${TRINITY_FORMAT_SCENE}\n"
        "  Prefab: ${TRINITY_FORMAT_PREFAB}\n"
        "  InputActions: ${TRINITY_FORMAT_INPUT_ACTIONS}\n"
        "  AudioMixer: ${TRINITY_FORMAT_AUDIO_MIXER}\n"
        "  SoundEvent: ${TRINITY_FORMAT_SOUND_EVENT}\n"
        "  Meta: ${TRINITY_FORMAT_META}\n"
        "  Pack: ${TRINITY_FORMAT_PACK}\n"
        "Templates: ${templates}\n"
    )

    file(MAKE_DIRECTORY "${TRINITY_VERSION_ROOT}")
    trinity_write_if_different("${TRINITY_VERSION_ROOT}/${TRINITY_MANIFEST_FILE}" "${manifest}")
else()
    message(FATAL_ERROR "TRINITY_VERSION_STAGE must be BuildInfo or Manifest, not '${TRINITY_VERSION_STAGE}'")
endif()