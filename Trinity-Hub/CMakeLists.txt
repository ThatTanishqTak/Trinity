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
# yaml-cpp reads and writes Hub's own files, as the engine's are, stb_image, header-only, reads the PNG thumbnails of the templates, and Trinity-Tools finds the tools each version's projects build with, as its Forge does
target_link_libraries(${TARGET_NAME} PRIVATE Trinity::Engine Trinity::Tools yaml-cpp::yaml-cpp stb::stb)
# Hub draws its pages with the engine's ImGui, whose text FreeType renders, which the check finds where they are, and carries none of the editor's other libraries, nor Slang
trinity_check_editor_libraries(${TARGET_NAME} absent ${TRINITY_EDITOR_LIBRARIES} ImGuizmo BasisEncoder Slang)
trinity_check_editor_libraries(${TARGET_NAME} present ImGui FreeType)
# Hub's zip is unpacked where nothing else is installed, so after each Distribution build it must import no DLL but Windows' own, as a player
trinity_check_imports(${TARGET_NAME})