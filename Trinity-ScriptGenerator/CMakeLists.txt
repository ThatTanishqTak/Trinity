# Trinity-ScriptGenerator CMakeLists.txt

# Built by dotnet into bin/<configuration>/Trinity/Scripting beside Trinity-ScriptCore, where a game's C# project finds both. dotnet runs from the root, so global.json picks its SDK, and keeps its intermediate files in the build tree. Built again when a source, the project, its release notes or global.json changes
file(GLOB_RECURSE TRINITY_SCRIPTGENERATOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cs")
set(TRINITY_SCRIPTGENERATOR_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Trinity-ScriptGenerator.csproj")
set(TRINITY_SCRIPTGENERATOR_RELEASES "${CMAKE_CURRENT_SOURCE_DIR}/AnalyzerReleases.Shipped.md" "${CMAKE_CURRENT_SOURCE_DIR}/AnalyzerReleases.Unshipped.md")
set(TRINITY_SCRIPTGENERATOR_ASSEMBLY "${OUTPUT_BASE}/Scripting/Trinity-ScriptGenerator.dll")

# dotnet leaves an assembly that did not change as it was, so the touch tells Ninja it is up to date
add_custom_command(OUTPUT "${TRINITY_SCRIPTGENERATOR_ASSEMBLY}"
    COMMAND "${CMAKE_COMMAND}" -E env DOTNET_NOLOGO=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
        "${TRINITY_DOTNET}" build "${TRINITY_SCRIPTGENERATOR_PROJECT}" --nologo --verbosity quiet -nodeReuse:false
        --configuration "$<IF:$<CONFIG:Debug>,Debug,Release>"
        --output "${OUTPUT_BASE}/Scripting"
        --artifacts-path "${CMAKE_CURRENT_BINARY_DIR}/Artifacts"
        "-p:TreatWarningsAsErrors=$<IF:$<BOOL:${TRINITY_WARNINGS_AS_ERRORS}>,true,false>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${TRINITY_SCRIPTGENERATOR_ASSEMBLY}"
    DEPENDS ${TRINITY_SCRIPTGENERATOR_SOURCES} "${TRINITY_SCRIPTGENERATOR_PROJECT}" ${TRINITY_SCRIPTGENERATOR_RELEASES} "${CMAKE_SOURCE_DIR}/global.json"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building Trinity-ScriptGenerator"
    VERBATIM
)

add_custom_target(Trinity-ScriptGenerator ALL DEPENDS "${TRINITY_SCRIPTGENERATOR_ASSEMBLY}" SOURCES ${TRINITY_SCRIPTGENERATOR_SOURCES} "${TRINITY_SCRIPTGENERATOR_PROJECT}" ${TRINITY_SCRIPTGENERATOR_RELEASES})