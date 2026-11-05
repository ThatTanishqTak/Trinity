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

# The package's redistributable, which installs the runtime, goes to Redist in Distribution's folder, where the players are, for Forge's export to put beside a game, as Microsoft asks a game that uses GameInput to ship it. The copy runs in Distribution alone, as no other configuration's folder is exported from. The first command runs in every configuration, since one whose commands all come to nothing leaves Ninja only the cd to the working folder, which on Windows it runs without cmd.exe and so cannot start
function(trinity_add_gameinput_redist target)
    set(redist "${CMAKE_SOURCE_DIR}/Vendor/GameInput/${TRINITY_GAMEINPUT_VERSION}/redist/GameInputRedist.msi")

    if(TRINITY_GAMEINPUT_FOUND AND EXISTS "${redist}")
        set(directory "${OUTPUT_BASE}/Redist")
        add_custom_target(${target} ALL
            COMMAND "${CMAKE_COMMAND}" -E true
            COMMAND "$<$<CONFIG:Distribution>:${CMAKE_COMMAND};-E;make_directory;${directory}>"
            COMMAND "$<$<CONFIG:Distribution>:${CMAKE_COMMAND};-E;copy_if_different;${redist};${directory}/GameInputRedist.msi>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    else()
        add_custom_target(${target})
    endif()
endfunction()