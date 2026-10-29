# Trinity-Runtime CMakeLists.txt

# Collect the source files
file(GLOB_RECURSE TRINITY_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE TRINITY_RUNTIME_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

# The player's main and its application, as objects, so a player links every one of them, main and the Agility SDK's exports included, whatever it references. The engine is linked privately, so a player takes it to link and nothing else, and on Windows compiles the engine's global new and delete once, in these objects
add_library(Trinity-RuntimeMain OBJECT ${TRINITY_RUNTIME_SOURCES} ${TRINITY_RUNTIME_HEADERS})

trinity_configure_target(Trinity-RuntimeMain)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_RUNTIME_HEADERS} ${TRINITY_RUNTIME_SOURCES})

target_include_directories(Trinity-RuntimeMain PRIVATE Include)
target_link_libraries(Trinity-RuntimeMain PRIVATE Trinity::Engine)

# The player for a project without C++, built in every configuration: Distribution's links the static engine and the static C runtime, and Debug and Release's the shared engine, loading the project's module as Forge does
add_executable(Trinity-Runtime)

trinity_configure_executable(Trinity-Runtime)
target_link_libraries(Trinity-Runtime PRIVATE Trinity-RuntimeMain)
trinity_check_editor_libraries(Trinity-Runtime absent)