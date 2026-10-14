# Trinity-InteropGen CMakeLists.txt

set(TARGET_NAME Trinity-InteropGen)

# A build tool, which writes the C# side of NativeFunctions.def as Trinity-ScriptCore builds. It goes in the build tree rather than bin, so it is never shipped beside the executables
add_executable(${TARGET_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/Source/Main.cpp")

trinity_configure_target(${TARGET_NAME})
set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tools/$<CONFIG>")