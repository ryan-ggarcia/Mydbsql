add_library(mydb_core STATIC
    errors/DbError.cpp
)
add_library(mydb::core ALIAS mydb_core)

target_include_directories(mydb_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(mydb_core PUBLIC cxx_std_20)
mydb_set_warnings(mydb_core)
