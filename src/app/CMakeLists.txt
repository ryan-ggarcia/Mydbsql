# mydb_app_support: partes do app sem Qt (linha de comando, relatório de inicialização), testáveis.
add_library(mydb_app_support STATIC
    CommandLine.cpp
    StartupReport.cpp
)
add_library(mydb::app_support ALIAS mydb_app_support)
target_link_libraries(mydb_app_support PUBLIC mydb::core)
mydb_set_warnings(mydb_app_support)

if(NOT MYDB_BUILD_APP)
    return()
endif()

# mydbsql: composição (CLAUDE.md §4.3, D). Só aqui as implementações concretas se encontram.
add_executable(mydbsql
    main.cpp
    Application.cpp
    EventLoopWatchdog.cpp
    StartupProbe.cpp
)
set_target_properties(mydbsql PROPERTIES AUTOMOC ON)
target_link_libraries(mydbsql PRIVATE
    mydb::app_support
    mydb::platform
    mydb::logging
    mydb::ui
    Qt6::Widgets)
target_compile_definitions(mydbsql PRIVATE
    QT_NO_CAST_FROM_ASCII QT_NO_KEYWORDS
    MYDB_VERSION="${PROJECT_VERSION}"
    MYDB_DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)
mydb_set_warnings(mydbsql)

# Release: app gráfico sem console no Windows. No Debug o console fica, para ver o log.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(mydbsql PROPERTIES WIN32_EXECUTABLE ON)
endif()
