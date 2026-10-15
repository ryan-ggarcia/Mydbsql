#pragma once

#include "core/model/Column.hpp"
#include "core/model/ForeignKey.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace mydb::drivers::mysql {

/// Uma linha de information_schema.COLUMNS, como texto (NULL = std::nullopt).
struct InformationSchemaColumn {
    std::string name;
    std::string dataType;   ///< DATA_TYPE, ex.: "int"
    std::string columnType; ///< COLUMN_TYPE, ex.: "int(10) unsigned", "enum('a','b')"
    bool nullable{true};
    std::optional<std::string> defaultValue; ///< COLUMN_DEFAULT
    std::string extra;                       ///< EXTRA, ex.: "auto_increment", "on update current_timestamp()"
    std::string charset;
    std::string collation;
    std::string comment;
    std::string generationExpression;
};

/// Converte a linha do information_schema no modelo neutro. MySQL e MariaDB escrevem COLUMN_DEFAULT de
/// formas diferentes: o MariaDB (10.2.7+) põe literais entre aspas e escreve NULL por extenso; o MySQL
/// marca expressões com DEFAULT_GENERATED no EXTRA.
[[nodiscard]] core::model::Column parseColumn(const InformationSchemaColumn& row, bool mariaDb);

/// "CASCADE", "SET NULL"… → ReferentialAction.
[[nodiscard]] core::model::ReferentialAction parseReferentialAction(std::string_view rule) noexcept;

/// "2026-10-15 13:45:00" (DATETIME do servidor) → time_point; nullopt se o texto não for uma data.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> parseDateTime(std::string_view text);

} // namespace mydb::drivers::mysql
