set(TRINITY_AGILITY_SDK_FOUND OFF)

if(TRINITY_RHI_D3D12)
    file(STRINGS "${CMAKE_SOURCE_DIR}/Scripts/AgilitySDK.lock" TRINITY_AGILITY_SDK_LINE REGEX "^Microsoft\\.Direct3D\\.D3D12 ")
    string(REGEX REPLACE "^[^ ]+ ([0-9.]+) .*$" "\\1" TRINITY_AGILITY_SDK_VERSION "${TRINITY_AGILITY_SDK_LINE}")
    string(REGEX REPLACE "^1\\.([0-9]+)\\..*$" "\\1" TRINITY_AGILITY_SDK_NUMBER "${TRINITY_AGILITY_SDK_VERSION}")

    # The runtime and the DirectX-Headers the engine compiles against must be the same SDK version
    file(STRINGS "${CMAKE_SOURCE_DIR}/Vendor/DirectX-Headers/include/directx/d3d12.h" TRINITY_D3D12_HEADER_LINE REGEX "^#define[ \t]+D3D12_SDK_VERSION[ \t]")
    string(REGEX REPLACE "^.*\\([ \t]*([0-9]+)[ \t]*\\).*$" "\\1" TRINITY_D3D12_HEADER_NUMBER "${TRINITY_D3D12_HEADER_LINE}")
    if(NOT TRINITY_AGILITY_SDK_NUMBER STREQUAL TRINITY_D3D12_HEADER_NUMBER)
        message(FATAL_ERROR "Scripts/AgilitySDK.lock pins Agility SDK ${TRINITY_AGILITY_SDK_VERSION} (${TRINITY_AGILITY_SDK_NUMBER}), but the DirectX-Headers pin is SDK ${TRINITY_D3D12_HEADER_NUMBER}. Keep the two in step.")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(TRINITY_AGILITY_SDK_ARCHITECTURE "arm64")
    else()
        set(TRINITY_AGILITY_SDK_ARCHITECTURE "x64")
    endif()
    set(TRINITY_AGILITY_SDK_BINARIES "${CMAKE_SOURCE_DIR}/Vendor/AgilitySDK/${TRINITY_AGILITY_SDK_VERSION}/build/native/bin/${TRINITY_AGILITY_SDK_ARCHITECTURE}")

    if(EXISTS "${TRINITY_AGILITY_SDK_BINARIES}/D3D12Core.dll")
        set(TRINITY_AGILITY_SDK_FOUND ON)
        target_compile_definitions(Trinity-BuildConfig INTERFACE TR_AGILITY_SDK_VERSION=${TRINITY_AGILITY_SDK_NUMBER})
    else()
        message(WARNING "The D3D12 Agility SDK ${TRINITY_AGILITY_SDK_VERSION} is missing from Vendor/AgilitySDK. Run Scripts/Bootstrap.ps1; until then D3D12 uses the runtime built into Windows.")
    endif()
endif()

# Copies the runtime beside the executables. The SDK layers are the debug layer that matches the runtime, so Distribution leaves them out
function(trinity_add_agility_sdk target)
    set(outputs "")

    if(TRINITY_AGILITY_SDK_FOUND)
        set(directory "${OUTPUT_BASE}/D3D12")

        foreach(file IN ITEMS D3D12Core.dll d3d12SDKLayers.dll)
            set(output "${directory}/${file}")
            add_custom_command(OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TRINITY_AGILITY_SDK_BINARIES}/${file}" "${output}"
                DEPENDS "${TRINITY_AGILITY_SDK_BINARIES}/${file}"
                COMMENT "Copying Agility SDK ${TRINITY_AGILITY_SDK_VERSION} ${file}"
                VERBATIM
            )
        endforeach()

        list(APPEND outputs "${directory}/D3D12Core.dll" "$<$<NOT:$<CONFIG:Distribution>>:${directory}/d3d12SDKLayers.dll>")
    endif()

    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()