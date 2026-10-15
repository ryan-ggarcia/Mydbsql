#pragma once

#include "core/interfaces/IConnection.hpp"
#include "core/interfaces/ICredentialStore.hpp"
#include "core/interfaces/IDriverCatalog.hpp"
#include "services/session/Session.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace mydb::services::session {

/// Abre a conexão de uma sessão salva. Para sessões que não guardam a senha, a senha digitada fica só
/// na memória (num cofre sobreposto ao do SO) enquanto o app estiver aberto, e nunca vai para o disco.
/// O SessionConnector precisa viver mais que as conexões que cria: elas leem a senha dele ao reconectar.
/// connect() pode ser chamado de uma thread de trabalho.
class SessionConnector {
public:
    SessionConnector(core::IDriverCatalog& drivers, core::ICredentialStore& savedCredentials);
    SessionConnector(const SessionConnector&) = delete;
    SessionConnector& operator=(const SessionConnector&) = delete;
    SessionConnector(SessionConnector&&) = delete;
    SessionConnector& operator=(SessionConnector&&) = delete;
    ~SessionConnector();

    /// Cria e abre a conexão. `password` substitui a senha salva. Lança ConnectionError (inclusive para
    /// driver inexistente) e os demais DbError da abertura.
    [[nodiscard]] std::unique_ptr<core::IConnection> connect(const Session& session,
                                                             const std::optional<std::string>& password = {});

private:
    class OverlayStore;

    core::IDriverCatalog& m_drivers;
    std::mutex m_driversMutex;
    std::unique_ptr<OverlayStore> m_credentials;
};

} // namespace mydb::services::session
