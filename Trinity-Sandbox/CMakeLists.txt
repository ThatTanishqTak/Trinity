# Trinity-Sandbox CMakeLists.txt

set(TARGET_NAME Trinity-Sandbox)

# Collect the source files
file(GLOB_RECURSE TRINITY_SANDBOX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE TRINITY_SANDBOX_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

add_executable(${TARGET_NAME} ${TRINITY_SANDBOX_SOURCES} ${TRINITY_SANDBOX_HEADERS})

trinity_configure_executable(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_SANDBOX_HEADERS} ${TRINITY_SANDBOX_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE Include)
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine)

# Loaded and unloaded by Sandbox to check that a module shares the engine and leaves nothing behind
trinity_add_module(Trinity-Sandbox-Module "Module/SandboxModule.cpp")
add_dependencies(${TARGET_NAME} Trinity-Sandbox-Module)
target_compile_definitions(${TARGET_NAME} PRIVATE TR_SANDBOX_MODULE="$<TARGET_FILE_NAME:Trinity-Sandbox-Module>")