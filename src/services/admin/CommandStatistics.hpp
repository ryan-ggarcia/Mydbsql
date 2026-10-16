#pragma once

#include "core/interfaces/OptionalCapabilities.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mydb::services::admin {

struct CommandStatistic {
    std::string command; ///< "select", "insert", "alter table"…
    std::uint64_t total{0};
    double perHour{0};
    double perSecond{0};
    double percent{0}; ///< parte do total de comandos, de 0 a 100
};

/// Estatísticas de comandos da aba Host a partir das variáveis de status Com_* e Uptime do MySQL/MariaDB.
/// Só comandos executados ao menos uma vez, do mais frequente para o menos.
[[nodiscard]] std::vector<CommandStatistic> commandStatistics(std::span<const core::ServerVariable> status);

} // namespace mydb::services::admin
