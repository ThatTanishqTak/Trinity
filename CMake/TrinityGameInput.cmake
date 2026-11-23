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
        message(WARNING "GameInput ${TRINITY_GAMEINPUT_VERSION} is missing from Vendor/GameInput, so the engine is built without gamepads. Run Scripts/Bootstrap.ps1")
    endif()
endif()

# The engine compiles against GameInput's header and links its loader, which finds the runtime
function(trinity_link_gameinput target)
    if(TRINITY_GAMEINPUT_FOUND)
        target_include_directories(${target} SYSTEM PRIVATE "${TRINITY_GAMEINPUT_DIRECTORY}/include")
        target_link_libraries(${target} PRIVATE "${TRINITY_GAMEINPUT_DIRECTORY}/lib/${TRINITY_GAMEINPUT_ARCHITECTURE}/GameInput.lib")
        target_compile_definitions(${target} PRIVATE TR_GAMEINPUT)
        # The game package names it for the static engine, so TrinityPackage.cmake copies it
        set_property(GLOBAL APPEND PROPERTY TRINITY_PACKAGE_LIBRARIES "${TRINITY_GAMEINPUT_DIRECTORY}/lib/${TRINITY_GAMEINPUT_ARCHITECTURE}/GameInput.lib")
    endif()
endfunction()

# The installer goes to Redist for exports. Ninja on Windows cannot start a command of only a cd
function(trinity_add_gameinput_redist target)
    set(redist "${CMAKE_SOURCE_DIR}/Vendor/GameInput/${TRINITY_GAMEINPUT_VERSION}/redist/GameInputRedist.msi")

    if(TRINITY_GAMEINPUT_FOUND AND EXISTS "${redist}")
        set(directory "${OUTPUT_BASE}/Redist")
        add_custom_target(${target} ALL
            COMMAND "${CMAKE_COMMAND}" -E true
            COMMAND "$<$<CONFIG:Release,Distribution>:${CMAKE_COMMAND};-E;make_directory;${directory}>"
            COMMAND "$<$<CONFIG:Release,Distribution>:${CMAKE_COMMAND};-E;copy_if_different;${redist};${directory}/GameInputRedist.msi>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    else()
        add_custom_target(${target})
    endif()
endfunction()