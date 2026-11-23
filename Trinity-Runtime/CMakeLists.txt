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

# The player's main as objects, so a player links them all, the Agility SDK's exports included
add_library(Trinity-RuntimeMain OBJECT ${TRINITY_RUNTIME_SOURCES} ${TRINITY_RUNTIME_HEADERS})

trinity_configure_target(Trinity-RuntimeMain)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TRINITY_RUNTIME_HEADERS} ${TRINITY_RUNTIME_SOURCES})

target_include_directories(Trinity-RuntimeMain PRIVATE Include)
target_link_libraries(Trinity-RuntimeMain PRIVATE Trinity::Engine)

# The player for a project without C++: static in Distribution, on the shared engine otherwise
add_executable(Trinity-Runtime)

trinity_configure_executable(Trinity-Runtime)
target_link_libraries(Trinity-Runtime PRIVATE Trinity-RuntimeMain)
trinity_check_player(Trinity-Runtime)

# The package a Distribution player builds against, with these objects as its main
trinity_write_game_package(Trinity-Engine-Static Trinity-RuntimeMain)