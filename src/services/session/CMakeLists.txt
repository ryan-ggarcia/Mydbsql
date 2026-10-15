find_package(nlohmann_json CONFIG REQUIRED)

add_library(mydb_services_session STATIC
    SessionConnector.cpp
    SessionRepository.cpp
)
add_library(mydb::services_session ALIAS mydb_services_session)
target_link_libraries(mydb_services_session
    PUBLIC mydb::core
    PRIVATE nlohmann_json::nlohmann_json)
mydb_set_warnings(mydb_services_session)
