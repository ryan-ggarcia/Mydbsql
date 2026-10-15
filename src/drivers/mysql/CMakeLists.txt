find_package(unofficial-libmariadb CONFIG REQUIRED)

add_library(mydb_driver_mysql_text STATIC
    MySqlColumnParser.cpp
    MySqlDialect.cpp
    MySqlServerVersion.cpp
)
target_link_libraries(mydb_driver_mysql_text PUBLIC mydb::core)
mydb_set_warnings(mydb_driver_mysql_text)

add_library(mydb_driver_mysql_impl STATIC
    MySqlConnection.cpp
    MySqlDriver.cpp
    MySqlExecution.cpp
    MySqlMetadataProvider.cpp
    MySqlResultSet.cpp
)
target_link_libraries(mydb_driver_mysql_impl PUBLIC mydb_driver_mysql_text unofficial::libmariadb)
if(WIN32)
    target_compile_definitions(mydb_driver_mysql_impl PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
mydb_set_warnings(mydb_driver_mysql_impl)

mydb_add_dynamic_module(
    TARGET mydb_driver_mysql
    ID driver.mysql
    SOURCES MySqlModule.cpp
    LIBRARIES mydb_driver_mysql_impl)

set(MYDB_MARIADB_PLUGINS_RELEASE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/plugins/libmariadb")
if(EXISTS "${MYDB_MARIADB_PLUGINS_RELEASE}")
    add_custom_command(TARGET mydb_driver_mysql POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>plugins/libmariadb"
                "${MYDB_MODULE_OUTPUT_DIR}/mariadb-plugins"
        VERBATIM)
endif()

if(WIN32)
    add_custom_command(TARGET mydb_driver_mysql POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:mydb_driver_mysql> "${MYDB_MODULE_OUTPUT_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif()
