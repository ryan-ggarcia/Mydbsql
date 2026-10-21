#include "services/session/SessionService.hpp"

#include "sdk/SessionEvents.hpp"
#include "services/admin/ServerAdminService.hpp"
#include "support/FakeServer.hpp"
#include "support/TestDispatcher.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::services::Done;
using mydb::services::Outcome;
using mydb::services::session::Session;
using mydb::services::session::SessionService;
using mydb::services::session::WorkContext;
using mydb::tests::FakeServer;
using mydb::tests::FakeServerConnection;
using mydb::tests::TestDispatcher;

Session session(std::string id) {
    Session s;
    s.id = std::move(id);
    s.name = "Sessão " + s.id;
    s.params.sessionId = s.id;
    s.params.defaultDatabase = "app";
    return s;
}

class SessionServiceTest : public ::testing::Test {
protected:
    SessionServiceTest() {
        server.objects["app"] = {DbObject{.ref = ObjectRef{.database = "app", .name = "users"}}};
        subscriptions.push_back(bus.subscribe<mydb::sdk::SessionOpened>(
            [this](const auto& e) { events.push_back("opened:" + e.sessionId); }));
        subscriptions.push_back(bus.subscribe<mydb::sdk::SessionClosed>(
            [this](const auto& e) { events.push_back("closed:" + e.sessionId); }));
        subscriptions.push_back(bus.subscribe<mydb::sdk::DatabaseChanged>(
            [this](const auto& e) { events.push_back("database:" + e.sessionId + ":" + e.database); }));
        subscriptions.push_back(bus.subscribe<mydb::sdk::ObjectsChanged>([this](const auto& e) {
            events.push_back("objects:" + e.sessionId + ":" + e.database + ":" + (e.object ? e.object->name : ""));
        }));
    }

    void open(const std::string& id = "s1") {
        ASSERT_TRUE(service.add(session(id), std::make_unique<FakeServerConnection>(server)));
    }

    template <typename T>
    Outcome<T> wait(std::optional<Outcome<T>>& slot) {
        EXPECT_TRUE(ui.runUntil([&] { return slot.has_value(); }));
        return std::move(*slot);
    }

    FakeServer server;
    mydb::sdk::EventBus bus;
    std::vector<mydb::sdk::EventBus::Subscription> subscriptions;
    std::vector<std::string> events;
    TestDispatcher ui;
    mydb::services::log::LogBuffer log;
    SessionService service{bus, ui.dispatcher(), log};
};

TEST_F(SessionServiceTest, AddPublishesOpenedAndRejectsDuplicates) {
    open();
    EXPECT_FALSE(service.add(session("s1"), std::make_unique<FakeServerConnection>(server)));
    ASSERT_EQ(service.sessions().size(), 1U);
    const auto* opened = service.find("s1");
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(opened->server().productName, "Fake");
    EXPECT_EQ(opened->currentDatabase(), "app");
    EXPECT_FALSE(opened->hasServerAdmin());
    EXPECT_EQ(events, (std::vector<std::string>{"opened:s1"}));
}

TEST_F(SessionServiceTest, MetadataRunsOnTheWorkerAndIsCached) {
    open();
    std::optional<Outcome<std::vector<std::string>>> first;
    std::optional<Outcome<std::vector<std::string>>> second;
    service.databases("s1", [&](auto outcome) { first.emplace(std::move(outcome)); });
    service.databases("s1", [&](auto outcome) { second.emplace(std::move(outcome)); });
    EXPECT_EQ(wait(first).value(), (std::vector<std::string>{"app", "logs"}));
    EXPECT_TRUE(wait(second).ok());
    EXPECT_EQ(server.databaseCalls.load(), 1);
}

TEST_F(SessionServiceTest, ConnectionSqlGoesToTheLog) {
    open();
    std::optional<Outcome<std::vector<std::string>>> result;
    service.databases("s1", [&](auto outcome) { result.emplace(std::move(outcome)); });
    (void)wait(result);
    bool logged = false;
    for (const auto& entry : log.since(0)) {
        logged = logged || (entry.category == mydb::core::LogCategory::Sql && entry.text == "SHOW DATABASES" &&
                            entry.sessionId == "s1");
    }
    EXPECT_TRUE(logged);
}

TEST_F(SessionServiceTest, RefreshInvalidatesAndPublishes) {
    open();
    std::optional<Outcome<std::vector<DbObject>>> before;
    service.objects("s1", "app", [&](auto outcome) { before.emplace(std::move(outcome)); });
    EXPECT_EQ(wait(before).value().size(), 1U);

    service.refresh("s1", "app");
    std::optional<Outcome<std::vector<DbObject>>> after;
    service.objects("s1", "app", [&](auto outcome) { after.emplace(std::move(outcome)); });
    EXPECT_TRUE(wait(after).ok());
    EXPECT_EQ(server.objectCalls.load(), 2);
    EXPECT_EQ(events.back(), "objects:s1:app:");
}

TEST_F(SessionServiceTest, UseDatabaseUpdatesStateAndPublishes) {
    open();
    std::optional<Outcome<Done>> result;
    service.useDatabase("s1", "logs", [&](auto outcome) { result.emplace(std::move(outcome)); });
    EXPECT_TRUE(wait(result).ok());
    EXPECT_EQ(service.find("s1")->currentDatabase(), "logs");
    EXPECT_EQ(server.usedDatabase, "logs");
    EXPECT_EQ(events.back(), "database:s1:logs");
}

TEST_F(SessionServiceTest, FailuresReachTheCallbackAndTheLog) {
    open();
    server.failDatabase = "secret";
    std::optional<Outcome<std::vector<DbObject>>> result;
    service.objects("s1", "secret", [&](auto outcome) { result.emplace(std::move(outcome)); });
    const auto outcome = wait(result);
    ASSERT_FALSE(outcome.ok());
    EXPECT_EQ(mydb::services::session::describe(outcome.error()), "Access denied\nConfira os privilégios.");
    const auto entries = log.since(0);
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries.back().category, mydb::core::LogCategory::Error);
}

TEST_F(SessionServiceTest, WorkOnAClosedSessionFails) {
    std::optional<Outcome<int>> result;
    service.run<int>("nope", [](WorkContext&) { return 1; }, [&](auto outcome) { result.emplace(std::move(outcome)); });
    EXPECT_FALSE(wait(result).ok());
}

TEST_F(SessionServiceTest, CloseCancelsAndPublishes) {
    open("s1");
    open("s2");
    ASSERT_EQ(service.sessions().size(), 2U);
    service.close("s1");
    EXPECT_EQ(service.find("s1"), nullptr);
    EXPECT_NE(service.find("s2"), nullptr);
    EXPECT_EQ(events.back(), "closed:s1");
    service.closeAll();
    EXPECT_TRUE(service.sessions().empty());
    EXPECT_TRUE(server.closed);
}

TEST_F(SessionServiceTest, CancelInterruptsRunningWork) {
    open();
    server.blockNext = true;
    std::optional<Outcome<Done>> result;
    service.run<Done>(
        "s1",
        [](WorkContext& context) {
            (void)context.connection().execute("SELECT SLEEP(100)");
            return Done{};
        },
        [&](auto outcome) { result.emplace(std::move(outcome)); });
    ASSERT_TRUE(server.waitBlocked(std::chrono::seconds{5}));
    service.cancel("s1");
    const auto outcome = wait(result);
    ASSERT_FALSE(outcome.ok());
    EXPECT_THROW(std::rethrow_exception(outcome.error()), mydb::core::CancelledError);
}

TEST_F(SessionServiceTest, ServerAdminWithoutCapabilityFails) {
    open();
    mydb::services::admin::ServerAdminService admin(service);
    std::optional<Outcome<std::vector<mydb::core::ServerVariable>>> result;
    admin.status("s1", [&](auto outcome) { result.emplace(std::move(outcome)); });
    const auto outcome = wait(result);
    ASSERT_FALSE(outcome.ok());
    EXPECT_THROW(std::rethrow_exception(outcome.error()), mydb::core::QueryError);
}

} // namespace
