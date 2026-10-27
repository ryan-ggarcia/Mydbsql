find_package(nlohmann_json CONFIG REQUIRED)

add_library(mydb_services_query STATIC
    QueryDocuments.cpp
    QueryHistoryStore.cpp
)
add_library(mydb::services_query ALIAS mydb_services_query)
target_link_libraries(mydb_services_query
    PUBLIC mydb::services_session
    PRIVATE nlohmann_json::nlohmann_json)
mydb_set_warnings(mydb_services_query)
