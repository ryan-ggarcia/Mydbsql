find_package(nlohmann_json CONFIG REQUIRED)

add_library(mydb_services_session STATIC
    CachedMetadataProvider.cpp
    ConnectionWorker.cpp
    FavoritesStore.cpp
    SessionConnector.cpp
    SessionRepository.cpp
    SessionService.cpp
)
add_library(mydb::services_session ALIAS mydb_services_session)
target_link_libraries(mydb_services_session
    PUBLIC mydb::core mydb::sdk mydb::services_log
    PRIVATE nlohmann_json::nlohmann_json)
mydb_set_warnings(mydb_services_session)
