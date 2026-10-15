#include "services/session/SessionConnector.hpp"

#include "core/errors/DbError.hpp"

namespace mydb::services::session {

/// Lê primeiro as senhas digitadas nesta execução do app e depois as salvas no cofre do SO.
class SessionConnector::OverlayStore final : public core::ICredentialStore {
public:
    explicit OverlayStore(core::ICredentialStore& saved) : m_saved(&saved) {}

    void setTransient(std::string key, std::string secret) {
        const std::scoped_lock lock(m_mutex);
        m_transient.insert_or_assign(std::move(key), std::move(secret));
    }

    [[nodiscard]] std::optional<std::string> read(std::string_view key) override {
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_transient.find(key); found != m_transient.end()) {
                return found->second;
            }
        }
        return m_saved->read(key);
    }

    void write(std::string_view key, std::string_view secret) override { m_saved->write(key, secret); }

    void erase(std::string_view key) override {
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_transient.find(key); found != m_transient.end()) {
                m_transient.erase(found);
            }
        }
        m_saved->erase(key);
    }

    [[nodiscard]] bool persistent() const noexcept override { return m_saved->persistent(); }

private:
    core::ICredentialStore* m_saved;
    std::mutex m_mutex;
    std::map<std::string, std::string, std::less<>> m_transient;
};

SessionConnector::SessionConnector(core::IDriverCatalog& drivers, core::ICredentialStore& savedCredentials)
    : m_drivers(drivers), m_credentials(std::make_unique<OverlayStore>(savedCredentials)) {}

SessionConnector::~SessionConnector() = default;

std::unique_ptr<core::IConnection> SessionConnector::connect(const Session& session,
                                                             const std::optional<std::string>& password) {
    if (password) {
        m_credentials->setTransient(passwordKey(session.id), *password);
    }
    const core::IDriver* driver = nullptr;
    {
        // O catálogo carrega módulos: uma carga por vez.
        const std::scoped_lock lock(m_driversMutex);
        driver = m_drivers.driver(session.params.driverId);
    }
    if (driver == nullptr) {
        throw core::ConnectionError(core::DbErrorInfo{.message = "Driver não instalado: " + session.params.driverId,
                                                      .code = 0,
                                                      .sqlState = {},
                                                      .hint = "Confira se o plugin mydb_driver_" +
                                                              session.params.driverId + " está na pasta de módulos."});
    }
    core::ConnectionParams params = session.params;
    params.sessionId = session.id;
    auto connection = driver->createConnection(params, *m_credentials);
    connection->open();
    return connection;
}

} // namespace mydb::services::session
