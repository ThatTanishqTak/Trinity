# Copies the MSVC runtime DLLs beside Release for Development exports. Ninja needs more than a cd
function(trinity_add_msvc_runtime target)
    set(libraries "")
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
        set(CMAKE_INSTALL_DEBUG_LIBRARIES FALSE)
        include(InstallRequiredSystemLibraries)
        set(libraries ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
        if(NOT libraries)
            message(WARNING "Visual Studio's redistributable C++ runtime was not found, so a Development export's player needs it installed")
        endif()
    endif()

    if(libraries)
        add_custom_target(${target} ALL
            COMMAND "${CMAKE_COMMAND}" -E true
            COMMAND "$<$<CONFIG:Release>:${CMAKE_COMMAND};-E;make_directory;${OUTPUT_BASE}>"
            COMMAND "$<$<CONFIG:Release>:${CMAKE_COMMAND};-E;copy_if_different;${libraries};${OUTPUT_BASE}>"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    else()
        add_custom_target(${target})
    endif()
endfunction()