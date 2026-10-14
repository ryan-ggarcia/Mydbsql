find_package(benchmark CONFIG REQUIRED)

add_executable(mydb_bench
    PlatformBench.cpp
    StartupReportBench.cpp
)
target_link_libraries(mydb_bench PRIVATE
    mydb::platform
    mydb::app_support
    benchmark::benchmark_main)
mydb_set_warnings(mydb_bench)
