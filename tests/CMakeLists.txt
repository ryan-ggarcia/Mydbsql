find_package(GTest CONFIG REQUIRED)
include(GoogleTest)
include(GenerateExportHeader)

add_library(mydb_test_plugin MODULE fixtures/TestPlugin.cpp)
generate_export_header(mydb_test_plugin
    BASE_NAME MYDB_TEST_PLUGIN
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated/TestPluginExport.hpp")
target_include_directories(mydb_test_plugin PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
mydb_set_warnings(mydb_test_plugin)

mydb_add_dynamic_module(
    TARGET mydb_module_test_bad_version
    ID test.bad_version
    SOURCES fixtures/BadVersionModule.cpp)

add_executable(mydb_tests
    unit/core/DbErrorTest.cpp
    unit/core/ModelTest.cpp
    unit/core/SqlSplitterTest.cpp
    unit/core/SqlTokenizerTest.cpp
    unit/sdk/SdkTest.cpp
    unit/infra/DynamicLibraryTest.cpp
    unit/infra/PlatformTest.cpp
    unit/infra/UrlPolicyTest.cpp
    unit/app/CommandLineTest.cpp
    unit/app/ModuleHostTest.cpp
    unit/app/StartupReportTest.cpp
)
target_link_libraries(mydb_tests PRIVATE
    mydb::core
    mydb::sdk
    mydb::platform
    mydb::app_support
    GTest::gtest_main)
target_compile_definitions(mydb_tests PRIVATE
    MYDB_TEST_PLUGIN_PATH="$<TARGET_FILE:mydb_test_plugin>"
    MYDB_EXAMPLE_MODULE_PATH="$<TARGET_FILE:mydb_module_example_hello>"
    MYDB_BAD_VERSION_MODULE_PATH="$<TARGET_FILE:mydb_module_test_bad_version>")
add_dependencies(mydb_tests mydb_test_plugin mydb_module_example_hello mydb_module_test_bad_version)
mydb_set_warnings(mydb_tests)

gtest_discover_tests(mydb_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)

if(Python3_Interpreter_FOUND)
    add_test(NAME lint.platform_ifdefs
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_platform_ifdefs.py"
                --root "${PROJECT_SOURCE_DIR}")
    set_tests_properties(lint.platform_ifdefs PROPERTIES LABELS lint)
    add_test(NAME lint.module_dependencies
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_module_deps.py"
                --root "${PROJECT_SOURCE_DIR}")
    set_tests_properties(lint.module_dependencies PROPERTIES LABELS lint)
endif()

if(MYDB_BUILD_APP)
    add_test(NAME ui.measure_startup_smoke
        COMMAND mydbsql --measure-startup --settle-ms=0 -platform offscreen)
    set_tests_properties(ui.measure_startup_smoke PROPERTIES
        LABELS "ui;startup"
        TIMEOUT 60
        PASS_REGULAR_EXPRESSION "\"startup_ms\":[0-9.]+"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
