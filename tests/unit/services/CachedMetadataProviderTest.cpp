#include "services/session/CachedMetadataProvider.hpp"

#include "services/session/LruCache.hpp"
#include "support/FakeServer.hpp"

#include <gtest/gtest.h>

namespace {

using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::services::session::CachedMetadataProvider;
using mydb::services::session::LruCache;
using mydb::tests::FakeMetadata;
using mydb::tests::FakeServer;

ObjectRef table(std::string database, std::string name) {
    return ObjectRef{.database = std::move(database), .schema = {}, .name = std::move(name), .type = ObjectType::Table};
}

DbObject object(ObjectRef ref) {
    DbObject result;
    result.ref = std::move(ref);
    return result;
}

TEST(LruCacheTest, EvictsTheLeastRecentlyUsed) {
    LruCache<int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    ASSERT_NE(cache.find("a"), nullptr); // "a" passa a ser o mais recente
    cache.put("c", 3);
    EXPECT_EQ(cache.find("b"), nullptr);
    EXPECT_EQ(*cache.find("a"), 1);
    EXPECT_EQ(*cache.find("c"), 3);
    EXPECT_EQ(cache.size(), 2U);
}

TEST(LruCacheTest, ReplacesAndErasesByPrefix) {
    LruCache<int> cache(10);
    cache.put("app\x1ft1", 1);
    cache.put("app\x1ft2", 2);
    cache.put("application\x1ft1", 3);
    cache.put("app\x1ft1", 4);
    EXPECT_EQ(*cache.find("app\x1ft1"), 4);
    cache.eraseWithPrefix("app\x1f");
    EXPECT_EQ(cache.find("app\x1ft1"), nullptr);
    EXPECT_EQ(cache.find("app\x1ft2"), nullptr);
    EXPECT_EQ(*cache.find("application\x1ft1"), 3);
}

class CachedMetadataProviderTest : public ::testing::Test {
protected:
    CachedMetadataProviderTest() {
        server.objects["app"] = {object(table("app", "users")), object(table("app", "orders"))};
        server.objects["logs"] = {object(table("logs", "events"))};
    }

    FakeServer server;
    FakeMetadata source{server};
    CachedMetadataProvider cache{source, CachedMetadataProvider::Limits{.databases = 2, .objects = 4}};
};

TEST_F(CachedMetadataProviderTest, AsksTheSourceOnlyOnce) {
    EXPECT_EQ(cache.databases(), (std::vector<std::string>{"app", "logs"}));
    EXPECT_EQ(cache.databases().size(), 2U);
    EXPECT_EQ(cache.objects("app").size(), 2U);
    EXPECT_EQ(cache.objects("app").size(), 2U);
    (void)cache.table(table("app", "users"));
    (void)cache.table(table("app", "users"));
    EXPECT_EQ(server.databaseCalls.load(), 1);
    EXPECT_EQ(server.objectCalls.load(), 1);
    EXPECT_EQ(server.tableCalls.load(), 1);
}

TEST_F(CachedMetadataProviderTest, InvalidatingADatabaseKeepsTheOthers) {
    (void)cache.objects("app");
    (void)cache.objects("logs");
    (void)cache.table(table("app", "users"));
    (void)cache.table(table("logs", "events"));
    cache.invalidateDatabase("app");
    (void)cache.objects("app");
    (void)cache.objects("logs");
    (void)cache.table(table("app", "users"));
    (void)cache.table(table("logs", "events"));
    EXPECT_EQ(server.objectCalls.load(), 3);
    EXPECT_EQ(server.tableCalls.load(), 3);
    EXPECT_EQ(server.databaseCalls.load(), 0);
}

TEST_F(CachedMetadataProviderTest, InvalidatingAnObjectRefreshesItsDatabaseList) {
    (void)cache.objects("app");
    (void)cache.table(table("app", "users"));
    (void)cache.table(table("app", "orders"));
    cache.invalidateObject(table("app", "users"));
    (void)cache.objects("app");
    (void)cache.table(table("app", "users"));
    (void)cache.table(table("app", "orders"));
    EXPECT_EQ(server.objectCalls.load(), 2);
    EXPECT_EQ(server.tableCalls.load(), 3);
}

TEST_F(CachedMetadataProviderTest, InvalidateAllClearsEverything) {
    (void)cache.databases();
    (void)cache.databaseSummaries();
    (void)cache.objects("app");
    cache.invalidateAll();
    (void)cache.databases();
    (void)cache.databaseSummaries();
    (void)cache.objects("app");
    EXPECT_EQ(server.summaryCalls.load(), 2);
    EXPECT_EQ(server.objectCalls.load(), 2);
}

TEST_F(CachedMetadataProviderTest, KeepsAtMostTheConfiguredNumberOfDatabases) {
    server.objects["third"] = {};
    (void)cache.objects("app");
    (void)cache.objects("logs");
    (void)cache.objects("third"); // descarta "app", o usado há mais tempo
    (void)cache.objects("logs");
    (void)cache.objects("app");
    EXPECT_EQ(server.objectCalls.load(), 4);
}

TEST_F(CachedMetadataProviderTest, FailuresAreNotCached) {
    server.failDatabase = "app";
    EXPECT_THROW((void)cache.objects("app"), mydb::core::QueryError);
    server.failDatabase.clear();
    EXPECT_EQ(cache.objects("app").size(), 2U);
}

} // namespace
