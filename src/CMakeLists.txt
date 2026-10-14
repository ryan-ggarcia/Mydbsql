# Camadas (CLAUDE.md §4.1): as dependências só apontam para baixo, até o core.
add_subdirectory(core)
add_subdirectory(infra)

if(MYDB_BUILD_APP)
    add_subdirectory(ui)
endif()

# Sempre entra: mydb_app_support não usa Qt e é testado sem ele. O executável só com MYDB_BUILD_APP.
add_subdirectory(app)
