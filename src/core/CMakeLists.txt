add_library(mydb_core STATIC
    errors/DbError.cpp
    model/Column.cpp
    model/DataType.cpp
    model/ForeignKey.cpp
    model/LocalTime.cpp
    model/TableDefinition.cpp
    sql/SqlFormatter.cpp
    sql/SqlSplitter.cpp
    sql/SqlTokenizer.cpp
)
add_library(mydb::core ALIAS mydb_core)

target_include_directories(mydb_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(mydb_core PUBLIC cxx_std_20)
mydb_set_warnings(mydb_core)

if(MSVC)
    set_source_files_properties(model/LocalTime.cpp PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)
endif()
