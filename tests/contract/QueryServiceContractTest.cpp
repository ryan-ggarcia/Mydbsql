// QueryService contra MySQL 8 e MariaDB 11 de verdade (CLAUDE.md §5, Fase 5): lotes, DELIMITER, USE, DDL,
// cancelamento e arquivo .sql em streaming. Servidores por MYDB_TEST_MYSQL e MYDB_TEST_MARIADB.

#include "drivers/mysql/MySqlDriver.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"
#include "sdk/SessionEvents.hpp"
#include "services/query/QueryService.hpp"
#include "support/TestDispatcher.hpp"
#include "support/TestServers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace mydb::contract {
namespace {

using services::Outcome;
using services::query::ExecutionMode;
using services::query::QueryRequest;
using services::query::QueryService;
using services::query::QuerySummary;
using services::query::StatementResult;

QueryRequest request(std::string sql) {
    QueryRequest r;
    r.sql = std::move(sql);
    return r;
}

class QueryServiceContractTest : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override {
        const auto server = tests::serverFromEnvironment(GetParam());
        if (!server) {
            GTEST_SKIP() << GetParam() << " não definida; servidor ausente";
        }
        credentials.write("q/password", tests::serverValue(*server, "password", ""));
        services::session::Session session;
        session.id = "q";
        session.name = "contrato";
        session.params.sessionId = "q";
        session.params.driverId = "mysql";
        session.params.host = tests::serverValue(*server, "host", "127.0.0.1");
        session.params.port = static_cast<std::uint16_t>(std::stoi(tests::serverValue(*server, "port", "3306")));
        session.params.user = tests::serverValue(*server, "user", "root");
        auto connection =
            drivers::mysql::MySqlDriver(MYDB_MARIADB_PLUGIN_DIR).createConnection(session.params, credentials);
        connection->open();
        ASSERT_TRUE(sessions.add(session, std::move(connection)));
        subscription = bus.subscribe<sdk::ObjectsChanged>([this](const auto& e) { refreshed.push_back(e.database); });
        ASSERT_TRUE(execute(request("DROP DATABASE IF EXISTS mydb_query_contract; "
                                    "CREATE DATABASE mydb_query_contract; USE mydb_query_contract"))
                        .ok());
        results.clear();
        refreshed.clear();
    }

    void TearDown() override {
        if (sessions.find("q") != nullptr) {
            (void)execute(request("DROP DATABASE IF EXISTS mydb_query_contract"));
        }
    }

    Outcome<QuerySummary> wait(std::optional<Outcome<QuerySummary>>& slot) {
        EXPECT_TRUE(ui.runUntil([&] { return slot.has_value(); }, std::chrono::seconds{30}));
        return std::move(*slot);
    }

    Outcome<QuerySummary> execute(QueryRequest query) {
        std::optional<Outcome<QuerySummary>> outcome;
        queries.execute(
            "q", std::move(query), [this](const StatementResult& r) { results.push_back(r); },
            [&outcome](Outcome<QuerySummary> o) { outcome.emplace(std::move(o)); });
        return wait(outcome);
    }

    infra::credentials::MemoryCredentialStore credentials;
    sdk::EventBus bus;
    sdk::EventBus::Subscription subscription;
    std::vector<std::string> refreshed;
    tests::TestDispatcher ui;
    services::log::LogBuffer log;
    services::session::SessionService sessions{bus, ui.dispatcher(), log};
    QueryService queries{sessions, nullptr};
    std::vector<StatementResult> results;
};

TEST_P(QueryServiceContractTest, RunsScriptsWithDelimiterAndRoutines) {
    for (const ExecutionMode mode : {ExecutionMode::OneByOne, ExecutionMode::Batch}) {
        results.clear();
        QueryRequest query = request("DROP PROCEDURE IF EXISTS p;\n"
                                     "CREATE TABLE IF NOT EXISTS t (id INT PRIMARY KEY, nome VARCHAR(20));\n"
                                     "DELETE FROM t;\n"
                                     "INSERT INTO t VALUES (1, 'um'), (2, NULL);\n"
                                     "DELIMITER //\n"
                                     "CREATE PROCEDURE p() BEGIN SELECT COUNT(*) FROM t; END//\n"
                                     "DELIMITER ;\n"
                                     "SELECT id, nome FROM t ORDER BY id;\n"
                                     "CALL p()");
        query.mode = mode;
        const auto outcome = execute(std::move(query));
        ASSERT_TRUE(outcome.ok());
        EXPECT_EQ(outcome.value().failed, 0U) << (results.empty() ? "" : results.back().error);
        const auto select =
            std::ranges::find_if(results, [](const StatementResult& r) { return r.sql.starts_with("SELECT id"); });
        ASSERT_NE(select, results.end());
        ASSERT_TRUE(select->table.has_value());
        EXPECT_EQ(select->table->rows,
                  (std::vector<std::vector<std::optional<std::string>>>{{"1", "um"}, {"2", std::nullopt}}));
        EXPECT_EQ(select->line, 8U);
        const auto insert =
            std::ranges::find_if(results, [](const StatementResult& r) { return r.sql.starts_with("INSERT"); });
        ASSERT_NE(insert, results.end());
        EXPECT_EQ(insert->rowsAffected, 2);
        const auto call = std::ranges::find_if(
            results, [](const StatementResult& r) { return r.sql == "CALL p()" && r.table.has_value(); });
        ASSERT_NE(call, results.end());
        EXPECT_EQ(call->table->rows.front().front(), "2");
    }
    EXPECT_FALSE(refreshed.empty()) << "o DDL recarrega o banco atual";
}

TEST_P(QueryServiceContractTest, ReportsErrorsAndContinuesWhenAsked) {
    QueryRequest query = request("SELECT 1; SELECT * FROM tabela_que_nao_existe; SELECT 3");
    query.stopOnError = false;
    const auto outcome = execute(std::move(query));
    ASSERT_TRUE(outcome.ok());
    ASSERT_EQ(results.size(), 3U);
    EXPECT_NE(results[1].error.find("tabela_que_nao_existe"), std::string::npos) << results[1].error;
    EXPECT_EQ(results[2].table->rows.front().front(), "3");
    EXPECT_EQ(outcome.value().failed, 1U);
}

TEST_P(QueryServiceContractTest, UseChangesTheSessionDatabase) {
    ASSERT_TRUE(execute(request("USE information_schema")).ok());
    EXPECT_EQ(sessions.find("q")->currentDatabase(), "information_schema");
    ASSERT_TRUE(execute(request("SELECT DATABASE()")).ok());
    EXPECT_EQ(results.back().table->rows.front().front(), "information_schema");
}

TEST_P(QueryServiceContractTest, CancelInterruptsALongQuery) {
    std::optional<Outcome<QuerySummary>> outcome;
    queries.execute(
        "q", request("SELECT SLEEP(30); SELECT 2"), [this](const StatementResult& r) { results.push_back(r); },
        [&outcome](Outcome<QuerySummary> o) { outcome.emplace(std::move(o)); });
    const auto started = std::chrono::steady_clock::now();
    // Dá tempo de a sentença chegar ao servidor antes de cancelar.
    (void)ui.runUntil([] { return false; }, std::chrono::milliseconds{500});
    queries.cancel("q");
    const auto summary = wait(outcome);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{15});
    ASSERT_TRUE(summary.ok());
    EXPECT_TRUE(summary.value().cancelled);
    EXPECT_EQ(results.size(), 1U);
    // A conexão continua utilizável.
    results.clear();
    ASSERT_TRUE(execute(request("SELECT 5")).ok());
    EXPECT_EQ(results.at(0).table->rows.front().front(), "5");
}

TEST_P(QueryServiceContractTest, RunsAFileInStreaming) {
    const auto file = std::filesystem::temp_directory_path() /
                      ("mydbsql-contract-" + std::to_string(std::random_device{}()) + ".sql");
    {
        std::ofstream out(file, std::ios::binary);
        out << "CREATE TABLE f (id INT PRIMARY KEY, v VARCHAR(40));\n";
        for (int i = 0; i < 2000; ++i) {
            out << "INSERT INTO f VALUES (" << i << ", 'linha " << i << "; com ponto e vírgula');\n";
        }
        out << "SELECT * FROM f;\n";
    }
    std::optional<Outcome<QuerySummary>> outcome;
    queries.runFile(
        "q", file, true, [this](const StatementResult& r) { results.push_back(r); },
        [&outcome](Outcome<QuerySummary> o) { outcome.emplace(std::move(o)); });
    const auto summary = wait(outcome);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    ASSERT_TRUE(summary.ok());
    EXPECT_EQ(summary.value().statements, 2002U);
    EXPECT_EQ(summary.value().failed, 0U);
    EXPECT_EQ(summary.value().rowsAffected, 2000);
    EXPECT_EQ(summary.value().rowsReturned, 2000U);
}

INSTANTIATE_TEST_SUITE_P(Servers, QueryServiceContractTest, ::testing::Values("MYDB_TEST_MYSQL", "MYDB_TEST_MARIADB"),
                         [](const auto& test) { return std::string(test.param).substr(10); });

} // namespace
} // namespace mydb::contract
