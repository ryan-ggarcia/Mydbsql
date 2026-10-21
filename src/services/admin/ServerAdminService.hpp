#pragma once

#include "services/admin/CommandStatistics.hpp"
#include "services/session/SessionService.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace mydb::services::admin {

struct ServerVariables {
    std::vector<core::ServerVariable> session;
    std::vector<core::ServerVariable> global;
};

/// Aba Host: variáveis, status, processos e estatísticas de comandos de uma sessão aberta, pelo IServerAdmin da
/// conexão. Assíncrono como o SessionService; sem IServerAdmin, as chamadas falham com QueryError.
class ServerAdminService {
public:
    explicit ServerAdminService(session::SessionService& sessions) noexcept : m_sessions(&sessions) {}

    void variables(std::string_view sessionId, Completion<ServerVariables> done);
    void setVariable(std::string_view sessionId, core::ServerVariable change, Completion<Done> done);
    void status(std::string_view sessionId, Completion<std::vector<core::ServerVariable>> done);
    void processes(std::string_view sessionId, Completion<std::vector<core::ServerProcess>> done);
    void killProcess(std::string_view sessionId, std::uint64_t id, Completion<Done> done);
    void commandStatistics(std::string_view sessionId, Completion<std::vector<CommandStatistic>> done);

private:
    session::SessionService* m_sessions;
};

} // namespace mydb::services::admin
