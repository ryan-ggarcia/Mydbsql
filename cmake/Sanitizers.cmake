# Sanitizers globais, escolhidos pelo cache MYDB_SANITIZER (presets asan e tsan).
# Valem para todos os alvos do projeto, para que testes e bibliotecas usem o mesmo runtime.

if(MYDB_SANITIZER STREQUAL "")
    return()
endif()

if(MSVC)
    if(MYDB_SANITIZER STREQUAL "address")
        add_compile_options(/fsanitize=address /Zi)
        # O ASan do MSVC não combina com /RTC (padrão do Debug).
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    else()
        message(FATAL_ERROR "MSVC não suporta MYDB_SANITIZER=${MYDB_SANITIZER}; use address")
    endif()
    return()
endif()

if(MYDB_SANITIZER STREQUAL "address")
    set(_mydb_sanitize_flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
elseif(MYDB_SANITIZER STREQUAL "thread")
    set(_mydb_sanitize_flags -fsanitize=thread -fno-omit-frame-pointer)
else()
    message(FATAL_ERROR "MYDB_SANITIZER desconhecido: ${MYDB_SANITIZER}")
endif()

add_compile_options(${_mydb_sanitize_flags})
add_link_options(${_mydb_sanitize_flags})
