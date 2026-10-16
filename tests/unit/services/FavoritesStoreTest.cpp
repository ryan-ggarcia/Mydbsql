#include "services/session/FavoritesStore.hpp"

#include "infra/settings/MemorySettingsStore.hpp"
#include "services/session/SessionRepository.hpp"

#include <gtest/gtest.h>

namespace {

using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::services::session::FavoritesStore;

const ObjectRef kUsers{.database = "app", .schema = {}, .name = "users", .type = ObjectType::Table};
const ObjectRef kReport{.database = "app", .schema = {}, .name = "report", .type = ObjectType::Procedure};

TEST(FavoritesStoreTest, MarksAndPersists) {
    mydb::infra::settings::MemorySettingsStore settings;
    {
        FavoritesStore favorites(settings);
        favorites.load();
        EXPECT_TRUE(favorites.set("s1", kUsers, true));
        EXPECT_FALSE(favorites.set("s1", kUsers, true));
        EXPECT_TRUE(favorites.set("s1", kReport, true));
        EXPECT_TRUE(favorites.contains("s1", kUsers));
        EXPECT_FALSE(favorites.contains("s2", kUsers));
    }
    FavoritesStore reloaded(settings);
    reloaded.load();
    EXPECT_EQ(reloaded.list("s1"), (std::vector<ObjectRef>{kUsers, kReport}));
    EXPECT_TRUE(reloaded.set("s1", kUsers, false));
    EXPECT_EQ(reloaded.list("s1"), (std::vector<ObjectRef>{kReport}));
    reloaded.removeSession("s1");
    EXPECT_TRUE(reloaded.list("s1").empty());
}

TEST(FavoritesStoreTest, RejectsCorruptedAndNewerDocuments) {
    mydb::infra::settings::MemorySettingsStore settings;
    settings.write(FavoritesStore::kDocument, "{ not json");
    FavoritesStore favorites(settings);
    EXPECT_THROW(favorites.load(), mydb::services::session::SettingsError);
    settings.write(FavoritesStore::kDocument, R"({"schemaVersion": 99, "sessions": {}})");
    EXPECT_THROW(favorites.load(), mydb::services::session::SettingsError);
}

} // namespace
