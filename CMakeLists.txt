cmake_minimum_required(VERSION 3.25)

project(Mydbsql
    VERSION 0.1.0
    DESCRIPTION "Cliente gráfico de bancos de dados, leve e multiplataforma"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "Build dentro da árvore de fontes não é suportado. Use: cmake --preset debug")
endif()

option(MYDB_BUILD_APP "Compila o executável mydbsql (exige Qt 6)" ON)
option(MYDB_BUILD_TESTS "Compila os testes" ON)
option(MYDB_BUILD_BENCHMARKS "Compila os benchmarks (Google Benchmark)" ON)
option(MYDB_WARNINGS_AS_ERRORS "Trata warnings como erros (CLAUDE.md §0)" ON)
option(MYDB_ENABLE_LTO "Ativa LTO/IPO nos builds de release" OFF)
set(MYDB_SANITIZER "" CACHE STRING "Sanitizer: vazio, address (ASan+UBSan) ou thread (TSan)")
set_property(CACHE MYDB_SANITIZER PROPERTY STRINGS "" address thread)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(ReleaseOptimizations)
include(StaticAnalysis)
include(Modules)

if(MYDB_BUILD_TESTS)
    enable_testing()
endif()

if(MYDB_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)
endif()

add_subdirectory(src)

if(MYDB_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(MYDB_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

mydb_check_module_dependencies()
