#pragma once

#include "core/interfaces/ISqlDialect.hpp"
#include "drivers/mysql/MySqlServerVersion.hpp"

#include <vector>

namespace mydb::drivers::mysql {

/// Partes do @@sql_mode que mudam como strings e identificadores são escritos.
struct MySqlSqlMode {
    bool ansiQuotes{false};
    bool noBackslashEscapes{false};
};

/// Regras textuais do MySQL e do MariaDB. Depende da versão (recursos e tipos) e do sql_mode da sessão.
class MySqlDialect final : public core::ISqlDialect {
public:
    using SqlMode = MySqlSqlMode;

    explicit MySqlDialect(MySqlServerVersion version, SqlMode mode = {});

    [[nodiscard]] std::string quoteIdentifier(std::string_view identifier) const override;
    [[nodiscard]] std::string quoteString(std::string_view value) const override;
    [[nodiscard]] std::string quoteBinary(std::span<const std::byte> value) const override;
    [[nodiscard]] std::string applyLimit(std::string_view select, std::int64_t limit,
                                         std::int64_t offset) const override;
    [[nodiscard]] std::span<const core::model::DataType> dataTypes() const override { return m_dataTypes; }
    [[nodiscard]] bool supports(core::Feature feature) const override;
    [[nodiscard]] core::sql::SqlSyntax syntax() const override;

    [[nodiscard]] const MySqlServerVersion& version() const noexcept { return m_version; }

    /// Lê o valor de @@sql_mode.
    [[nodiscard]] static SqlMode parseSqlMode(std::string_view sqlMode);

private:
    MySqlServerVersion m_version;
    SqlMode m_mode;
    std::vector<core::model::DataType> m_dataTypes;
};

} // namespace mydb::drivers::mysql
