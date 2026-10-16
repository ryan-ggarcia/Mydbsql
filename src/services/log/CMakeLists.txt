add_library(mydb_services_log STATIC
    LogBuffer.cpp
)
add_library(mydb::services_log ALIAS mydb_services_log)
target_link_libraries(mydb_services_log PUBLIC mydb::core)
mydb_set_warnings(mydb_services_log)
