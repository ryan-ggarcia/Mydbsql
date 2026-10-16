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
    unit/core/LocalTimeTest.cpp
    unit/core/ModelTest.cpp
    unit/core/SqlSplitterTest.cpp
    unit/core/SqlTokenizerTest.cpp
    unit/sdk/SdkTest.cpp
    unit/drivers/MySqlTextTest.cpp
    unit/infra/JsonSettingsStoreTest.cpp
    unit/infra/SystemCredentialStoreTest.cpp
    unit/services/SessionRepositoryTest.cpp
    unit/services/SessionConnectorTest.cpp
    unit/services/CachedMetadataProviderTest.cpp
    unit/services/CommandStatisticsTest.cpp
    unit/services/ConnectionWorkerTest.cpp
    unit/services/FavoritesStoreTest.cpp
    unit/services/LogBufferTest.cpp
    unit/services/SessionServiceTest.cpp
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
    mydb_driver_mysql_text
    mydb::settings
    mydb::services_session
    mydb::services_admin
    GTest::gtest_main)
target_include_directories(mydb_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mydb_tests PRIVATE
    MYDB_TEST_PLUGIN_PATH="$<TARGET_FILE:mydb_test_plugin>"
    MYDB_EXAMPLE_MODULE_PATH="$<TARGET_FILE:mydb_module_example_hello>"
    MYDB_BAD_VERSION_MODULE_PATH="$<TARGET_FILE:mydb_module_test_bad_version>"
    MYDB_MYSQL_DRIVER_PATH="$<TARGET_FILE:mydb_driver_mysql>")
add_dependencies(mydb_tests mydb_test_plugin mydb_module_example_hello mydb_module_test_bad_version mydb_driver_mysql)
mydb_set_warnings(mydb_tests)

gtest_discover_tests(mydb_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)

add_executable(mydb_contract_tests
    contract/MySqlContractTest.cpp
    contract/MySqlScaleTest.cpp
    contract/SessionEndToEndTest.cpp
)
target_link_libraries(mydb_contract_tests PRIVATE
    mydb_driver_mysql_impl
    mydb::app_support
    mydb::settings
    mydb::services_session
    GTest::gtest_main)
target_include_directories(mydb_contract_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mydb_contract_tests PRIVATE
    MYDB_MARIADB_PLUGIN_DIR="${MYDB_MODULE_OUTPUT_DIR}/mariadb-plugins"
    MYDB_MODULE_DIR="${MYDB_MODULE_OUTPUT_DIR}")
add_dependencies(mydb_contract_tests mydb_driver_mysql)
mydb_set_warnings(mydb_contract_tests)
if(MSVC)
    target_compile_definitions(mydb_contract_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

gtest_discover_tests(mydb_contract_tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS integration)

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
    add_executable(mydb_ui_tests ui/SessionManagerDialogTest.cpp)
    set_target_properties(mydb_ui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(mydb_ui_tests PRIVATE mydb::ui mydb::settings Qt6::Test)
    target_include_directories(mydb_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(mydb_ui_tests PRIVATE QT_NO_KEYWORDS)
    mydb_set_warnings(mydb_ui_tests)
    add_test(NAME ui.session_manager COMMAND mydb_ui_tests -platform offscreen)
    set_tests_properties(ui.session_manager PROPERTIES
        LABELS ui
        TIMEOUT 120
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    add_executable(mydb_ui_main_window_tests ui/MainWindowTest.cpp)
    set_target_properties(mydb_ui_main_window_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(mydb_ui_main_window_tests PRIVATE mydb::ui mydb::app_support mydb::settings Qt6::Test)
    target_include_directories(mydb_ui_main_window_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(mydb_ui_main_window_tests PRIVATE QT_NO_KEYWORDS)
    mydb_set_warnings(mydb_ui_main_window_tests)
    add_test(NAME ui.main_window COMMAND mydb_ui_main_window_tests -platform offscreen)
    set_tests_properties(ui.main_window PROPERTIES
        LABELS ui
        TIMEOUT 120
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

    add_test(NAME ui.measure_startup_smoke
        COMMAND mydbsql --measure-startup --settle-ms=0 -platform offscreen)
    set_tests_properties(ui.measure_startup_smoke PROPERTIES
        LABELS "ui;startup"
        TIMEOUT 60
        PASS_REGULAR_EXPRESSION "\"startup_ms\":[0-9.]+"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
