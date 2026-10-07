# Trinity-Hub CMakeLists.txt

set(TARGET_NAME Trinity-Hub)

# Collect the source files
file(GLOB_RECURSE TRINITY_HUB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE TRINITY_HUB_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

add_executable(${TARGET_NAME} ${TRINITY_HUB_SOURCES} ${TRINITY_HUB_HEADERS})

trinity_configure_executable(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_HUB_HEADERS} ${TRINITY_HUB_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE Include)
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine)
trinity_check_editor_libraries(${TARGET_NAME} absent)