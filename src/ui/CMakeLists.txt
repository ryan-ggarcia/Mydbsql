add_library(mydb_ui STATIC
    main/MainWindow.cpp
)
add_library(mydb::ui ALIAS mydb_ui)

set_target_properties(mydb_ui PROPERTIES AUTOMOC ON)
target_link_libraries(mydb_ui PUBLIC mydb::core Qt6::Widgets)
target_compile_definitions(mydb_ui PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_KEYWORDS)
mydb_set_warnings(mydb_ui)
