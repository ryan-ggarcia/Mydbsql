#include "services/session/SessionConnector.hpp"

#include "core/errors/DbError.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"
#include "support/FakeDriver.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mydb::services::session {
namespace {

using tests::FakeCatalog;

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
