# Trinity-InteropGen CMakeLists.txt

set(TARGET_NAME Trinity-InteropGen)

# A build tool that writes ScriptCore's bindings, kept in the build tree so it is never shipped
add_executable(${TARGET_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/Source/Main.cpp")

trinity_configure_target(${TARGET_NAME})
set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tools/$<CONFIG>")