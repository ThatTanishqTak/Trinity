##--- Launcher ---##

project(TrinityCentre LANGUAGES CXX)

file(GLOB_RECURSE LAUNCHER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

add_executable(${PROJECT_NAME} ${LAUNCHER_SOURCES})

target_link_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
    $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
)

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/Trinity/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE Trinity)