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