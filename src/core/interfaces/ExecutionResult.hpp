#pragma once

#include "core/interfaces/IResultSet.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace mydb::core {

/// O que uma execução devolve. Um lote pode trazer vários resultados (ex.: CALL no MySQL).
struct ExecutionResult {
    std::vector<std::unique_ptr<IResultSet>> resultSets;
    std::int64_t rowsAffected{-1}; ///< -1 quando não se aplica (SELECT)
    std::uint32_t warnings{0};
    std::chrono::microseconds serverTime{0};
    std::chrono::microseconds networkTime{0};
};

} // namespace mydb::core
