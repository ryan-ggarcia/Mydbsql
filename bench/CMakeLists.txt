find_package(benchmark CONFIG REQUIRED)

add_executable(mydb_bench
    PlatformBench.cpp
    SqlSplitterBench.cpp
    StartupReportBench.cpp
    CompletionBench.cpp
)
target_link_libraries(mydb_bench PRIVATE
    mydb::core
    mydb::platform
    mydb::app_support
    mydb::services_query
    benchmark::benchmark_main)
mydb_set_warnings(mydb_bench)

if(MYDB_BUILD_APP)
    add_executable(mydb_ui_bench ui/ObjectTreeBench.cpp)
    target_link_libraries(mydb_ui_bench PRIVATE mydb::ui benchmark::benchmark)
    mydb_set_warnings(mydb_ui_bench)
endif()
