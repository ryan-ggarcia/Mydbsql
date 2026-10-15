#include "services/session/SessionRepository.hpp"

#include "infra/credentials/MemoryCredentialStore.hpp"
#include "infra/settings/MemorySettingsStore.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mydb::services::session {
namespace {

class SessionRepositoryTest : public ::testing::Test {
protected:
    infra::settings::MemorySettingsStore m_settings;
    infra::credentials::MemoryCredentialStore m_credentials;
    SessionRepository m_repository{m_settings, m_credentials};
};

TEST_F(SessionRepositoryTest, EmptyWhenNothingWasSaved) {
    m_repository.load();
    EXPECT_TRUE(m_repository.sessions().empty());
    EXPECT_TRUE(m_repository.folders().empty());
}

TEST_F(SessionRepositoryTest, SavesAndReloadsEveryField) {
    Session& created = m_repository.create("Produção", "Clientes/ACME");
    EXPECT_EQ(created.id.size(), 32U);
    EXPECT_EQ(created.params.sessionId, created.id);
    EXPECT_TRUE(created.statistics.created.has_value());

    Session session = created;
    session.color = "#336699";
    session.comment = "réplica de leitura";
    session.savePassword = false;
    session.params.host = "db.example.org";
    session.params.port = 3307;
    session.params.user = "app";
    session.params.defaultDatabase = "loja";
    session.params.queryTimeout = std::chrono::seconds{30};
    session.params.keepAlive = std::chrono::seconds{60};
    session.params.startupScript = "SET @a = 1;";
    session.params.ignoreDatabasePattern = "^tmp_";
    session.params.compressed = true;
    session.params.localTimeZone = true;
    session.params.ssl = core::SslOptions{.mode = core::SslOptions::Mode::VerifyCa,
                                          .privateKey = "chaves/cliente.key",
                                          .certificate = "chaves/cliente.pem",
                                          .certificateAuthority = "chaves/ca.pem",
                                          .cipher = "TLS_AES_256_GCM_SHA384"};
    session.params.ssh = core::SshTunnelOptions{.host = "bastion",
                                                .port = 2222,
                                                .user = "ops",
                                                .privateKey = "id_ed25519",
                                                .localPort = 13306,
                                                .timeout = std::chrono::seconds{15}};
    m_repository.update(session);
    m_repository.recordConnection(session.id, std::chrono::system_clock::now());
    m_repository.save();

    SessionRepository reloaded(m_settings, m_credentials);
    reloaded.load();
    ASSERT_EQ(reloaded.sessions().size(), 1U);
    const Session& back = reloaded.sessions().front();
    EXPECT_EQ(back.statistics.connections, 1U);
    EXPECT_TRUE(back.statistics.lastConnected.has_value());
    Session expected = session;
    expected.statistics = back.statistics;
    EXPECT_EQ(back, expected);
}

TEST_F(SessionRepositoryTest, PasswordsGoToTheCredentialStoreNeverToSettings) {
    const std::string id = m_repository.create("A").id;
    m_repository.setPassword(id, "s3gr3d0");
    m_repository.setSshPassword(id, "ssh-s3gr3d0");
    m_repository.save();

    EXPECT_EQ(m_credentials.read(passwordKey(id)), "s3gr3d0");
    EXPECT_EQ(m_credentials.read(sshPasswordKey(id)), "ssh-s3gr3d0");
    const auto document = m_settings.read(SessionRepository::kDocument);
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->find("s3gr3d0"), std::string::npos);
    EXPECT_EQ(m_repository.exportJson().find("s3gr3d0"), std::string::npos);

    m_repository.clearPassword(id);
    EXPECT_FALSE(m_credentials.read(passwordKey(id)).has_value());
    m_repository.remove(id);
    EXPECT_FALSE(m_credentials.read(sshPasswordKey(id)).has_value());
    EXPECT_TRUE(m_repository.sessions().empty());
}

TEST_F(SessionRepositoryTest, DuplicateCopiesSettingsAndPasswords) {
    Session& original = m_repository.create("Banco", "Pasta");
    original.params.host = "h";
    const std::string id = original.id;
    m_repository.setPassword(id, "pw");
    const Session copy = m_repository.duplicate(id);
    EXPECT_NE(copy.id, id);
    EXPECT_EQ(copy.name, "Banco (cópia)");
    EXPECT_EQ(copy.params.host, "h");
    EXPECT_EQ(copy.params.sessionId, copy.id);
    EXPECT_EQ(m_credentials.read(passwordKey(copy.id)), "pw");
}

TEST_F(SessionRepositoryTest, FoldersAreDerivedRenamedAndRemoved) {
    m_repository.create("A", "Prod/Clientes");
    m_repository.create("B", "Prod");
    m_repository.addFolder("Vazia");
    EXPECT_EQ(m_repository.folders(), (std::vector<std::string>{"Prod", "Prod/Clientes", "Vazia"}));

    m_repository.renameFolder("Prod", "Produção");
    EXPECT_EQ(m_repository.folders(), (std::vector<std::string>{"Produção", "Produção/Clientes", "Vazia"}));
    EXPECT_EQ(m_repository.sessions()[0].folder, "Produção/Clientes");

    EXPECT_THROW(m_repository.removeFolder("Produção"), std::logic_error);
    m_repository.removeFolder("Vazia");
    EXPECT_EQ(m_repository.folders(), (std::vector<std::string>{"Produção", "Produção/Clientes"}));
}

TEST_F(SessionRepositoryTest, ExportAndImportCreateNewSessions) {
    Session& session = m_repository.create("Exportada", "X");
    session.params.host = "srv";
    m_repository.recordConnection(session.id, std::chrono::system_clock::now());
    const std::string exported = m_repository.exportJson();
    EXPECT_EQ(exported.find("statistics"), std::string::npos);

    SessionRepository other(m_settings, m_credentials);
    EXPECT_EQ(other.importJson(exported), 1U);
    ASSERT_EQ(other.sessions().size(), 1U);
    EXPECT_NE(other.sessions()[0].id, session.id);
    EXPECT_EQ(other.sessions()[0].params.host, "srv");
    EXPECT_EQ(other.sessions()[0].statistics.connections, 0U);

    EXPECT_THROW(other.importJson("{\"format\": \"outro\"}"), SettingsError);
    EXPECT_THROW(other.importJson("não é json"), SettingsError);
}

TEST_F(SessionRepositoryTest, RejectsCorruptOrNewerDocuments) {
    m_settings.write(SessionRepository::kDocument, "{ quebrado");
    EXPECT_THROW(m_repository.load(), SettingsError);
    m_settings.write(SessionRepository::kDocument, R"({"schemaVersion": 99, "sessions": []})");
    EXPECT_THROW(m_repository.load(), SettingsError);
    m_settings.write(SessionRepository::kDocument, R"({"sessions": []})");
    EXPECT_THROW(m_repository.load(), SettingsError);
}

TEST_F(SessionRepositoryTest, UnknownSessionIsAnError) {
    Session ghost;
    ghost.id = "nao-existe";
    EXPECT_THROW(m_repository.update(ghost), std::out_of_range);
    EXPECT_THROW(m_repository.setPassword("nao-existe", "x"), std::out_of_range);
    EXPECT_EQ(m_repository.find("nao-existe"), nullptr);
}

} // namespace
} // namespace mydb::services::session
