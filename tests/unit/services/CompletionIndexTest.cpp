#include "services/query/CompletionIndex.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using mydb::core::model::Column;
using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::core::model::TableDefinition;
using mydb::core::sql::SqlSyntax;
using mydb::services::query::CompletionIndex;
using mydb::services::query::CompletionKind;

DbObject object(std::string database, std::string name, ObjectType type = ObjectType::Table) {
    DbObject o;
    o.ref = ObjectRef{.database = std::move(database), .schema = {}, .name = std::move(name), .type = type};
    return o;
}

TableDefinition table(std::string database, std::string name, const std::vector<std::string>& columns) {
    TableDefinition t;
    t.ref =
        ObjectRef{.database = std::move(database), .schema = {}, .name = std::move(name), .type = ObjectType::Table};
    for (const std::string& columnName : columns) {
        Column c;
        c.name = columnName;
        c.typeName = "INT";
        t.columns.push_back(c);
    }
    return t;
}

class CompletionIndexTest : public ::testing::Test {
protected:
    CompletionIndexTest() {
        index.setKeywords({"SELECT", "SET", "FROM", "WHERE"});
        index.setFunctions({{.name = "SUM", .signature = "SUM(expr)", .description = "Soma"},
                            {.name = "SUBSTRING", .signature = "SUBSTRING(str, pos)", .description = "Trecho"}});
        index.setDatabases({"app", "logs"}, "app");
        index.setObjects("app", {object("app", "users"), object("app", "user_roles"),
                                 object("app", "active_users", ObjectType::View),
                                 object("app", "audit", ObjectType::Trigger)});
        index.setObjects("logs", {object("logs", "events")});
        index.setColumns(table("app", "users", {"id", "name", "email"}));
        index.setColumns(table("logs", "events", {"id", "message"}));
    }

    /// Os textos sugeridos com o cursor no "|" de `sql`.
    std::vector<std::string> at(std::string sql) const {
        const std::size_t cursor = sql.find('|');
        sql.erase(cursor, 1);
        std::vector<std::string> texts;
        for (const auto& completion : index.complete(sql, cursor)) {
            texts.push_back(completion.text);
        }
        return texts;
    }

    CompletionIndex index{SqlSyntax::mysql()};
};

TEST_F(CompletionIndexTest, CompletesKeywordsFunctionsAndObjectsByPrefix) {
    EXPECT_EQ(at("se|"), (std::vector<std::string>{"SELECT", "SET"}));
    EXPECT_EQ(at("SELECT * FROM us|"), (std::vector<std::string>{"user_roles", "users"}));
    EXPECT_EQ(at("SELECT SU|"), (std::vector<std::string>{"SUBSTRING", "SUM"}));
    EXPECT_EQ(at("SELECT * FROM l|"), (std::vector<std::string>{"logs"}));
    EXPECT_EQ(at("SELECT * FROM ac|"), (std::vector<std::string>{"active_users"}));
    EXPECT_TRUE(at("SELECT * FROM aud|").empty()) << "triggers não entram no autocomplete";
    EXPECT_EQ(at("SEL|ECT"), (std::vector<std::string>{"SELECT"}));
}

TEST_F(CompletionIndexTest, ExactCasePrefixComesFirst) {
    index.setObjects("app", {object("app", "Settings"), object("app", "selection")});
    EXPECT_EQ(at("Se|"), (std::vector<std::string>{"Settings", "SELECT", "selection", "SET"}));
}

TEST_F(CompletionIndexTest, ReportsKindAndDetail) {
    const auto result = index.complete("SELECT SUM", 10);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].kind, CompletionKind::Function);
    EXPECT_EQ(result[0].detail, "SUM(expr)");
}

TEST_F(CompletionIndexTest, CompletesColumnsAfterTableAliasOrDatabase) {
    EXPECT_EQ(at("SELECT users.| FROM users"), (std::vector<std::string>{"email", "id", "name"}));
    EXPECT_EQ(at("SELECT u.n| FROM users AS u"), (std::vector<std::string>{"name"}));
    EXPECT_EQ(at("SELECT e.| FROM logs.events e JOIN users u ON u.id = e.id"),
              (std::vector<std::string>{"id", "message"}));
    EXPECT_EQ(at("SELECT * FROM logs.|"), (std::vector<std::string>{"events"}));
    EXPECT_EQ(at("SELECT logs.events.m|"), (std::vector<std::string>{"message"}));
    EXPECT_EQ(at("SELECT `u`.`na|` FROM `users` `u`"), (std::vector<std::string>{"name"}));
    EXPECT_TRUE(at("SELECT nada.|").empty());
}

TEST_F(CompletionIndexTest, OffersAliasesAndColumnsOfCitedTables) {
    const auto result = at("SELECT | FROM users u, logs.events AS ev WHERE 1");
    EXPECT_NE(std::ranges::find(result, "u"), result.end());
    EXPECT_NE(std::ranges::find(result, "ev"), result.end());
    EXPECT_NE(std::ranges::find(result, "email"), result.end());
    EXPECT_NE(std::ranges::find(result, "message"), result.end());
    EXPECT_EQ(at("SELECT e| FROM users u, logs.events AS ev"), (std::vector<std::string>{"email", "ev"}));
}

TEST_F(CompletionIndexTest, AliasesStayInsideTheirStatement) {
    EXPECT_TRUE(at("SELECT * FROM users zz; SELECT zz|").empty());
    EXPECT_EQ(at("SELECT zz.i|; SELECT * FROM users zz"), std::vector<std::string>{});
}

TEST_F(CompletionIndexTest, StringsAndCommentsCompleteNothing) {
    EXPECT_TRUE(at("SELECT 'se|").empty());
    EXPECT_TRUE(at("SELECT 1 -- se|").empty());
    EXPECT_TRUE(at("SELECT /* se| */ 1").empty());
    EXPECT_FALSE(at("SELECT 1 -- x\nse|").empty());
}

TEST_F(CompletionIndexTest, LimitsResults) {
    EXPECT_EQ(index.complete("", 0, 3).size(), 3U);
    EXPECT_EQ(index.complete("x", 99).size(), 0U);
}

TEST_F(CompletionIndexTest, ReplacingDataDropsTheOldEntries) {
    index.setDatabases({"other"}, "other");
    EXPECT_EQ(at("SELECT * FROM l|"), std::vector<std::string>{});
    EXPECT_TRUE(at("SELECT * FROM us|").empty()) << "o banco atual mudou";
    index.setColumns(table("app", "users", {"login"}));
    EXPECT_EQ(at("SELECT app.users.|"), (std::vector<std::string>{"login"}));
    index.setSnippets({"select_all"});
    EXPECT_EQ(at("sel|"), (std::vector<std::string>{"select_all", "SELECT"}));
}

TEST_F(CompletionIndexTest, MissingTablesListsCitedTablesWithoutColumns) {
    index.setObjects("app",
                     {object("app", "users"), object("app", "Orders"), object("app", "recent", ObjectType::View)});
    const std::string sql = "SELECT * FROM users u JOIN orders o ON o.uid = u.id JOIN recent r JOIN ghost g "
                            "JOIN logs.events e JOIN logs.unknown_here; UPDATE other SET a = 1";
    const auto missing = index.missingTables(sql, sql.find("u JOIN"));
    EXPECT_EQ(missing, (std::vector<ObjectRef>{
                           {.database = "app", .schema = {}, .name = "Orders", .type = ObjectType::Table},
                           {.database = "app", .schema = {}, .name = "recent", .type = ObjectType::View},
                       }));

    const std::string typing = "SELECT * FROM logs.mess";
    EXPECT_TRUE(index.missingTables(typing, typing.size()).empty()) << "o nome em digitação fica de fora";
    const std::string unknownDb = "SELECT * FROM other.t x";
    EXPECT_EQ(index.missingTables(unknownDb, unknownDb.size()),
              (std::vector<ObjectRef>{{.database = "other", .schema = {}, .name = "t", .type = ObjectType::Table}}));
}

TEST(CompletionCatalogTest, ParsesTheMySqlCatalog) {
    std::ifstream file(std::string(MYDB_FUNCTIONS_DIR) + "/mysql.json", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::stringstream text;
    text << file.rdbuf();
    const auto functions = mydb::services::query::parseFunctionCatalog(text.str());
    EXPECT_GT(functions.size(), 150U);
    std::set<std::string> names;
    for (const auto& function : functions) {
        EXPECT_FALSE(function.signature.empty()) << function.name;
        EXPECT_FALSE(function.description.empty()) << function.name;
        EXPECT_TRUE(names.insert(function.name).second) << "repetida: " << function.name;
    }
    EXPECT_TRUE(names.contains("CONCAT"));
    EXPECT_TRUE(names.contains("JSON_EXTRACT"));
}

TEST(CompletionCatalogTest, RejectsInvalidCatalogs) {
    using mydb::services::query::parseFunctionCatalog;
    EXPECT_THROW((void)parseFunctionCatalog("{"), std::runtime_error);
    EXPECT_THROW((void)parseFunctionCatalog(R"json({"functions": [{"signature": "X()"}]})json"), std::runtime_error);
    EXPECT_TRUE(parseFunctionCatalog(R"({"functions": []})").empty());
}

TEST(CompletionCatalogTest, KeywordsAreSortedUniqueAndDialectSpecific) {
    using mydb::services::query::sqlKeywords;
    const auto mysql = sqlKeywords("mysql");
    const auto generic = sqlKeywords("sqlite");
    EXPECT_TRUE(std::ranges::is_sorted(mysql));
    EXPECT_EQ(std::ranges::adjacent_find(mysql), mysql.end());
    EXPECT_NE(std::ranges::find(mysql, "AUTO_INCREMENT"), mysql.end());
    EXPECT_EQ(std::ranges::find(generic, "AUTO_INCREMENT"), generic.end());
    EXPECT_NE(std::ranges::find(generic, "SELECT"), generic.end());
}

} // namespace
