##--- Editor ---##

project(TrinityForge LANGUAGES CXX)

# Editor source files
file(GLOB_RECURSE EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

# Build .exe
add_executable(${PROJECT_NAME} ${EDITOR_SOURCES})

# Turn the console window off in release build
target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
    $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
)

# Inlcude directory of the engine
target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/Trinity/src
)

# Link the engine to the editor
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Trinity
)