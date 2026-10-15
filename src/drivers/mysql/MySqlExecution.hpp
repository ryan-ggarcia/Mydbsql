#pragma once

#include "core/interfaces/IExecution.hpp"
#include "drivers/mysql/MySqlResultSet.hpp"

#include <memory>

namespace mydb::drivers::mysql {

class MySqlConnection;

/// Percorre os resultados de um lote com mysql_next_result, um por vez.
class MySqlExecution final : public core::IExecution {
public:
    MySqlExecution(MySqlConnection& connection, std::chrono::microseconds serverTime);
    MySqlExecution(const MySqlExecution&) = delete;
    MySqlExecution& operator=(const MySqlExecution&) = delete;
    MySqlExecution(MySqlExecution&&) = delete;
    MySqlExecution& operator=(MySqlExecution&&) = delete;
    ~MySqlExecution() override;

    [[nodiscard]] bool nextResult() override;
    [[nodiscard]] core::IResultSet* resultSet() override { return m_result.get(); }
    [[nodiscard]] std::int64_t rowsAffected() const override { return m_rowsAffected; }
    [[nodiscard]] std::uint32_t warnings() const override { return m_warnings; }
    [[nodiscard]] Timing timing() const override { return m_timing; }

    /// Chamado pela conexão quando outra execução começa ou a conexão fecha: descarta o resto e se solta.
    void detach() noexcept;

private:
    enum class State : std::uint8_t {
        Pending,  ///< o primeiro resultado já chegou, mas nextResult() ainda não foi chamado
        InResult, ///< posicionada num resultado
        Finished,
    };

    void loadCurrent();
    void drain() noexcept;

    MySqlConnection* m_connection;
    State m_state{State::Pending};
    std::unique_ptr<MySqlResultSet> m_result;
    std::int64_t m_rowsAffected{-1};
    std::uint32_t m_warnings{0};
    Timing m_timing;
};

} // namespace mydb::drivers::mysql
