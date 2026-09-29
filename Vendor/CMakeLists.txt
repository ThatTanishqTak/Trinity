# Vendor CMakeLists.txt

# spdlog -------
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_USE_STD_FORMAT ON)
set(SPDLOG_BUILD_EXAMPLE_HO OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_TESTS_HO OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_BUILD_WARNINGS OFF)
set(SPDLOG_ENABLE_PCH OFF)
set(SPDLOG_INSTALL OFF)

add_subdirectory(spdlog SYSTEM)
# --------------

# GameInput -------
if(WIN32)
    set(GAMEINPUT_VERSION "v3.3.195" CACHE STRING "GameInput release folder under Vendor/GameInput/releases")
    set(GAMEINPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/GameInput/releases/${GAMEINPUT_VERSION}")

    if(ARCH_NAME STREQUAL "arm64")
        set(GAMEINPUT_ARCH "arm64")
    elseif(ARCH_NAME STREQUAL "x86_64")
        set(GAMEINPUT_ARCH "x64")
    else()
        message(FATAL_ERROR "GameInput has no ${ARCH_NAME} library in Vendor/GameInput, build for x64 or arm64")
    endif()

    find_path(GAMEINPUT_INCLUDE_DIR
        NAMES GameInput.h
        PATHS "${GAMEINPUT_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}/GameInput"
        PATH_SUFFIXES include native/include
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_library(GAMEINPUT_LIBRARY
        NAMES GameInput gameinput
        PATHS "${GAMEINPUT_ROOT}"
        PATH_SUFFIXES lib/${GAMEINPUT_ARCH} lib/native/${GAMEINPUT_ARCH} lib/native/lib/${GAMEINPUT_ARCH} lib
        NO_DEFAULT_PATH
        REQUIRED
    )

    # Players need the GameInput runtime
    find_file(GAMEINPUT_REDIST
        NAMES GameInputRedist.msi
        PATHS "${GAMEINPUT_ROOT}"
        PATH_SUFFIXES redist
        NO_DEFAULT_PATH
    )

    if(NOT GAMEINPUT_REDIST)
        message(WARNING "GameInputRedist.msi not found under ${GAMEINPUT_ROOT}/redist, it won't be copied to the output folder")
    endif()

    add_library(GameInput INTERFACE)
    target_include_directories(GameInput SYSTEM INTERFACE "${GAMEINPUT_INCLUDE_DIR}")
    target_link_libraries(GameInput INTERFACE "${GAMEINPUT_LIBRARY}")
endif()
# --------------