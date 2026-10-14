add_subdirectory(core)
add_subdirectory(infra)

if(MYDB_BUILD_APP)
    add_subdirectory(ui)
endif()

add_subdirectory(app)
