#pragma once

#include "core/interfaces/OptionalCapabilities.hpp"

namespace mydb::drivers::mysql {

class MySqlConnection;

/// Aba Host do MySQL/MariaDB: SHOW VARIABLES, SHOW STATUS, SHOW FULL PROCESSLIST e KILL.
class MySqlServerAdmin final : public core::IServerAdmin {
public:
    explicit MySqlServerAdmin(MySqlConnection& connection) noexcept : m_connection(&connection) {}

    [[nodiscard]] std::vector<core::ServerVariable> variables(bool global) override;
    /// Números e DEFAULT vão como estão; o resto, como string. Lança QueryError para nomes inválidos.
    void setVariable(const core::ServerVariable& variable) override;
    [[nodiscard]] std::vector<core::ServerVariable> status() override;
    [[nodiscard]] std::vector<core::ServerProcess> processes() override;
    void killProcess(std::uint64_t id) override;

private:
    MySqlConnection* m_connection;
};

} // namespace mydb::drivers::mysql
