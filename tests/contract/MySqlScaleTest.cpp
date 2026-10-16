// Critério de pronto da Fase 4 (CLAUDE.md §2.1): a árvore com 10 mil tabelas carrega em ≤ 500 ms. Aqui fica a
// parte do servidor (a consulta de objetos de um banco, com o parse das linhas); a parte da UI está em
// bench/ui/ObjectTreeBench.cpp e leva poucos milissegundos. Criar 10 mil tabelas demora, então o teste só roda
// com MYDB_TEST_SCALE=1 e reaproveita o banco mydb_scale entre execuções.
//
// No MariaDB o tempo é do servidor: o information_schema abre cada tabela para dar tamanho e linhas e, com o
// table_definition_cache padrão (400), leva ~3 s para 10 mil tabelas (~0,5 s com o cache maior que o número de
// tabelas). Por decisão de 2026-10-16, ali o teste só publica a medição; o orçamento vale para o MySQL.

#include "core/interfaces/IConnection.hpp"
#include "drivers/mysql/MySqlDriver.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"
#include "support/TestServers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::contract {
namespace {

constexpr int kTables = 10'000;
constexpr auto kServerBudget = std::chrono::milliseconds{400}; // sobra ≥ 100 ms para a UI

void run(core::IConnection& connection, const std::string& sql) {
    const auto execution = connection.execute(sql);
    while (execution->nextResult()) {
        if (core::IResultSet* rows = execution->resultSet()) {
            while (rows->next()) {
            }
        }
    }
}

int countTables(core::IConnection& connection) {
    const auto execution = connection.execute(
        "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'mydb_scale' AND TABLE_NAME LIKE 't\\_%'");
    if (!execution->nextResult() || execution->resultSet() == nullptr || !execution->resultSet()->next()) {
        return 0;
    }
    return std::stoi(std::string(execution->resultSet()->text(0)));
}

class MySqlScaleTest : public ::testing::TestWithParam<const char*> {};

TEST_P(MySqlScaleTest, TenThousandTablesLoadWithinBudget) {
    const char* enabled = std::getenv("MYDB_TEST_SCALE"); // NOLINT(concurrency-mt-unsafe)
    if (enabled == nullptr || std::string_view(enabled) != "1") {
        GTEST_SKIP() << "MYDB_TEST_SCALE=1 liga este teste (cria 10 mil tabelas na primeira vez)";
    }
    const auto server = tests::serverFromEnvironment(GetParam());
    if (!server) {
        GTEST_SKIP() << GetParam() << " não definida; servidor ausente";
    }
    infra::credentials::MemoryCredentialStore credentials;
    credentials.write("scale/password", tests::serverValue(*server, "password", ""));
    core::ConnectionParams params;
    params.sessionId = "scale";
    params.driverId = "mysql";
    params.host = tests::serverValue(*server, "host", "127.0.0.1");
    params.port = static_cast<std::uint16_t>(std::stoi(tests::serverValue(*server, "port", "3306")));
    params.user = tests::serverValue(*server, "user", "root");
    const auto connection = drivers::mysql::MySqlDriver(MYDB_MARIADB_PLUGIN_DIR).createConnection(params, credentials);
    connection->open();

    run(*connection, "CREATE DATABASE IF NOT EXISTS mydb_scale");
    if (countTables(*connection) < kTables) {
        // MEMORY: o custo de criar a tabela é só o do dicionário de dados (InnoDB cria um arquivo por tabela).
        run(*connection, "DROP PROCEDURE IF EXISTS mydb_scale.make_tables");
        run(*connection, "CREATE PROCEDURE mydb_scale.make_tables(n INT) BEGIN "
                         "DECLARE i INT DEFAULT 0; "
                         "WHILE i < n DO "
                         "SET @s = CONCAT('CREATE TABLE IF NOT EXISTS mydb_scale.t_', LPAD(i, 5, '0'), "
                         "' (id INT PRIMARY KEY, nome VARCHAR(40), criado DATETIME) ENGINE=MEMORY'); "
                         "PREPARE st FROM @s; EXECUTE st; DEALLOCATE PREPARE st; SET i = i + 1; "
                         "END WHILE; END");
        run(*connection, "CALL mydb_scale.make_tables(" + std::to_string(kTables) + ")");
    }
    ASSERT_GE(countTables(*connection), kTables);

    std::vector<std::chrono::milliseconds> times;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto started = std::chrono::steady_clock::now();
        const auto objects = connection->metadata().objects("mydb_scale");
        times.push_back(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started));
        ASSERT_GE(objects.size(), static_cast<std::size_t>(kTables));
    }
    std::ranges::sort(times);
    const auto median = times[1];
    RecordProperty("objects_ms", static_cast<int>(median.count()));
    std::printf("[ medição ] %s: objetos de 10 mil tabelas em %lld ms (mediana de 3)\n", GetParam(),
                static_cast<long long>(median.count()));
    if (connection->serverInfo().productName == "MariaDB") {
        return; // só informativo (veja o comentário no topo)
    }
    EXPECT_LE(median, kServerBudget);
}

INSTANTIATE_TEST_SUITE_P(Servers, MySqlScaleTest, ::testing::Values("MYDB_TEST_MYSQL", "MYDB_TEST_MARIADB"),
                         [](const auto& testParam) { return std::string(testParam.param).substr(10); });

} // namespace
} // namespace mydb::contract
