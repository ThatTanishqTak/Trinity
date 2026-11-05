# Microsoft's C++ runtime, which the shared Release engine and every module built against it link as DLLs, goes from Visual Studio's redistributable folder beside the Release build, from where a Development export copies it beside a game, as Microsoft lets a program carry its own copy, so a Development player needs nothing installed either. Debug's runtime is not Microsoft's to give away, and Distribution links the static one. The first command runs in every configuration, so one whose copy comes to nothing still gives Ninja more than the cd to its working folder
function(trinity_add_msvc_runtime target)
    set(libraries "")
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
        set(CMAKE_INSTALL_DEBUG_LIBRARIES FALSE)
        include(InstallRequiredSystemLibraries)
        set(libraries ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
        if(NOT libraries)
            message(WARNING "Visual Studio's redistributable C++ runtime was not found, so a Development export carries none, and its player needs the Visual C++ Redistributable installed")
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