// Critério de pronto da Fase 3: criar uma sessão MySQL, salvar, "reabrir o app" e conectar sem digitar
// a senha de novo. Usa o arquivo JSON de verdade, o cofre de senhas do SO (quando existe) e o driver
// carregado como plugin, contra o servidor de MYDB_TEST_MYSQL (ou MYDB_TEST_MARIADB).

#include "app/ModuleDriverCatalog.hpp"
#include "app/ModuleHost.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"
#include "infra/platform/IPlatform.hpp"
#include "infra/platform/SystemCredentialStore.hpp"
#include "infra/settings/JsonSettingsStore.hpp"
#include "services/session/SessionConnector.hpp"
#include "services/session/SessionRepository.hpp"
#include "support/TestServers.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <random>
#include <string>

namespace mydb::contract {
namespace {

TEST(SessionEndToEndTest, SavedSessionReconnectsWithoutTypingThePassword) {
    const char* raw = std::getenv("MYDB_TEST_MYSQL"); // NOLINT(concurrency-mt-unsafe)
    if (raw == nullptr || *raw == '\0') {
        raw = std::getenv("MYDB_TEST_MARIADB"); // NOLINT(concurrency-mt-unsafe)
    }
    if (raw == nullptr || *raw == '\0') {
        GTEST_SKIP() << "MYDB_TEST_MYSQL/MYDB_TEST_MARIADB não definida";
    }
    auto server = tests::parseServer(raw);
    // root sem senha não prova nada: cria um usuário com senha só para este teste.
    const std::string user = "mydb_e2e";
    const std::string password = "s3nh@-" + std::to_string(std::random_device{}());

    const auto platform = infra::platform::createPlatform();
    app::ModuleHost modules(*platform);
    app::ModuleDriverCatalog drivers(modules, MYDB_MODULE_DIR);
    infra::credentials::MemoryCredentialStore rootCredentials;
    {
        services::session::SessionConnector rootConnector(drivers, rootCredentials);
        services::session::Session root;
        root.id = "root";
        root.params.driverId = "mysql";
        root.params.host = server["host"];
        root.params.port = static_cast<std::uint16_t>(std::stoi(server["port"]));
        root.params.user = server["user"];
        auto admin = rootConnector.connect(root, server["password"]);
        const auto run = [&](const std::string& sql) {
            const auto execution = admin->execute(sql);
            while (execution->nextResult()) {
            }
        };
        run("DROP USER IF EXISTS '" + user + "'@'%'");
        run("CREATE USER '" + user + "'@'%' IDENTIFIED BY '" + password + "'");
    }

    const std::filesystem::path config =
        std::filesystem::temp_directory_path() / ("mydbsql-e2e-" + std::to_string(std::random_device{}()));
    std::unique_ptr<core::ICredentialStore> vault = infra::platform::createSystemCredentialStore();
    if (!vault) {
        vault = std::make_unique<infra::credentials::MemoryCredentialStore>(); // Linux sem secret service
    }

    std::string id;
    {
        infra::settings::JsonSettingsStore settings(config);
        services::session::SessionRepository repository(settings, *vault);
        repository.load();
        services::session::Session& session = repository.create("E2E");
        session.params.host = server["host"];
        session.params.port = static_cast<std::uint16_t>(std::stoi(server["port"]));
        session.params.user = user;
        id = session.id;
        repository.setPassword(id, password);
        repository.save();
    }

    // "Reabre o app": objetos novos, lendo o JSON do disco e a senha do cofre.
    {
        infra::settings::JsonSettingsStore settings(config);
        services::session::SessionRepository repository(settings, *vault);
        repository.load();
        const services::session::Session* session = repository.find(id);
        ASSERT_NE(session, nullptr);
        services::session::SessionConnector connector(drivers, *vault);
        const auto connection = connector.connect(*session); // sem senha digitada
        EXPECT_GT(connection->serverInfo().version, 0U);
        const auto execution = connection->execute("SELECT CURRENT_USER()");
        ASSERT_TRUE(execution->nextResult());
        ASSERT_TRUE(execution->resultSet()->next());
        EXPECT_EQ(execution->resultSet()->text(0).substr(0, user.size()), user);
        repository.remove(id); // apaga a senha do cofre
        repository.save();
    }
    EXPECT_FALSE(vault->read(id + "/password").has_value());

    std::error_code ignored;
    std::filesystem::remove_all(config, ignored);
}

} // namespace
} // namespace mydb::contract
