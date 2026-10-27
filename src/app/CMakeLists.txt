add_library(mydb_app_support STATIC
    BuiltinModules.cpp
    CommandLine.cpp
    ModuleDriverCatalog.cpp
    ModuleHost.cpp
    StartupReport.cpp
)
add_library(mydb::app_support ALIAS mydb_app_support)
target_link_libraries(mydb_app_support PUBLIC mydb::core mydb::sdk mydb::platform PRIVATE mydb_module_workbench mydb_module_query)
mydb_set_warnings(mydb_app_support)

if(NOT MYDB_BUILD_APP)
    return()
endif()

add_executable(mydbsql
    main.cpp
    Application.cpp
    EventLoopWatchdog.cpp
    StartupProbe.cpp
)
set_target_properties(mydbsql PROPERTIES AUTOMOC ON)
set_target_properties(mydbsql PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MYDB_RUNTIME_OUTPUT_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(mydbsql PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${MYDB_RUNTIME_OUTPUT_DIR}")
endforeach()
target_link_libraries(mydbsql PRIVATE
    mydb::app_support
    mydb::platform
    mydb::logging
    mydb::ui
    mydb::settings
    mydb::services_session
    mydb::services_query
    Qt6::Widgets)
target_compile_definitions(mydbsql PRIVATE
    QT_NO_CAST_FROM_ASCII QT_NO_KEYWORDS
    MYDB_VERSION="${PROJECT_VERSION}"
    MYDB_DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)
mydb_set_warnings(mydbsql)

if(WIN32)
    target_compile_definitions(mydbsql PRIVATE MYDB_DEFAULT_QPA_PLATFORM="windows:fontengine=gdi")
else()
    target_compile_definitions(mydbsql PRIVATE MYDB_DEFAULT_QPA_PLATFORM="")
endif()

if(UNIX AND NOT APPLE)
    target_link_options(mydbsql PRIVATE "LINKER:-rpath-link,$<TARGET_FILE_DIR:Qt6::Core>")
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(mydbsql PROPERTIES WIN32_EXECUTABLE ON)
endif()
