#include "services/admin/ServerAdminService.hpp"

#include "core/errors/DbError.hpp"

#include <utility>

namespace mydb::services::admin {

namespace {

using session::WorkContext;

core::IServerAdmin& requireAdmin(const WorkContext& context) {
    core::IServerAdmin* admin = context.connection().capabilities().serverAdmin;
    if (admin == nullptr) {
        throw core::QueryError("Este servidor não oferece variáveis, status nem processos.");
    }
    return *admin;
}

} // namespace

void ServerAdminService::variables(std::string_view sessionId, Completion<ServerVariables> done) {
    m_sessions->run<ServerVariables>(
        sessionId,
        [](WorkContext& context) {
            core::IServerAdmin& admin = requireAdmin(context);
            return ServerVariables{.session = admin.variables(false), .global = admin.variables(true)};
        },
        std::move(done));
}

void ServerAdminService::setVariable(std::string_view sessionId, core::ServerVariable change, Completion<Done> done) {
    m_sessions->run<Done>(
        sessionId,
        [variable = std::move(change)](WorkContext& context) {
            requireAdmin(context).setVariable(variable);
            return Done{};
        },
        std::move(done));
}

void ServerAdminService::status(std::string_view sessionId, Completion<std::vector<core::ServerVariable>> done) {
    m_sessions->run<std::vector<core::ServerVariable>>(
        sessionId, [](WorkContext& context) { return requireAdmin(context).status(); }, std::move(done));
}

void ServerAdminService::processes(std::string_view sessionId, Completion<std::vector<core::ServerProcess>> done) {
    m_sessions->run<std::vector<core::ServerProcess>>(
        sessionId, [](WorkContext& context) { return requireAdmin(context).processes(); }, std::move(done));
}

void ServerAdminService::killProcess(std::string_view sessionId, std::uint64_t id, Completion<Done> done) {
    m_sessions->run<Done>(
        sessionId,
        [id](WorkContext& context) {
            requireAdmin(context).killProcess(id);
            return Done{};
        },
        std::move(done));
}

void ServerAdminService::commandStatistics(std::string_view sessionId, Completion<std::vector<CommandStatistic>> done) {
    m_sessions->run<std::vector<CommandStatistic>>(
        sessionId, [](WorkContext& context) { return admin::commandStatistics(requireAdmin(context).status()); },
        std::move(done));
}

} // namespace mydb::services::admin
