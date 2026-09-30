# Trinity-Forge CMakeLists.txt

set(TARGET_NAME Trinity-Forge)

# Collect the source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)

# Collect the public header files
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Include/*.hpp"
)

# Create the executable
add_executable(${TARGET_NAME} ${SOURCES} ${HEADERS})

# IDE source layout
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES ${SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Include" PREFIX "Include" FILES ${HEADERS})

# Include directory
target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Include")

# Linking executable against the engine
target_link_libraries(${TARGET_NAME} PRIVATE Trinity-Engine)

# Warnings and platform defines
trinity_set_compile_options(${TARGET_NAME})

# Release builds open without a console window
if(MSVC)
    target_link_options(${TARGET_NAME} PRIVATE
        "$<$<CONFIG:Release,MinSizeRel>:/SUBSYSTEM:WINDOWS>"
        "$<$<CONFIG:Release,MinSizeRel>:/ENTRY:mainCRTStartup>"
    )
endif()

# Runtime files players need, such as the GameInput redistributable
trinity_copy_runtime_files(${TARGET_NAME})

# Shaders
trinity_add_shaders(${TARGET_NAME} SOURCE "Shaders/Triangle.slang" ENTRIES VertexMain:vertex FragmentMain:fragment)

# Forge properties
set_target_properties(${TARGET_NAME} PROPERTIES
    # Set output directory
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BASE}"

    # Set C++ standard
    CXX_EXTENSIONS OFF

    # Set debugger working directory
    VS_DEBUGGER_WORKING_DIRECTORY "${OUTPUT_BASE}"
)