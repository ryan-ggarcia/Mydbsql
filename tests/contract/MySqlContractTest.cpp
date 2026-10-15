// Contrato de IConnection/IExecution/IMetadataProvider contra servidores reais (CLAUDE.md §4.3, L).
// Cada servidor vem de uma variável de ambiente, ex.:
//   MYDB_TEST_MYSQL="host=127.0.0.1;port=3307;user=root;password="
//   MYDB_TEST_MARIADB="host=127.0.0.1;port=3308;user=root;password="
// Sem a variável, os testes daquele servidor são pulados.

#include "core/errors/DbError.hpp"
#include "core/interfaces/IConnection.hpp"
#include "drivers/mysql/MySqlDriver.hpp"
#include "infra/credentials/MemoryCredentialStore.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mydb::contract {
namespace {

using core::IExecution;
using core::IResultSet;
using core::model::ObjectRef;
using core::model::ObjectType;

constexpr std::string_view kDatabase = "mydb_contract";

std::optional<std::map<std::string, std::string, std::less<>>> serverFromEnvironment(const char* variable) {
    const char* raw = std::getenv(variable); // NOLINT(concurrency-mt-unsafe): lido antes de qualquer thread
    if (raw == nullptr || *raw == '\0') {
        return std::nullopt;
    }
    std::map<std::string, std::string, std::less<>> values;
    std::string_view text(raw);
    while (!text.empty()) {
        const std::size_t end = text.find(';');
        const std::string_view item = text.substr(0, end);
        if (const std::size_t equals = item.find('='); equals != std::string_view::npos) {
            values[std::string(item.substr(0, equals))] = std::string(item.substr(equals + 1));
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return values;
}

/// Executa e descarta todos os resultados.
void run(core::IConnection& connection, std::string_view sql) {
    const auto execution = connection.execute(sql);
    while (execution->nextResult()) {
        if (IResultSet* rows = execution->resultSet()) {
            while (rows->next()) {
            }
        }
    }
}

std::vector<std::vector<std::string>> rowsOf(IResultSet& result) {
    std::vector<std::vector<std::string>> rows;
    while (result.next()) {
        std::vector<std::string> row;
        for (std::size_t i = 0; i < result.columns().size(); ++i) {
            row.emplace_back(result.isNull(i) ? "<NULL>" : std::string(result.text(i)));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string scalar(core::IConnection& connection, std::string_view sql) {
    const auto execution = connection.execute(sql);
    if (!execution->nextResult() || execution->resultSet() == nullptr || !execution->resultSet()->next()) {
        throw std::runtime_error("sem resultado: " + std::string(sql));
    }
    IResultSet& rows = *execution->resultSet();
    return rows.isNull(0) ? "<NULL>" : std::string(rows.text(0));
}

class MySqlContractTest : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override {
        const auto server = serverFromEnvironment(GetParam());
        if (!server) {
            GTEST_SKIP() << GetParam() << " não definida; servidor ausente";
        }
        const auto value = [&](const char* key, const char* fallback) {
            const auto found = server->find(key);
            return found == server->end() ? std::string(fallback) : found->second;
        };
        m_params.sessionId = "contract";
        m_params.driverId = "mysql";
        m_params.host = value("host", "127.0.0.1");
        m_params.port = static_cast<std::uint16_t>(std::stoi(value("port", "3306")));
        m_params.user = value("user", "root");
        m_credentials.write("contract/password", value("password", ""));

        m_connection = connect(m_params);
        run(*m_connection, "DROP DATABASE IF EXISTS mydb_contract; CREATE DATABASE mydb_contract");
        m_connection->useDatabase(kDatabase);
        // CTEs recursivas geram as linhas dos testes de volume; os limites padrão param em ~1000.
        // O MariaDB corta em silêncio (max_recursive_iterations), o MySQL dá erro.
        run(*m_connection, mariaDb() ? "SET SESSION max_recursive_iterations = 1000000"
                                     : "SET SESSION cte_max_recursion_depth = 1000000");
    }

    void TearDown() override {
        if (m_connection) {
            run(*m_connection, "DROP DATABASE IF EXISTS mydb_contract");
        }
    }

    [[nodiscard]] std::unique_ptr<core::IConnection> connect(const core::ConnectionParams& params) {
        auto connection = drivers::mysql::MySqlDriver(MYDB_MARIADB_PLUGIN_DIR).createConnection(params, m_credentials);
        connection->open();
        return connection;
    }

    [[nodiscard]] bool mariaDb() const { return m_connection->serverInfo().productName == "MariaDB"; }

    core::ConnectionParams m_params;
    infra::credentials::MemoryCredentialStore m_credentials;
    std::unique_ptr<core::IConnection> m_connection;
};

TEST_P(MySqlContractTest, OpensAndReportsTheServer) {
    const core::ServerInfo info = m_connection->serverInfo();
    EXPECT_GT(info.version, 50000U);
    EXPECT_GT(info.connectionId, 0U);
    EXPECT_EQ(info.productName, std::string(GetParam()) == "MYDB_TEST_MARIADB" ? "MariaDB" : "MySQL");
    EXPECT_TRUE(m_connection->ping(false));
    EXPECT_EQ(scalar(*m_connection, "SELECT DATABASE()"), kDatabase);
}

TEST_P(MySqlContractTest, SelectReturnsTypedColumnsAndValues) {
    const auto execution = m_connection->execute(
        "SELECT 1 AS n, 'ação' AS t, NULL AS z, CAST(X'00FF' AS BINARY) AS b, 2.5 AS d, DATE '2026-10-15' AS dt");
    ASSERT_TRUE(execution->nextResult());
    IResultSet* rows = execution->resultSet();
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(execution->rowsAffected(), -1);

    const auto columns = rows->columns();
    ASSERT_EQ(columns.size(), 6U);
    EXPECT_EQ(columns[0].name, "n");
    EXPECT_EQ(columns[0].category, core::model::DataTypeCategory::Integer);
    EXPECT_EQ(columns[1].category, core::model::DataTypeCategory::Text);
    EXPECT_EQ(columns[3].category, core::model::DataTypeCategory::Binary);
    EXPECT_EQ(columns[4].category, core::model::DataTypeCategory::Real);
    EXPECT_EQ(columns[5].category, core::model::DataTypeCategory::Temporal);

    ASSERT_TRUE(rows->next());
    EXPECT_EQ(rows->text(0), "1");
    EXPECT_EQ(rows->text(1), "ação");
    EXPECT_TRUE(rows->isNull(2));
    const auto bytes = rows->bytes(3);
    ASSERT_EQ(bytes.size(), 2U);
    EXPECT_EQ(bytes[0], std::byte{0x00});
    EXPECT_EQ(bytes[1], std::byte{0xFF});
    EXPECT_EQ(rows->text(5), "2026-10-15");
    EXPECT_FALSE(rows->next());
    EXPECT_FALSE(execution->nextResult());
}

TEST_P(MySqlContractTest, BatchYieldsOneResultPerStatement) {
    const auto execution = m_connection->execute("CREATE TABLE t (id INT PRIMARY KEY, v VARCHAR(10));"
                                                 "INSERT INTO t VALUES (1, 'a'), (2, 'b');"
                                                 "SELECT id, v FROM t ORDER BY id;"
                                                 "UPDATE t SET v = 'c' WHERE id = 1");
    ASSERT_TRUE(execution->nextResult());
    EXPECT_EQ(execution->resultSet(), nullptr);
    EXPECT_EQ(execution->rowsAffected(), 0);
    ASSERT_TRUE(execution->nextResult());
    EXPECT_EQ(execution->rowsAffected(), 2);
    ASSERT_TRUE(execution->nextResult());
    ASSERT_NE(execution->resultSet(), nullptr);
    EXPECT_EQ(rowsOf(*execution->resultSet()), (std::vector<std::vector<std::string>>{{"1", "a"}, {"2", "b"}}));
    ASSERT_TRUE(execution->nextResult());
    EXPECT_EQ(execution->rowsAffected(), 1);
    EXPECT_FALSE(execution->nextResult());
    EXPECT_GE(execution->timing().server.count(), 0);
}

TEST_P(MySqlContractTest, ErrorsBecomeQueryErrorsAndTheConnectionSurvives) {
    try {
        (void)m_connection->execute("SELECT * FROM nao_existe");
        FAIL() << "deveria lançar QueryError";
    } catch (const core::QueryError& error) {
        EXPECT_EQ(error.code(), 1146);
        EXPECT_EQ(error.sqlState(), "42S02");
    }

    const auto execution = m_connection->execute("SELECT 1; SELECT * FROM nao_existe; SELECT 3");
    ASSERT_TRUE(execution->nextResult());
    EXPECT_THROW((void)execution->nextResult(), core::QueryError);
    EXPECT_FALSE(execution->nextResult());

    EXPECT_EQ(scalar(*m_connection, "SELECT 2"), "2");
}

TEST_P(MySqlContractTest, StreamsLargeResultsRowByRow) {
    const auto execution =
        m_connection->execute("WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM s WHERE n < 200000) "
                              "SELECT n FROM s");
    ASSERT_TRUE(execution->nextResult());
    IResultSet& rows = *execution->resultSet();
    std::size_t count = 0;
    std::string last;
    while (rows.next()) {
        ++count;
        last = std::string(rows.text(0));
    }
    EXPECT_EQ(count, 200000U);
    EXPECT_EQ(last, "200000");
}

TEST_P(MySqlContractTest, AbandonedExecutionsLeaveTheConnectionReady) {
    {
        const auto execution = m_connection->execute(
            "WITH RECURSIVE s(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM s WHERE n < 50000) SELECT n FROM s;"
            "SELECT 'segundo'");
        ASSERT_TRUE(execution->nextResult());
        ASSERT_TRUE(execution->resultSet()->next()); // lê uma linha e abandona o resto
    }
    EXPECT_EQ(scalar(*m_connection, "SELECT 3"), "3");

    // Uma execução nova descarta a anterior, que passa a não ter mais resultados.
    const auto first = m_connection->execute("SELECT 1; SELECT 2");
    const auto second = m_connection->execute("SELECT 4");
    EXPECT_FALSE(first->nextResult());
    ASSERT_TRUE(second->nextResult());
    ASSERT_TRUE(second->resultSet()->next());
    EXPECT_EQ(second->resultSet()->text(0), "4");
}

TEST_P(MySqlContractTest, CancelInterruptsALongQuery) {
    std::thread canceller([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        m_connection->cancelCurrent();
    });
    const auto started = std::chrono::steady_clock::now();
    bool cancelled = false;
    try {
        // Interrompido por KILL QUERY, SLEEP devolve 1 em vez de esperar; outras consultas lançam o erro 1317.
        const auto execution = m_connection->execute("SELECT SLEEP(10)");
        while (execution->nextResult()) {
            if (IResultSet* rows = execution->resultSet()) {
                while (rows->next()) {
                }
            }
        }
    } catch (const core::CancelledError&) {
        cancelled = true;
    }
    canceller.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_LT(elapsed, std::chrono::seconds(5)) << (cancelled ? "cancelada" : "terminou sozinha");
    EXPECT_EQ(scalar(*m_connection, "SELECT 5"), "5");
}

TEST_P(MySqlContractTest, StartupScriptRunsAfterConnecting) {
    core::ConnectionParams params = m_params;
    params.defaultDatabase = std::string(kDatabase);
    params.startupScript = "SET @x = 42; SET @y = 'a;b'";
    const auto connection = connect(params);
    EXPECT_EQ(scalar(*connection, "SELECT CONCAT(@x, '|', @y)"), "42|a;b");
    EXPECT_EQ(scalar(*connection, "SELECT DATABASE()"), kDatabase);
}

TEST_P(MySqlContractTest, WrongPasswordIsAConnectionError) {
    core::ConnectionParams params = m_params;
    params.sessionId = "senha-errada";
    m_credentials.write("senha-errada/password", "nao-e-esta");
    try {
        (void)connect(params);
        FAIL() << "deveria lançar ConnectionError";
    } catch (const core::ConnectionError& error) {
        EXPECT_EQ(error.code(), 1045);
        EXPECT_FALSE(error.hint().empty());
    }
}

TEST_P(MySqlContractTest, MetadataDescribesTheSchema) {
    run(*m_connection, "CREATE TABLE pai (id INT PRIMARY KEY, nome VARCHAR(20) NOT NULL DEFAULT 'x', "
                       "UNIQUE KEY uk_nome (nome)) COMMENT = 'tabela pai'");
    std::string filho = "CREATE TABLE filho (id INT AUTO_INCREMENT PRIMARY KEY, pai_id INT, valor DECIMAL(10,2), "
                        "dobro DECIMAL(11,2) AS (valor * 2) STORED, "
                        "CONSTRAINT fk_pai FOREIGN KEY (pai_id) REFERENCES pai (id) ON DELETE CASCADE";
    const bool checks = m_connection->dialect().supports(core::Feature::CheckConstraints);
    if (checks) {
        filho += ", CONSTRAINT ck_valor CHECK (valor >= 0)";
    }
    run(*m_connection, filho + ")");
    run(*m_connection, "CREATE VIEW v AS SELECT id FROM pai");
    run(*m_connection, "CREATE PROCEDURE proc() BEGIN SELECT 1; END");
    run(*m_connection, "CREATE FUNCTION fn() RETURNS INT DETERMINISTIC RETURN 1");
    run(*m_connection, "CREATE TRIGGER trg BEFORE INSERT ON pai FOR EACH ROW SET NEW.nome = UPPER(NEW.nome)");
    run(*m_connection, "CREATE EVENT ev ON SCHEDULE EVERY 1 DAY DO DELETE FROM filho WHERE 1 = 0");

    core::IMetadataProvider& metadata = m_connection->metadata();
    const auto databases = metadata.databases();
    EXPECT_NE(std::ranges::find(databases, kDatabase), databases.end());

    const auto objects = metadata.objects(kDatabase);
    const auto typeOf = [&](std::string_view name) -> std::optional<ObjectType> {
        const auto found = std::ranges::find_if(objects, [name](const auto& o) { return o.ref.name == name; });
        return found == objects.end() ? std::nullopt : std::optional(found->ref.type);
    };
    EXPECT_EQ(typeOf("pai"), ObjectType::Table);
    EXPECT_EQ(typeOf("filho"), ObjectType::Table);
    EXPECT_EQ(typeOf("v"), ObjectType::View);
    EXPECT_EQ(typeOf("proc"), ObjectType::Procedure);
    EXPECT_EQ(typeOf("fn"), ObjectType::Function);
    EXPECT_EQ(typeOf("trg"), ObjectType::Trigger);
    EXPECT_EQ(typeOf("ev"), ObjectType::Event);
    const auto pai = std::ranges::find_if(objects, [](const auto& o) { return o.ref.name == "pai"; });
    EXPECT_EQ(pai->comment, "tabela pai");
    EXPECT_EQ(pai->engine, "InnoDB");
    EXPECT_TRUE(pai->created.has_value());

    const auto table = metadata.table(
        ObjectRef{.database = std::string(kDatabase), .schema = {}, .name = "filho", .type = ObjectType::Table});
    ASSERT_EQ(table.columns.size(), 4U);
    EXPECT_EQ(table.columns[0].defaultValue.kind, core::model::ColumnDefault::Kind::AutoIncrement);
    EXPECT_EQ(table.columns[2].typeName, "DECIMAL");
    EXPECT_EQ(table.columns[2].lengthSet, "10,2");
    ASSERT_TRUE(table.columns[3].generated.has_value());
    EXPECT_TRUE(table.columns[3].generated->stored);
    EXPECT_EQ(table.rowIdentityColumns(), (std::vector<std::string>{"id"}));
    ASSERT_EQ(table.foreignKeys.size(), 1U);
    EXPECT_EQ(table.foreignKeys[0].name, "fk_pai");
    EXPECT_EQ(table.foreignKeys[0].referencedTable.name, "pai");
    EXPECT_EQ(table.foreignKeys[0].referencedColumns, (std::vector<std::string>{"id"}));
    EXPECT_EQ(table.foreignKeys[0].onDelete, core::model::ReferentialAction::Cascade);
    EXPECT_EQ(table.options.engine, "InnoDB");
    if (checks) {
        ASSERT_EQ(table.checks.size(), 1U);
        EXPECT_EQ(table.checks[0].name, "ck_valor");
    }

    const auto parent = metadata.table(
        ObjectRef{.database = std::string(kDatabase), .schema = {}, .name = "pai", .type = ObjectType::Table});
    EXPECT_EQ(parent.columns[1].defaultValue,
              (core::model::ColumnDefault{.kind = core::model::ColumnDefault::Kind::Literal, .text = "x"}));
    EXPECT_EQ(parent.options.comment, "tabela pai");
    const auto unique = std::ranges::find_if(parent.indexes, [](const auto& i) { return i.name == "uk_nome"; });
    ASSERT_NE(unique, parent.indexes.end());
    EXPECT_EQ(unique->kind, core::model::Index::Kind::Unique);

    for (const auto& [name, type] : std::vector<std::pair<std::string, ObjectType>>{{"pai", ObjectType::Table},
                                                                                    {"v", ObjectType::View},
                                                                                    {"proc", ObjectType::Procedure},
                                                                                    {"fn", ObjectType::Function},
                                                                                    {"trg", ObjectType::Trigger},
                                                                                    {"ev", ObjectType::Event}}) {
        const std::string create = metadata.createStatement(
            ObjectRef{.database = std::string(kDatabase), .schema = {}, .name = name, .type = type});
        EXPECT_NE(create.find("CREATE"), std::string::npos) << name << ": " << create;
    }

    EXPECT_THROW(
        (void)metadata.table(ObjectRef{
            .database = std::string(kDatabase), .schema = {}, .name = "nao_existe", .type = ObjectType::Table}),
        core::QueryError);
}

TEST_P(MySqlContractTest, IgnoredDatabasesAreHidden) {
    core::ConnectionParams params = m_params;
    params.ignoreDatabasePattern = "^mydb_contract$";
    const auto connection = connect(params);
    const auto databases = connection->metadata().databases();
    EXPECT_EQ(std::ranges::find(databases, kDatabase), databases.end());
    EXPECT_FALSE(databases.empty());
}

INSTANTIATE_TEST_SUITE_P(Servers, MySqlContractTest, ::testing::Values("MYDB_TEST_MYSQL", "MYDB_TEST_MARIADB"),
                         [](const auto& testParam) { return std::string(testParam.param).substr(10); });

} // namespace
} // namespace mydb::contract
