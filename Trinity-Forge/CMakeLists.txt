# Trinity-Forge CMakeLists.txt

set(TARGET_NAME Trinity-Forge)

# Collect the source files
file(GLOB_RECURSE TRINITY_FORGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE TRINITY_FORGE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

add_executable(${TARGET_NAME} ${TRINITY_FORGE_SOURCES} ${TRINITY_FORGE_HEADERS})

trinity_configure_executable(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_FORGE_HEADERS} ${TRINITY_FORGE_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE Include)
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine ktx stb::stb)

# The texture importer encodes, so Forge takes the whole of libktx, and a static engine links it instead of ktx_read
set_target_properties(${TARGET_NAME} PROPERTIES TRINITY_TEXTURE_ENCODER ON)

# --import-test copies these into a project, so the importer always meets the same images
target_compile_definitions(${TARGET_NAME} PRIVATE TR_FORGE_TEST_TEXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Textures")