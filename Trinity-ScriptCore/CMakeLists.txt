# Trinity-ScriptCore CMakeLists.txt

# Built by dotnet into bin/<configuration>/Trinity/Scripting beside the executables, with the runtime config the engine starts .NET from. dotnet runs from the root, so global.json picks its SDK, and keeps its intermediate files in the build tree. Built again when a source, the project, global.json or the generated bindings change
file(GLOB_RECURSE TRINITY_SCRIPTCORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cs")
set(TRINITY_SCRIPTCORE_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Trinity-ScriptCore.csproj")
set(TRINITY_SCRIPTCORE_DIRECTORY "${OUTPUT_BASE}/Scripting")
set(TRINITY_SCRIPTCORE_ASSEMBLY "${TRINITY_SCRIPTCORE_DIRECTORY}/Trinity-ScriptCore.dll")

# The C# side of the engine's functions, written from NativeFunctions.def into the build tree. The generator leaves a file that would not change as it was, so ScriptCore builds again only when the list does
set(TRINITY_SCRIPTCORE_FUNCTIONS "${CMAKE_SOURCE_DIR}/Trinity-Engine/Internal/Trinity/Scripting/NativeFunctions.def")
set(TRINITY_SCRIPTCORE_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/Generated/$<CONFIG>")

add_custom_command(OUTPUT "${TRINITY_SCRIPTCORE_GENERATED}/Generated.stamp"
    BYPRODUCTS "${TRINITY_SCRIPTCORE_GENERATED}/NativeFunctions.g.cs"
    COMMAND Trinity-InteropGen "${TRINITY_SCRIPTCORE_FUNCTIONS}" "${TRINITY_SCRIPTCORE_GENERATED}/NativeFunctions.g.cs"
    COMMAND "${CMAKE_COMMAND}" -E touch "${TRINITY_SCRIPTCORE_GENERATED}/Generated.stamp"
    DEPENDS Trinity-InteropGen "${TRINITY_SCRIPTCORE_FUNCTIONS}"
    COMMENT "Generating the C# bindings of NativeFunctions.def"
    VERBATIM
)

# An IDE that opens the project on its own finds the Debug bindings through this, once a Debug build has written them, while CMake's builds name their own configuration's. Written only when it changes, and ignored by git
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/Trinity-ScriptCore.Local.props" CONTENT "<Project>\n  <PropertyGroup>\n    <TrinityGeneratedDirectory Condition=\"'$(TrinityGeneratedDirectory)' == ''\">@CMAKE_CURRENT_BINARY_DIR@/Generated/Debug</TrinityGeneratedDirectory>\n  </PropertyGroup>\n</Project>\n" @ONLY)

# dotnet leaves an assembly that did not change as it was, so the touch tells Ninja it is up to date
add_custom_command(OUTPUT "${TRINITY_SCRIPTCORE_ASSEMBLY}"
    COMMAND "${CMAKE_COMMAND}" -E env DOTNET_NOLOGO=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
        "${TRINITY_DOTNET}" build "${TRINITY_SCRIPTCORE_PROJECT}" --nologo --verbosity quiet -nodeReuse:false
        --configuration "$<IF:$<CONFIG:Debug>,Debug,Release>"
        --output "${TRINITY_SCRIPTCORE_DIRECTORY}"
        --artifacts-path "${CMAKE_CURRENT_BINARY_DIR}/Artifacts"
        "-p:TreatWarningsAsErrors=$<IF:$<BOOL:${TRINITY_WARNINGS_AS_ERRORS}>,true,false>"
        "-p:TrinityGeneratedDirectory=${TRINITY_SCRIPTCORE_GENERATED}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${TRINITY_SCRIPTCORE_ASSEMBLY}"
    DEPENDS ${TRINITY_SCRIPTCORE_SOURCES} "${TRINITY_SCRIPTCORE_PROJECT}" "${CMAKE_SOURCE_DIR}/global.json" "${TRINITY_SCRIPTCORE_GENERATED}/Generated.stamp" "${TRINITY_SCRIPTCORE_GENERATED}/NativeFunctions.g.cs"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building Trinity-ScriptCore"
    VERBATIM
)

add_custom_target(Trinity-ScriptCore ALL DEPENDS "${TRINITY_SCRIPTCORE_ASSEMBLY}" SOURCES ${TRINITY_SCRIPTCORE_SOURCES} "${TRINITY_SCRIPTCORE_PROJECT}" "${TRINITY_SCRIPTCORE_FUNCTIONS}")