#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mydb::drivers::mysql {

/// Versão do servidor, como o driver precisa dela para decidir recursos (ISqlDialect::supports).
struct MySqlServerVersion {
    bool mariaDb{false};
    std::uint32_t number{0}; ///< major*10000 + minor*100 + patch, ex.: 110412
    std::string text;        ///< como o servidor informa, ex.: "11.4.12-MariaDB-log"

    [[nodiscard]] bool atLeast(std::uint32_t major, std::uint32_t minor, std::uint32_t patch = 0) const noexcept {
        return number >= (major * 10000) + (minor * 100) + patch;
    }

    /// Lê textos como "8.0.45", "11.4.12-MariaDB" e "5.5.5-10.6.18-MariaDB" (prefixo de compatibilidade
    /// que o MariaDB manda para clientes antigos).
    [[nodiscard]] static MySqlServerVersion parse(std::string_view versionText);
};

} // namespace mydb::drivers::mysql
