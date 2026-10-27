#include "services/query/QueryHistoryStore.hpp"

#include "infra/settings/MemorySettingsStore.hpp"
#include "services/session/SessionRepository.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace {

using mydb::services::query::HistoryEntry;
using mydb::services::query::QueryHistoryStore;
using std::chrono::system_clock;

HistoryEntry entry(std::string sql, std::string sessionId = "s1", system_clock::time_point time = system_clock::now()) {
    HistoryEntry e;
    // Precisão de milissegundos, como no documento gravado.
    e.time = std::chrono::time_point_cast<std::chrono::milliseconds>(time);
    e.sessionId = std::move(sessionId);
    e.database = "app";
    e.sql = std::move(sql);
    e.duration = std::chrono::milliseconds{12};
    return e;
}

std::vector<std::string> texts(const std::vector<HistoryEntry>& entries) {
    std::vector<std::string> list;
    for (const HistoryEntry& e : entries) {
        list.push_back(e.sql);
    }
    return list;
}

TEST(QueryHistoryStoreTest, PersistsAndSearchesNewestFirst) {
    mydb::infra::settings::MemorySettingsStore settings;
    {
        QueryHistoryStore history(settings);
        history.load();
        history.add(entry("SELECT 1"));
        history.add(entry("select * FROM users", "s2"));
        HistoryEntry failed = entry("DROP TABLE x");
        failed.failed = true;
        history.add(failed);
    }
    QueryHistoryStore reloaded(settings);
    reloaded.load();
    const auto all = reloaded.search("");
    EXPECT_EQ(texts(all), (std::vector<std::string>{"DROP TABLE x", "select * FROM users", "SELECT 1"}));
    EXPECT_TRUE(all.front().failed);
    EXPECT_EQ(all.front().duration, std::chrono::milliseconds{12});
    EXPECT_EQ(all.front().database, "app");
    EXPECT_EQ(texts(reloaded.search("SELECT")), (std::vector<std::string>{"select * FROM users", "SELECT 1"}));
    EXPECT_EQ(texts(reloaded.search("select", "s1")), (std::vector<std::string>{"SELECT 1"}));
    EXPECT_TRUE(reloaded.search("nada").empty());
}

TEST(QueryHistoryStoreTest, DropsExpiredAndExcessEntries) {
    mydb::infra::settings::MemorySettingsStore settings;
    QueryHistoryStore history(settings, 30, 2);
    history.add(entry("velha", "s1", system_clock::now() - std::chrono::hours{24 * 40}));
    EXPECT_TRUE(history.search("").empty());
    history.add(entry("a"));
    history.add(entry("b"));
    history.add(entry("c"));
    EXPECT_EQ(texts(history.search("")), (std::vector<std::string>{"c", "b"}));

    QueryHistoryStore forever(settings, 0, 10);
    forever.add(entry("antiga", "s1", system_clock::now() - std::chrono::hours{24 * 400}));
    forever.add(entry("recente", "s1", system_clock::now() - std::chrono::hours{24 * 5}));
    EXPECT_EQ(forever.search("").size(), 2U);
    forever.setKeepDays(3);
    EXPECT_TRUE(forever.search("").empty());
}

TEST(QueryHistoryStoreTest, ClearPersists) {
    mydb::infra::settings::MemorySettingsStore settings;
    QueryHistoryStore history(settings);
    history.add(entry("SELECT 1"));
    history.clear();
    QueryHistoryStore reloaded(settings);
    reloaded.load();
    EXPECT_TRUE(reloaded.search("").empty());
}

TEST(QueryHistoryStoreTest, RejectsCorruptedAndNewerDocuments) {
    mydb::infra::settings::MemorySettingsStore settings;
    QueryHistoryStore history(settings);
    settings.write(QueryHistoryStore::kDocument, "{ not json");
    EXPECT_THROW(history.load(), mydb::services::session::SettingsError);
    settings.write(QueryHistoryStore::kDocument, R"({"schemaVersion": 99, "entries": []})");
    EXPECT_THROW(history.load(), mydb::services::session::SettingsError);
    settings.write(QueryHistoryStore::kDocument, R"({"schemaVersion": 1, "entries": [{"sql": "sem data"}]})");
    EXPECT_THROW(history.load(), mydb::services::session::SettingsError);
    EXPECT_TRUE(history.search("").empty());
}

TEST(QueryHistoryStoreTest, DoesNotOverwriteAnUnreadableDocumentUntilCleared) {
    mydb::infra::settings::MemorySettingsStore settings;
    const std::string newer = R"({"schemaVersion": 99, "entries": []})";
    settings.write(QueryHistoryStore::kDocument, newer);
    QueryHistoryStore history(settings, 1);
    EXPECT_THROW(history.load(), mydb::services::session::SettingsError);
    history.add(entry("SELECT 1"));
    history.setKeepDays(0);
    EXPECT_EQ(texts(history.search("")), (std::vector<std::string>{"SELECT 1"})) << "segue em memória";
    EXPECT_EQ(settings.read(QueryHistoryStore::kDocument), newer);

    history.clear();
    history.add(entry("SELECT 2"));
    QueryHistoryStore reloaded(settings);
    reloaded.load();
    EXPECT_EQ(texts(reloaded.search("")), (std::vector<std::string>{"SELECT 2"}));
}

} // namespace
