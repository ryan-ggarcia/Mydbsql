# mydb_logging: spdlog assíncrono (CLAUDE.md §3). O logger global é a única exceção tolerada
# à regra "nada de singletons" (§4.3, D).
find_package(spdlog CONFIG REQUIRED)

add_library(mydb_logging STATIC Logging.cpp)
add_library(mydb::logging ALIAS mydb_logging)

target_link_libraries(mydb_logging
    PUBLIC spdlog::spdlog)
target_include_directories(mydb_logging PUBLIC "${PROJECT_SOURCE_DIR}/src")
mydb_set_warnings(mydb_logging)
