#pragma once

#include "core/interfaces/IExecution.hpp"
#include "core/interfaces/IResultSet.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mydb::services::query {

enum class ExecutionMode : std::uint8_t {
    OneByOne, ///< uma ida ao servidor por sentença
    Batch,    ///< várias sentenças por ida ao servidor (multi-statements), menos as que têm BEGIN…END
};

struct QueryRequest {
    std::string sql;            ///< o texto a executar (tudo, a seleção ou a sentença atual: quem chama recorta)
    std::uint32_t firstLine{1}; ///< linha do editor onde `sql` começa, para apontar erros na linha certa
    ExecutionMode mode{ExecutionMode::OneByOne};
    bool stopOnError{true};
    bool explain{false}; ///< executa "EXPLAIN <sentença>" no lugar de cada sentença
    /// shortcut: o resultado é materializado até este limite (o resto é descartado e `truncated` fica true);
    /// a grade em streaming (ResultBuffer, fetchMore) substitui isto na Fase 6.
    std::size_t maxRows{1000};
};

/// Linhas de um resultado, já copiadas do IResultSet.
struct ResultTable {
    std::vector<core::ColumnInfo> columns;
    std::vector<std::vector<std::optional<std::string>>> rows; ///< nullopt = NULL
    bool truncated{false};
};

/// O que aconteceu com uma sentença.
struct StatementResult {
    std::size_t index{0};       ///< posição da sentença na execução, a partir de 0
    std::string sql;            ///< a sentença como foi enviada
    std::uint32_t line{1};      ///< linha no editor
    std::optional<ResultTable> table; ///< vazio quando a sentença não devolve linhas
    std::int64_t rowsAffected{-1};
    std::uint32_t warnings{0};
    core::IExecution::Timing timing;
    std::string error; ///< vazio = sucesso
};

/// Fim de uma execução.
struct QuerySummary {
    std::size_t statements{0}; ///< sentenças executadas (com ou sem erro)
    std::size_t failed{0};
    bool cancelled{false};
    std::int64_t rowsAffected{0}; ///< soma das que se aplicam
    std::uint64_t rowsReturned{0};
    std::uint32_t warnings{0};
    core::IExecution::Timing timing; ///< soma
};

} // namespace mydb::services::query
