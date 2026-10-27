#include "services/query/QueryDocuments.hpp"

#include "infra/settings/MemorySettingsStore.hpp"
#include "services/session/SessionRepository.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using mydb::services::query::QueryTabsStore;
using mydb::services::query::QueryTabState;
using mydb::services::query::Snippet;
using mydb::services::query::SnippetStore;
using mydb::services::session::SettingsError;

TEST(SnippetStoreTest, PutReplacesByNameAndPersists) {
    mydb::infra::settings::MemorySettingsStore settings;
    {
        SnippetStore snippets(settings);
        snippets.load();
        EXPECT_TRUE(snippets.snippets().empty());
        snippets.put(Snippet{.name = "contar", .sql = "SELECT COUNT(*) FROM t"});
        snippets.put(Snippet{.name = "agora", .sql = "SELECT NOW()"});
        snippets.put(Snippet{.name = "contar", .sql = "SELECT COUNT(1) FROM t"});
    }
    SnippetStore reloaded(settings);
    reloaded.load();
    EXPECT_EQ(reloaded.snippets(), (std::vector<Snippet>{{.name = "contar", .sql = "SELECT COUNT(1) FROM t"},
                                                         {.name = "agora", .sql = "SELECT NOW()"}}));
    reloaded.remove("contar");
    reloaded.remove("inexistente");
    SnippetStore again(settings);
    again.load();
    EXPECT_EQ(again.snippets(), (std::vector<Snippet>{{.name = "agora", .sql = "SELECT NOW()"}}));
}

TEST(SnippetStoreTest, RejectsCorruptedAndNewerDocuments) {
    mydb::infra::settings::MemorySettingsStore settings;
    SnippetStore snippets(settings);
    settings.write(SnippetStore::kDocument, "[");
    EXPECT_THROW(snippets.load(), SettingsError);
    settings.write(SnippetStore::kDocument, R"({"schemaVersion": 99, "snippets": []})");
    EXPECT_THROW(snippets.load(), SettingsError);
    settings.write(SnippetStore::kDocument, R"({"schemaVersion": 1, "snippets": {}})");
    EXPECT_THROW(snippets.load(), SettingsError);
    settings.write(SnippetStore::kDocument, R"({"schemaVersion": 1, "snippets": [{"name": "x"}]})");
    EXPECT_THROW(snippets.load(), SettingsError);
    EXPECT_TRUE(snippets.snippets().empty());
}

TEST(QueryTabsStoreTest, RoundTripsTabs) {
    mydb::infra::settings::MemorySettingsStore settings;
    const QueryTabsStore tabs(settings);
    EXPECT_TRUE(tabs.load().empty());
    const std::vector<QueryTabState> saved{
        {.title = "Query #1", .sessionId = "s1", .sql = "SELECT 1;\nSELECT 'ç'", .file = {}},
        {.title = "relatorio.sql", .sessionId = {}, .sql = {}, .file = "C:/sql/relatorio.sql"},
    };
    tabs.save(saved);
    EXPECT_EQ(QueryTabsStore(settings).load(), saved);
    tabs.save({});
    EXPECT_TRUE(tabs.load().empty());
}

TEST(QueryTabsStoreTest, RejectsCorruptedAndNewerDocuments) {
    mydb::infra::settings::MemorySettingsStore settings;
    const QueryTabsStore tabs(settings);
    settings.write(QueryTabsStore::kDocument, "nada");
    EXPECT_THROW((void)tabs.load(), SettingsError);
    settings.write(QueryTabsStore::kDocument, R"({"schemaVersion": 2, "tabs": []})");
    EXPECT_THROW((void)tabs.load(), SettingsError);
    settings.write(QueryTabsStore::kDocument, R"({"schemaVersion": 1})");
    EXPECT_THROW((void)tabs.load(), SettingsError);
}

} // namespace
