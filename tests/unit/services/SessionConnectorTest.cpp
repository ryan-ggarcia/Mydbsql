#include "services/session/SessionConnector.hpp"

#include "core/errors/DbError.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mydb::services::session {
namespace {

/// Conexão falsa: registra a senha que o driver leu do cofre ao abrir.
class FakeConnection final : public core::IConnection {
public:
    FakeConnection(core::ConnectionParams params, core::ICredentialStore& credentials, std::string& seenPassword)
        : m_params(std::move(params)), m_credentials(&credentials), m_seenPassword(&seenPassword) {}

    void open() override {
        *m_seenPassword = m_credentials->read(m_params.sessionId + "/password").value_or("<nenhuma>");
        if (*m_seenPassword == "errada") {
            throw core::ConnectionError("Access denied");
        }
    }
    void close() noexcept override {}
    bool ping(bool) override { return true; }
    std::unique_ptr<core::IExecution> execute(std::string_view) override { return nullptr; }
    void cancelCurrent() override {}
    void useDatabase(std::string_view) override {}
    core::ServerInfo serverInfo() const override { return {}; }
    const core::ISqlDialect& dialect() const override { throw std::logic_error("não usado"); }
    core::IMetadataProvider& metadata() override { throw std::logic_error("não usado"); }
    core::Capabilities capabilities() override { return {}; }

private:
    core::ConnectionParams m_params;
    core::ICredentialStore* m_credentials;
    std::string* m_seenPassword;
};

class FakeDriver final : public core::IDriver {
public:
    explicit FakeDriver(std::string& seenPassword) : m_seenPassword(&seenPassword) {}
    std::string_view id() const noexcept override { return "fake"; }
    std::string displayName() const override { return "Fake"; }
    std::unique_ptr<core::IConnection> createConnection(const core::ConnectionParams& params,
                                                        core::ICredentialStore& credentials) const override {
        return std::make_unique<FakeConnection>(params, credentials, *m_seenPassword);
    }

private:
    std::string* m_seenPassword;
};

class FakeCatalog final : public core::IDriverCatalog {
public:
    explicit FakeCatalog(std::string& seenPassword) : m_driver(seenPassword) {}
    std::vector<std::string> availableDrivers() const override { return {"fake"}; }
    const core::IDriver* driver(std::string_view id) override { return id == "fake" ? &m_driver : nullptr; }

private:
    FakeDriver m_driver;
};

class SessionConnectorTest : public ::testing::Test {
protected:
    Session session(std::string id, std::string driver = "fake") {
        Session s;
        s.id = std::move(id);
        s.params.driverId = std::move(driver);
        return s;
    }

    std::string m_seenPassword;
    FakeCatalog m_catalog{m_seenPassword};
    infra::credentials::MemoryCredentialStore m_saved;
    SessionConnector m_connector{m_catalog, m_saved};
};

TEST_F(SessionConnectorTest, UsesTheSavedPassword) {
    m_saved.write("s1/password", "salva");
    EXPECT_NE(m_connector.connect(session("s1")), nullptr);
    EXPECT_EQ(m_seenPassword, "salva");
}

TEST_F(SessionConnectorTest, TypedPasswordWinsAndStaysOnlyInMemory) {
    m_saved.write("s1/password", "salva");
    (void)m_connector.connect(session("s1"), std::string("digitada"));
    EXPECT_EQ(m_seenPassword, "digitada");
    EXPECT_EQ(m_saved.read("s1/password"), "salva"); // o cofre do SO não muda
}

TEST_F(SessionConnectorTest, UnknownDriverAndOpenFailuresAreConnectionErrors) {
    EXPECT_THROW((void)m_connector.connect(session("s2", "inexistente")), core::ConnectionError);
    EXPECT_THROW((void)m_connector.connect(session("s3"), std::string("errada")), core::ConnectionError);
}

} // namespace
} // namespace mydb::services::session
