#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mydb::core::model {

/// Valor padrão de uma coluna. `text` só vale para Literal (valor já sem aspas) e Expression (SQL cru).
struct ColumnDefault {
    enum class Kind : std::uint8_t {
        None,          ///< sem DEFAULT
        Null,          ///< DEFAULT NULL
        Literal,       ///< DEFAULT 'texto' ou número
        Expression,    ///< DEFAULT (expr), CURRENT_TIMESTAMP etc.
        AutoIncrement, ///< AUTO_INCREMENT, SERIAL, IDENTITY
    };

    Kind kind{Kind::None};
    std::string text;

    friend bool operator==(const ColumnDefault&, const ColumnDefault&) = default;
};

/// Coluna gerada (virtual ou armazenada).
struct GeneratedColumn {
    std::string expression;
    bool stored{false};

    friend bool operator==(const GeneratedColumn&, const GeneratedColumn&) = default;
};

struct Column {
    std::string name;
    std::string typeName;   ///< ex.: "VARCHAR", "INT", "NUMERIC"
    std::string lengthSet;  ///< o que vai entre parênteses: "255", "10,2", "'a','b'"; vazio se não houver
    bool isUnsigned{false};
    bool nullable{true};
    ColumnDefault defaultValue;
    std::string onUpdate;   ///< ex.: "CURRENT_TIMESTAMP" (MySQL); vazio se não houver
    std::string charset;
    std::string collation;
    std::string comment;
    std::optional<GeneratedColumn> generated;
    bool invisible{false};
    std::optional<std::uint32_t> srid;

    /// Tipo completo como aparece no DDL, ex.: "DECIMAL(10,2) UNSIGNED".
    [[nodiscard]] std::string fullType() const;

    friend bool operator==(const Column&, const Column&) = default;
};

} // namespace mydb::core::model
