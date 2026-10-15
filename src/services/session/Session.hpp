#pragma once

#include "core/interfaces/ConnectionParams.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mydb::services::session {

struct SessionStatistics {
    using TimePoint = std::chrono::system_clock::time_point;

    std::uint64_t connections{0};
    std::optional<TimePoint> created;
    std::optional<TimePoint> lastConnected;

    friend bool operator==(const SessionStatistics&, const SessionStatistics&) = default;
};

/// Uma sessão salva no gerenciador de sessões. Não guarda senhas: elas ficam no ICredentialStore, nas
/// chaves "<id>/password" e "<id>/ssh-password" (CLAUDE.md §0).
struct Session {
    std::string id; ///< estável e único; também é ConnectionParams::sessionId
    std::string name;
    std::string folder; ///< caminho com "/", ex.: "Produção/Clientes"; vazio = raiz
    std::string color;  ///< "#RRGGBB" ou vazio
    std::string comment;
    bool savePassword{true}; ///< false: a senha é pedida a cada conexão
    core::ConnectionParams params;
    SessionStatistics statistics;

    friend bool operator==(const Session&, const Session&) = default;
};

[[nodiscard]] inline std::string passwordKey(std::string_view sessionId) {
    return std::string(sessionId) + "/password";
}
[[nodiscard]] inline std::string sshPasswordKey(std::string_view sessionId) {
    return std::string(sessionId) + "/ssh-password";
}

} // namespace mydb::services::session
