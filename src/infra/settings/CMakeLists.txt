add_library(mydb_settings STATIC JsonSettingsStore.cpp)
add_library(mydb::settings ALIAS mydb_settings)
target_link_libraries(mydb_settings PUBLIC mydb::core)
mydb_set_warnings(mydb_settings)
