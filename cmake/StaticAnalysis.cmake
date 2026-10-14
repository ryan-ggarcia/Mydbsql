# Alvos de qualidade: clang-tidy, format, format-check e a checagem de #ifdef de SO (§2.6, regra 1).
# Os scripts ficam em tools/ e são escritos em Python para rodarem iguais nas três plataformas.

find_package(Python3 3.9 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Python 3 não encontrado: alvos clang-tidy, format e a checagem de #ifdef desligados")
    return()
endif()

set(MYDB_SOURCE_DIRS src tests bench)

find_program(MYDB_CLANG_TIDY_EXE NAMES clang-tidy)
if(MYDB_CLANG_TIDY_EXE)
    add_custom_target(clang-tidy
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_clang_tidy.py"
                --clang-tidy "${MYDB_CLANG_TIDY_EXE}"
                --build-dir "${PROJECT_BINARY_DIR}"
                --source-dir "${PROJECT_SOURCE_DIR}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "clang-tidy em src/"
        USES_TERMINAL VERBATIM)
endif()

find_program(MYDB_CLANG_FORMAT_EXE NAMES clang-format)
if(MYDB_CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_clang_format.py"
                --clang-format "${MYDB_CLANG_FORMAT_EXE}" --fix ${MYDB_SOURCE_DIRS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "clang-format (corrige)"
        USES_TERMINAL VERBATIM)
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/run_clang_format.py"
                --clang-format "${MYDB_CLANG_FORMAT_EXE}" ${MYDB_SOURCE_DIRS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "clang-format (só verifica)"
        USES_TERMINAL VERBATIM)
endif()
