#pragma once

#include "core/interfaces/IResultSet.hpp"

#include <chrono>
#include <cstdint>

namespace mydb::core {

/// Uma execução em andamento de IConnection::execute(). Entrega os resultados de um lote um por vez,
/// em streaming (CLAUDE.md §2.2, regra 4; ADR-0008): nada é lido do servidor antes de ser pedido.
///
/// Uso:
///   auto execution = connection.execute("SELECT 1; UPDATE t SET a = 1");
///   while (execution->nextResult()) {
///       if (IResultSet* rows = execution->resultSet()) { while (rows->next()) { … } }
///       else { execution->rowsAffected(); }
///   }
///
/// Destruir a execução antes do fim descarta o que faltar ler e deixa a conexão pronta para a próxima.
/// A execução precisa ser destruída antes da conexão; só uma execução por conexão existe de cada vez.
class IExecution {
public:
    struct Timing {
        std::chrono::microseconds server{0};  ///< até o servidor responder a cada sentença
        std::chrono::microseconds network{0}; ///< lendo linhas
    };

    virtual ~IExecution() = default;

    /// Avança para o resultado da próxima sentença. false quando o lote acabou.
    /// Lança QueryError se a sentença falhar (as seguintes não são executadas) e CancelledError se cancelada.
    [[nodiscard]] virtual bool nextResult() = 0;

    /// Linhas do resultado atual, ou nullptr se a sentença não devolve linhas (INSERT, UPDATE, DDL…).
    /// Vale até a próxima chamada de nextResult().
    [[nodiscard]] virtual IResultSet* resultSet() = 0;

    /// Linhas afetadas pela sentença atual; -1 quando não se aplica (SELECT).
    [[nodiscard]] virtual std::int64_t rowsAffected() const = 0;
    [[nodiscard]] virtual std::uint32_t warnings() const = 0;
    [[nodiscard]] virtual Timing timing() const = 0;

protected:
    IExecution() = default;
    IExecution(const IExecution&) = default;
    IExecution(IExecution&&) = default;
    IExecution& operator=(const IExecution&) = default;
    IExecution& operator=(IExecution&&) = default;
};

} // namespace mydb::core
