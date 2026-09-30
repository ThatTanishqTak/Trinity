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

# Vulkan-Headers -------
set(VULKAN_HEADERS_ENABLE_TESTS OFF)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF)
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)

add_subdirectory(Vulkan-Headers SYSTEM)
# --------------

# volk -------
add_library(volk STATIC "${CMAKE_CURRENT_SOURCE_DIR}/volk/volk.c")
target_include_directories(volk SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/volk")
target_link_libraries(volk PUBLIC Vulkan::Headers PRIVATE ${CMAKE_DL_LIBS})

target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES $<$<BOOL:${WIN32}>:VK_USE_PLATFORM_WIN32_KHR>)
# --------------

# VulkanMemoryAllocator -------
add_library(VulkanMemoryAllocator INTERFACE)
target_include_directories(VulkanMemoryAllocator SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator/include")
target_link_libraries(VulkanMemoryAllocator INTERFACE volk)
target_compile_definitions(VulkanMemoryAllocator INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)
# --------------

# D3D12MemoryAllocator -------
if(WIN32)
    add_library(D3D12MemoryAllocator STATIC "${CMAKE_CURRENT_SOURCE_DIR}/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp")
    target_include_directories(D3D12MemoryAllocator SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/D3D12MemoryAllocator/include")
    target_link_libraries(D3D12MemoryAllocator PUBLIC d3d12 dxgi dxguid)
    target_compile_features(D3D12MemoryAllocator PRIVATE cxx_std_17)
endif()
# --------------