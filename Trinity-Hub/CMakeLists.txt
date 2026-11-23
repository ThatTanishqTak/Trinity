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
# Hub's own version, which the root CMakeLists.txt names beside the engine's
target_compile_definitions(${TARGET_NAME} PRIVATE TR_HUB_VERSION="${TRINITY_HUB_VERSION}")
# yaml-cpp for Hub's files, stb_image for the thumbnails, Trinity-Tools to find build tools
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine Trinity::Tools yaml-cpp::yaml-cpp stb::stb)
# Hub draws with the engine's ImGui and FreeType, which the check must find, and nothing else
trinity_check_editor_libraries(${TARGET_NAME} absent ${TRINITY_EDITOR_LIBRARIES} ImGuizmo BasisEncoder Slang)
trinity_check_editor_libraries(${TARGET_NAME} present ImGui FreeType)
# Hub's zip is unpacked where nothing is installed, so it must import no DLL but Windows' own
trinity_check_imports(${TARGET_NAME})