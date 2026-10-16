add_library(mydb_services_admin STATIC
    CommandStatistics.cpp
)
add_library(mydb::services_admin ALIAS mydb_services_admin)
target_link_libraries(mydb_services_admin PUBLIC mydb::core)
mydb_set_warnings(mydb_services_admin)
