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

# The editor's own shaders, such as the Viewport's grid, compile beside the engine's
file(GLOB TRINITY_FORGE_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang")
trinity_add_shaders(Trinity-Forge-Shaders ${TRINITY_FORGE_SHADERS})
add_dependencies(${TARGET_NAME} Trinity-Forge-Shaders)

# The export leaves out shaders no game draws with, by name: these and the RHI's tests
get_property(TRINITY_UNSHIPPED_SHADERS GLOBAL PROPERTY TRINITY_TEST_SHADERS)
foreach(shader IN LISTS TRINITY_FORGE_SHADERS)
    cmake_path(GET shader STEM name)
    list(APPEND TRINITY_UNSHIPPED_SHADERS "${name}")
endforeach()
list(JOIN TRINITY_UNSHIPPED_SHADERS "," TRINITY_UNSHIPPED_SHADERS)
target_compile_definitions(${TARGET_NAME} PRIVATE TR_UNSHIPPED_SHADERS="${TRINITY_UNSHIPPED_SHADERS}")

trinity_configure_executable(${TARGET_NAME})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_FORGE_HEADERS} ${TRINITY_FORGE_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE Include)
# The model importer stays in the editor, so no game links it. Build tools are found as Hub does
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine Trinity::Tools ktx zstd::headers stb::stb ImGuizmo::ImGuizmo fastgltf::fastgltf meshoptimizer assimp::assimp)
trinity_check_editor_libraries(${TARGET_NAME} present ${TRINITY_EDITOR_LIBRARIES} ImGui ImGuizmo BasisEncoder)

# ImGuizmo keeps its warnings. MSVC hides them with /external:W0, and /W0 would itself warn
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/ImGuizmoUnit.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>")

# Forge builds a project's C++ module with its own compiler, and finds its Visual Studio by path
target_compile_definitions(${TARGET_NAME} PRIVATE TR_FORGE_CXX_COMPILER="${CMAKE_CXX_COMPILER}")

# The texture importer encodes, so Forge links all of libktx in place of ktx_read
set_target_properties(${TARGET_NAME} PROPERTIES TRINITY_TEXTURE_ENCODER ON)

# --import-test copies these into a project: images, the fetched samples, assimp's test models
target_compile_definitions(${TARGET_NAME} PRIVATE TR_FORGE_TEST_TEXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Textures" TR_FORGE_TEST_MODELS="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Models" TR_FORGE_TEST_ASSIMP="${CMAKE_SOURCE_DIR}/Vendor/assimp/test")