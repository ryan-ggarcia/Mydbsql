#pragma once

// Handles C da libmariadb com posse RAII (CLAUDE.md §0). Só incluído pelos fontes do driver.

#include <mysql.h>

#include <memory>

namespace mydb::drivers::mysql {

struct MysqlCloser {
    void operator()(MYSQL* handle) const noexcept { mysql_close(handle); }
};

/// mysql_free_result também lê e descarta as linhas que faltarem (mysql_use_result).
struct ResultFreer {
    void operator()(MYSQL_RES* result) const noexcept { mysql_free_result(result); }
};

using MysqlHandle = std::unique_ptr<MYSQL, MysqlCloser>;
using ResultHandle = std::unique_ptr<MYSQL_RES, ResultFreer>;

} // namespace mydb::drivers::mysql
