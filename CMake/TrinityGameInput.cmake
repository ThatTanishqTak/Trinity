set(TRINITY_GAMEINPUT_FOUND OFF)

if(TRINITY_PLATFORM STREQUAL "Windows")
    file(STRINGS "${CMAKE_SOURCE_DIR}/Scripts/GameInput.lock" TRINITY_GAMEINPUT_LINE REGEX "^Microsoft\\.GameInput ")
    string(REGEX REPLACE "^[^ ]+ ([0-9.]+) .*$" "\\1" TRINITY_GAMEINPUT_VERSION "${TRINITY_GAMEINPUT_LINE}")
    set(TRINITY_GAMEINPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Vendor/GameInput/${TRINITY_GAMEINPUT_VERSION}/native")

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(TRINITY_GAMEINPUT_ARCHITECTURE "arm64")
    else()
        set(TRINITY_GAMEINPUT_ARCHITECTURE "x64")
    endif()

    if(EXISTS "${TRINITY_GAMEINPUT_DIRECTORY}/include/GameInput.h" AND EXISTS "${TRINITY_GAMEINPUT_DIRECTORY}/lib/${TRINITY_GAMEINPUT_ARCHITECTURE}/GameInput.lib")
        set(TRINITY_GAMEINPUT_FOUND ON)
    else()
        message(WARNING "GameInput ${TRINITY_GAMEINPUT_VERSION} is missing from Vendor/GameInput. Run Scripts/Bootstrap.ps1; until then the engine is built without gamepads.")
    endif()
endif()

# The engine compiles its Windows gamepads against GameInput's header, and links the package's loader, which finds the GameInput runtime installed on the machine as the engine starts. Nothing is copied beside the executables
function(trinity_link_gameinput target)
    if(TRINITY_GAMEINPUT_FOUND)
        target_include_directories(${target} SYSTEM PRIVATE "${TRINITY_GAMEINPUT_DIRECTORY}/include")
        target_link_libraries(${target} PRIVATE "${TRINITY_GAMEINPUT_DIRECTORY}/lib/${TRINITY_GAMEINPUT_ARCHITECTURE}/GameInput.lib")
        target_compile_definitions(${target} PRIVATE TR_GAMEINPUT)
    endif()
endfunction()