set(MYDB_MODULE_OUTPUT_DIR "${PROJECT_BINARY_DIR}/modules")
set(MYDB_MODULE_ALLOWED_LINKS mydb::core mydb::sdk mydb_core mydb_sdk)

function(mydb_add_dynamic_module)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET;ID" "SOURCES")
    if(NOT ARG_TARGET OR NOT ARG_ID OR NOT ARG_SOURCES)
        message(FATAL_ERROR "mydb_add_dynamic_module: TARGET, ID e SOURCES são obrigatórios")
    endif()
    string(REPLACE "." "_" stem "mydb_${ARG_ID}")

    add_library(${ARG_TARGET} MODULE ${ARG_SOURCES})
    set_target_properties(${ARG_TARGET} PROPERTIES
        OUTPUT_NAME "${stem}"
        PREFIX ""
        SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${MYDB_MODULE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${MYDB_MODULE_OUTPUT_DIR}")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${ARG_TARGET} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${config} "${MYDB_MODULE_OUTPUT_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_${config} "${MYDB_MODULE_OUTPUT_DIR}")
    endforeach()
    target_link_libraries(${ARG_TARGET} PRIVATE mydb::sdk)
    if(MSVC)
        target_compile_definitions(${ARG_TARGET} PRIVATE "MYDB_MODULE_EXPORT=__declspec(dllexport)")
    else()
        target_compile_definitions(${ARG_TARGET} PRIVATE "MYDB_MODULE_EXPORT=__attribute__((visibility(\"default\")))")
    endif()
    mydb_set_warnings(${ARG_TARGET})
    set_property(GLOBAL APPEND PROPERTY MYDB_DYNAMIC_MODULES ${ARG_TARGET})
endfunction()

function(mydb_check_module_dependencies)
    get_property(modules GLOBAL PROPERTY MYDB_DYNAMIC_MODULES)
    foreach(module IN LISTS modules)
        get_target_property(links ${module} LINK_LIBRARIES)
        foreach(link IN LISTS links)
            if(NOT link IN_LIST MYDB_MODULE_ALLOWED_LINKS)
                message(FATAL_ERROR "O módulo ${module} linka ${link}; módulos só podem depender de mydb_core e mydb_sdk (CLAUDE.md §2.4)")
            endif()
        endforeach()
    endforeach()
endfunction()
