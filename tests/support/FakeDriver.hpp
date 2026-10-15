#pragma once

// Driver falso para testes de services e de UI: não conecta a nada e registra a senha que leu do cofre.

#include "core/errors/DbError.hpp"
#include "core/interfaces/IDriverCatalog.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mydb::tests {

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
    bool ping(bool /*reconnect*/) override { return true; }
    std::unique_ptr<core::IExecution> execute(std::string_view /*sql*/) override { return nullptr; }
    void cancelCurrent() override {}
    void useDatabase(std::string_view /*database*/) override {}
    core::ServerInfo serverInfo() const override {
        return core::ServerInfo{.productName = "Fake", .versionString = "1.0", .version = 10000, .connectionId = 1};
    }
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

} // namespace mydb::tests
