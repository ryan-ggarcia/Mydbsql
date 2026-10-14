# Warnings do projeto (CLAUDE.md §0). Aplicados só aos nossos alvos, nunca às dependências:
# os headers de Qt, GoogleTest e Google Benchmark entram como SYSTEM pelos alvos importados.

function(mydb_set_warnings target)
    if(MSVC)
        set(flags /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /external:W0)
        if(MYDB_WARNINGS_AS_ERRORS)
            list(APPEND flags /WX)
        endif()
    else()
        set(flags
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-align
            -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough)
        if(MYDB_WARNINGS_AS_ERRORS)
            list(APPEND flags -Werror)
        endif()
    endif()
    target_compile_options(${target} PRIVATE ${flags})
endfunction()
