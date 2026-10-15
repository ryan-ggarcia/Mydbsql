add_subdirectory(core)
add_subdirectory(sdk)
add_subdirectory(infra)
add_subdirectory(drivers)
add_subdirectory(modules)
add_subdirectory(services)

if(MYDB_BUILD_APP)
    add_subdirectory(ui)
endif()

add_subdirectory(app)
