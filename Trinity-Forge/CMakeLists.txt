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

# The editor's own shaders, such as the Viewport's grid, are compiled beside the engine's under /engine/shaders
file(GLOB TRINITY_FORGE_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang")
trinity_add_shaders(Trinity-Forge-Shaders ${TRINITY_FORGE_SHADERS})
add_dependencies(${TARGET_NAME} Trinity-Forge-Shaders)

trinity_configure_executable(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_FORGE_HEADERS} ${TRINITY_FORGE_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE Include)
# The model importer's glTF, FBX, OBJ and COLLADA reading and mesh ordering stay in the editor, so no game links them
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine ktx stb::stb ImGuizmo::ImGuizmo fastgltf::fastgltf meshoptimizer assimp::assimp)
trinity_check_editor_libraries(${TARGET_NAME} present)

# ImGuizmo's own code keeps its own warnings, and reads ImGui before any precompiled header could
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/ImGuizmoUnit.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")

# The texture importer encodes, so Forge takes the whole of libktx, and a static engine links it instead of ktx_read
set_target_properties(${TARGET_NAME} PROPERTIES TRINITY_TEXTURE_ENCODER ON)

# --import-test copies these into a project, so the importer always meets the same images, the models Scripts/FetchSamples fetches when they are there, and assimp's own test models in FBX, OBJ and COLLADA
target_compile_definitions(${TARGET_NAME} PRIVATE TR_FORGE_TEST_TEXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Textures" TR_FORGE_TEST_MODELS="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Models" TR_FORGE_TEST_ASSIMP="${CMAKE_SOURCE_DIR}/Vendor/assimp/test")