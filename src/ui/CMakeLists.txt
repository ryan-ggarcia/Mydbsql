add_library(mydb_ui STATIC
    common/Icons.cpp
    dialogs/FindReplaceDialog.cpp
    dialogs/QueryHistoryDialog.cpp
    dialogs/SessionManagerDialog.cpp
    dialogs/SnippetsDialog.cpp
    editors/EditorPreferences.cpp
    editors/QueryTab.cpp
    editors/SqlEditor.cpp
    main/MainWindow.cpp
    models/ObjectTreeFilter.cpp
    models/ObjectTreeModel.cpp
    models/ResultTableModel.cpp
    models/RowsModel.cpp
    panels/DatabasePanel.cpp
    panels/HostPanel.cpp
    panels/LogPanel.cpp
    panels/TableTab.cpp
)
add_library(mydb::ui ALIAS mydb_ui)

file(GLOB MYDB_ICON_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(mydb_ui mydb_icons
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES ${MYDB_ICON_FILES}
    OPTIONS -compress-algo zlib)

file(GLOB MYDB_FUNCTION_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/functions/*.json")
qt_add_resources(mydb_ui mydb_functions
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES ${MYDB_FUNCTION_FILES}
    OPTIONS -compress-algo zlib)

set_target_properties(mydb_ui PROPERTIES AUTOMOC ON)
target_link_libraries(mydb_ui PUBLIC
    mydb::core
    mydb::sdk
    mydb::services_session
    mydb::services_admin
    mydb::services_log
    mydb::services_query
    Qt6::Widgets)
target_compile_definitions(mydb_ui PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_KEYWORDS)
mydb_set_warnings(mydb_ui)
