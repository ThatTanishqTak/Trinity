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

# Shader compilation -----------------------------------------------------
# Gather all GLSL sources in the Assets/Shaders directory
file(GLOB_RECURSE FORGE_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders/*.frag
)

if(DEFINED ENV{VULKAN_SDK})
    set(GLSLANG_VALIDATOR $ENV{VULKAN_SDK}/Bin/glslangValidator)
else()
    message(FATAL_CRITICAL "VULKAN_SDK environment variable not set")
endif()

set(FORGE_SHADER_OUTPUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Assets/Shaders)
file(MAKE_DIRECTORY ${FORGE_SHADER_OUTPUT_DIR})

foreach(SHADER ${FORGE_SHADER_SOURCES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPV ${FORGE_SHADER_OUTPUT_DIR}/${FILE_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILE_NAME}"
    )
    list(APPEND FORGE_SHADER_BINARIES ${SPV})
endforeach()

add_custom_target(ForgeShaders ALL DEPENDS ${FORGE_SHADER_BINARIES})
add_dependencies(${PROJECT_NAME} ForgeShaders)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Trinity/vendor/assimp/bin/${ARCH}/assimp-vc143-mt.dll
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
        ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Shaders
        ${FORGE_SHADER_OUTPUT_DIR}
    COMMENT "Copying assimp-vc143-mt.dll to output directory"
)

#add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E copy_if_different
#    
#    
#    COMMENT "Copying shaders to output directory"
#)