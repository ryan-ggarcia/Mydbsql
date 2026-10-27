#include "services/query/QueryService.hpp"

#include "infra/settings/MemorySettingsStore.hpp"
#include "sdk/SessionEvents.hpp"
#include "support/FakeServer.hpp"
#include "support/TestDispatcher.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mydb::services::Outcome;
using mydb::services::query::ExecutionMode;
using mydb::services::query::QueryHistoryStore;
using mydb::services::query::QueryRequest;
using mydb::services::query::QueryService;
using mydb::services::query::QuerySummary;
using mydb::services::query::StatementResult;
using mydb::services::session::Session;
using mydb::services::session::SessionService;
using mydb::tests::FakeResult;
using mydb::tests::FakeServer;
using mydb::tests::FakeServerConnection;
using mydb::tests::TestDispatcher;

/// "SELECT many" devolve 5 linhas; "SELECT NULL", uma linha nula; "SELECT x", uma linha com x; o que contém
/// FAIL falha; o resto afeta 2 linhas com 1 aviso.
FakeResult resultFor(std::string_view sql) {
    FakeResult result;
    if (sql.find("FAIL") != std::string_view::npos) {
        result.error = "falhou: " + std::string(sql);
        return result;
    }
    if (sql.starts_with("SELECT ")) {
        result.columns = {mydb::tests::fakeColumn("v")};
        const std::string_view value = sql.substr(7);
        if (value == "many") {
            for (int i = 0; i < 5; ++i) {
                result.rows.push_back({std::to_string(i)});
            }
        } else if (value == "NULL") {
            result.rows.push_back({std::nullopt});
        } else {
            result.rows.push_back({std::string(value)});
        }
        return result;
    }
    result.rowsAffected = 2;
    result.warnings = 1;
    return result;
}

/// Um resultado por sentença de um lote (as sentenças vêm separadas por ";\n").
std::vector<FakeResult> script(std::string_view sql) {
    std::vector<FakeResult> results;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t end = sql.find(";\n", begin);
        results.push_back(resultFor(sql.substr(begin, end == std::string_view::npos ? end : end - begin)));
        if (end == std::string_view::npos) {
            return results;
        }
        begin = end + 2;
    }
}

QueryRequest request(std::string sql) {
    QueryRequest r;
    r.sql = std::move(sql);
    return r;
}

class QueryServiceTest : public ::testing::Test {
protected:
    QueryServiceTest() {
        server.script = script;
        subscriptions.push_back(bus.subscribe<mydb::sdk::DatabaseChanged>(
            [this](const auto& e) { events.push_back("database:" + e.database); }));
        subscriptions.push_back(bus.subscribe<mydb::sdk::ObjectsChanged>(
            [this](const auto& e) { events.push_back("objects:" + e.database); }));
    }

    void SetUp() override {
        Session s;
        s.id = "s1";
        s.name = "Sessão";
        s.params.sessionId = s.id;
        s.params.defaultDatabase = "app";
        ASSERT_TRUE(sessions.add(s, std::make_unique<FakeServerConnection>(server)));
    }

    Outcome<QuerySummary> wait(std::optional<Outcome<QuerySummary>>& slot) {
        EXPECT_TRUE(ui.runUntil([&] { return slot.has_value(); }));
        return std::move(*slot);
    }

    Outcome<QuerySummary> execute(QueryRequest query, std::string_view sessionId = "s1") {
        std::optional<Outcome<QuerySummary>> outcome;
        queries.execute(
            sessionId, std::move(query), [this](const StatementResult& r) { results.push_back(r); },
            [&outcome](Outcome<QuerySummary> o) { outcome.emplace(std::move(o)); });
        return wait(outcome);
    }

    Outcome<QuerySummary> runFile(const std::filesystem::path& file, bool stopOnError) {
        std::optional<Outcome<QuerySummary>> outcome;
        queries.runFile(
            "s1", file, stopOnError, [this](const StatementResult& r) { results.push_back(r); },
            [&outcome](Outcome<QuerySummary> o) { outcome.emplace(std::move(o)); });
        return wait(outcome);
    }

    std::vector<std::string> executed() {
        const std::scoped_lock lock(server.mutex);
        return server.executed;
    }

    FakeServer server;
    mydb::sdk::EventBus bus;
    std::vector<mydb::sdk::EventBus::Subscription> subscriptions;
    std::vector<std::string> events;
    TestDispatcher ui;
    mydb::services::log::LogBuffer log;
    mydb::infra::settings::MemorySettingsStore settings;
    QueryHistoryStore history{settings};
    SessionService sessions{bus, ui.dispatcher(), log};
    QueryService queries{sessions, &history};
    std::vector<StatementResult> results;
};

TEST_F(QueryServiceTest, RunsStatementsInOrderAndSummarizes) {
    const auto outcome = execute(request("SELECT 7;\nSELECT NULL; UPDATE t SET a = 1"));
    ASSERT_TRUE(outcome.ok());
    EXPECT_EQ(executed(), (std::vector<std::string>{"SELECT 7", "SELECT NULL", "UPDATE t SET a = 1"}));
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].index, 0U);
    EXPECT_EQ(results[0].sql, "SELECT 7");
    ASSERT_TRUE(results[0].table.has_value());
    EXPECT_EQ(results[0].table->columns.front().name, "v");
    EXPECT_EQ(results[0].table->rows, (std::vector<std::vector<std::optional<std::string>>>{{"7"}}));
    EXPECT_FALSE(results[0].table->truncated);
    EXPECT_EQ(results[1].table->rows.front().front(), std::nullopt);
    EXPECT_EQ(results[2].index, 2U);
    EXPECT_FALSE(results[2].table.has_value());
    EXPECT_EQ(results[2].rowsAffected, 2);
    EXPECT_EQ(results[2].warnings, 1U);
    EXPECT_TRUE(results[2].error.empty());
    EXPECT_EQ(results[2].timing.server, std::chrono::microseconds{1});

    const QuerySummary& summary = outcome.value();
    EXPECT_EQ(summary.statements, 3U);
    EXPECT_EQ(summary.failed, 0U);
    EXPECT_FALSE(summary.cancelled);
    EXPECT_EQ(summary.rowsAffected, 2);
    EXPECT_EQ(summary.rowsReturned, 2U);
    EXPECT_EQ(summary.warnings, 1U);
    EXPECT_EQ(summary.timing.server, std::chrono::microseconds{3});
}

TEST_F(QueryServiceTest, ReportsLinesFromTheEditor) {
    QueryRequest query = request("SELECT 1;\n\n  SELECT 2");
    query.firstLine = 10;
    ASSERT_TRUE(execute(std::move(query)).ok());
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].line, 10U);
    EXPECT_EQ(results[1].line, 12U);
}

TEST_F(QueryServiceTest, TruncatesLargeResults) {
    QueryRequest query = request("SELECT many");
    query.maxRows = 2;
    const auto outcome = execute(std::move(query));
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].table->rows.size(), 2U);
    EXPECT_TRUE(results[0].table->truncated);
    EXPECT_EQ(outcome.value().rowsReturned, 2U);
}

TEST_F(QueryServiceTest, StopsOnTheFirstErrorWhenAsked) {
    const auto outcome = execute(request("SELECT 1; SELECT FAIL; SELECT 3"));
    EXPECT_EQ(executed(), (std::vector<std::string>{"SELECT 1", "SELECT FAIL"}));
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[1].error, "falhou: SELECT FAIL");
    EXPECT_FALSE(results[1].table.has_value());
    EXPECT_EQ(outcome.value().statements, 2U);
    EXPECT_EQ(outcome.value().failed, 1U);
}

TEST_F(QueryServiceTest, ContinuesAfterErrorsWhenAsked) {
    QueryRequest query = request("SELECT 1; SELECT FAIL; SELECT 3");
    query.stopOnError = false;
    const auto outcome = execute(std::move(query));
    EXPECT_EQ(executed().size(), 3U);
    EXPECT_EQ(outcome.value().statements, 3U);
    EXPECT_EQ(outcome.value().failed, 1U);
}

TEST_F(QueryServiceTest, ExplainPrefixesEachStatementAndHasNoEffects) {
    QueryRequest query = request("SELECT 1; USE logs");
    query.explain = true;
    ASSERT_TRUE(execute(std::move(query)).ok());
    EXPECT_EQ(executed(), (std::vector<std::string>{"EXPLAIN SELECT 1", "EXPLAIN USE logs"}));
    EXPECT_EQ(results[0].sql, "EXPLAIN SELECT 1");
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(sessions.find("s1")->currentDatabase(), "app");
}

TEST_F(QueryServiceTest, BatchGroupsStatementsExceptCompoundOnes) {
    QueryRequest query =
        request("SELECT 1; SELECT 2;\nCREATE PROCEDURE p() BEGIN SELECT 1; END;\nSELECT 3 -- fim\n;SELECT 4");
    query.mode = ExecutionMode::Batch;
    const auto outcome = execute(std::move(query));
    EXPECT_EQ(executed(), (std::vector<std::string>{"SELECT 1;\nSELECT 2", "CREATE PROCEDURE p() BEGIN SELECT 1; END",
                                                    "SELECT 3 -- fim", "SELECT 4"}));
    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[1].sql, "SELECT 2");
    EXPECT_EQ(results[1].table->rows.front().front(), "2");
    EXPECT_EQ(results[4].index, 4U);
    EXPECT_EQ(outcome.value().statements, 5U);
}

TEST_F(QueryServiceTest, BatchResumesAfterTheFailedStatement) {
    QueryRequest query = request("SELECT 1; SELECT FAIL; SELECT 3");
    query.mode = ExecutionMode::Batch;
    query.stopOnError = false;
    const auto outcome = execute(std::move(query));
    EXPECT_EQ(executed(), (std::vector<std::string>{"SELECT 1;\nSELECT FAIL;\nSELECT 3", "SELECT 3"}));
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[1].sql, "SELECT FAIL");
    EXPECT_FALSE(results[1].error.empty());
    EXPECT_EQ(results[2].sql, "SELECT 3");
    EXPECT_EQ(outcome.value().failed, 1U);

    results.clear();
    QueryRequest stopping = request("SELECT 1; SELECT FAIL; SELECT 3");
    stopping.mode = ExecutionMode::Batch;
    EXPECT_EQ(execute(std::move(stopping)).value().statements, 2U);
    EXPECT_EQ(results.size(), 2U);
}

TEST_F(QueryServiceTest, UseChangesTheCurrentDatabaseAndDdlRefreshes) {
    ASSERT_TRUE(execute(request("USE `lo``gs`;\nCREATE TABLE t (a INT); ALTER TABLE t ADD b INT")).ok());
    EXPECT_EQ(sessions.find("s1")->currentDatabase(), "lo`gs");
    EXPECT_EQ(events, (std::vector<std::string>{"database:lo`gs", "objects:lo`gs"}));

    events.clear();
    ASSERT_TRUE(execute(request("CREATE TABLE x (a INT); DROP DATABASE old")).ok());
    EXPECT_EQ(events, (std::vector<std::string>{"objects:"})) << "DDL de banco recarrega o servidor inteiro";

    events.clear();
    QueryRequest failing = request("CREATE TABLE FAIL (a INT); SELECT 1");
    failing.stopOnError = false;
    ASSERT_TRUE(execute(std::move(failing)).ok());
    EXPECT_TRUE(events.empty()) << "DDL que falhou não muda nada";
}

TEST_F(QueryServiceTest, LogsTheSqlOnceAsUserSql) {
    ASSERT_TRUE(execute(request("SELECT 42")).ok());
    int user = 0;
    int internal = 0;
    for (const auto& entry : log.since(0)) {
        if (entry.text == "SELECT 42") {
            (entry.category == mydb::core::LogCategory::UserSql ? user : internal) += 1;
        }
    }
    EXPECT_EQ(user, 1);
    EXPECT_EQ(internal, 0);
}

TEST_F(QueryServiceTest, RecordsHistory) {
    QueryRequest query = request("SELECT 1; SELECT FAIL");
    ASSERT_TRUE(execute(std::move(query)).ok());
    const auto entries = history.search("");
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].sql, "SELECT 1; SELECT FAIL");
    EXPECT_EQ(entries[0].sessionId, "s1");
    EXPECT_EQ(entries[0].database, "app");
    EXPECT_TRUE(entries[0].failed);
}

TEST_F(QueryServiceTest, CancelStopsTheRunningStatementAndTheRest) {
    server.blockNext = true;
    std::optional<Outcome<QuerySummary>> outcome;
    queries.execute(
        "s1", request("SELECT SLEEP(10); SELECT 2"), [this](const StatementResult& r) { results.push_back(r); },
        [&outcome](Outcome<QuerySummary> o) { outcome.emplace(std::move(o)); });
    ASSERT_TRUE(server.waitBlocked(std::chrono::seconds{5}));
    queries.cancel("s1");
    const auto summary = wait(outcome);
    ASSERT_TRUE(summary.ok());
    EXPECT_TRUE(summary.value().cancelled);
    EXPECT_EQ(summary.value().statements, 1U);
    EXPECT_EQ(summary.value().failed, 0U);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(results[0].error.empty());
    EXPECT_EQ(executed().size(), 1U);
}

TEST_F(QueryServiceTest, FailsWhenTheSessionIsNotOpen) {
    const auto outcome = execute(request("SELECT 1"), "nope");
    ASSERT_FALSE(outcome.ok());
    EXPECT_THROW(std::rethrow_exception(outcome.error()), mydb::core::ConnectionError);
    EXPECT_TRUE(results.empty());
}

class QueryServiceFileTest : public QueryServiceTest {
protected:
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove(m_file, ignored);
    }

    const std::filesystem::path& write(const std::string& text) {
        std::ofstream out(m_file, std::ios::binary);
        out << text;
        return m_file;
    }

    std::filesystem::path m_file =
        std::filesystem::temp_directory_path() / ("mydbsql-runfile-" + std::to_string(std::random_device{}()) + ".sql");
};

TEST_F(QueryServiceFileTest, StreamsTheFileWithoutKeepingRows) {
    const std::string big(100'000, 'x'); // maior que um bloco de leitura
    const auto outcome = runFile(write("DELIMITER //\nCREATE PROCEDURE p() BEGIN SELECT 1; END//\nDELIMITER ;\n"
                                       "SELECT many;\nUPDATE t SET a = 1;\nSELECT " +
                                       big + "\n"),
                                 true);
    ASSERT_TRUE(outcome.ok());
    const auto sent = executed();
    ASSERT_EQ(sent.size(), 4U);
    EXPECT_EQ(sent[0], "CREATE PROCEDURE p() BEGIN SELECT 1; END");
    EXPECT_EQ(sent[3].size(), big.size() + 7);
    ASSERT_EQ(results.size(), 4U);
    for (const StatementResult& result : results) {
        EXPECT_FALSE(result.table.has_value());
    }
    EXPECT_EQ(results[3].sql.size(), 1024U);
    EXPECT_EQ(results[3].line, 6U);
    EXPECT_EQ(outcome.value().rowsReturned, 6U);
    EXPECT_EQ(outcome.value().rowsAffected, 4);
    EXPECT_EQ(history.search("").front().sql.rfind("-- ", 0), 0U);
}

TEST_F(QueryServiceFileTest, StopsOnErrorAndReportsMissingFiles) {
    const auto outcome = runFile(write("SELECT 1;\nSELECT FAIL;\nSELECT 3;\n"), true);
    EXPECT_EQ(executed().size(), 2U);
    EXPECT_EQ(outcome.value().failed, 1U);

    const auto missing = runFile(m_file.string() + ".nada", true);
    EXPECT_FALSE(missing.ok());
}

} // namespace
