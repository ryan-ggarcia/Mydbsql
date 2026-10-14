add_library(mydb_sdk STATIC
    EventBus.cpp
    IModule.cpp
)
add_library(mydb::sdk ALIAS mydb_sdk)

target_link_libraries(mydb_sdk PUBLIC mydb::core)
mydb_set_warnings(mydb_sdk)
