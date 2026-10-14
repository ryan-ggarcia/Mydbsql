# CLAUDE.md §2.3, regra 8: -O2 (não -O3), LTO opcional e símbolos ocultos no release.

if(NOT MSVC)
    foreach(config RELEASE RELWITHDEBINFO)
        string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
    endforeach()
endif()

if(MYDB_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _mydb_ipo_ok OUTPUT _mydb_ipo_error LANGUAGES CXX)
    if(_mydb_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(WARNING "LTO indisponível neste compilador: ${_mydb_ipo_error}")
    endif()
endif()
