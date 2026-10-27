#pragma once

// Conexão falsa com metadados configuráveis, para testar ConnectionWorker, CachedMetadataProvider e
// SessionService sem servidor. O FakeServer é o estado que o teste enxerga e controla.

#include "core/errors/DbError.hpp"
#include "core/interfaces/IConnection.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mydb::tests {

/// Um resultado roteirizado de FakeServer::script.
struct FakeResult {
    std::vector<core::ColumnInfo> columns; ///< vazio: a sentença não devolve linhas
    std::vector<std::vector<std::optional<std::string>>> rows;
    std::int64_t rowsAffected{-1};
    std::uint32_t warnings{0};
    std::string error; ///< não vazio: este resultado lança QueryError (no execute(), se for o primeiro)
};

/// Uma coluna de texto para FakeResult::columns.
[[nodiscard]] inline core::ColumnInfo fakeColumn(std::string name) {
    core::ColumnInfo column;
    column.name = std::move(name);
    column.typeName = "VARCHAR";
    return column;
}

struct FakeServer {
    std::mutex mutex;
    std::vector<std::string> databases{"app", "logs"};
    std::map<std::string, std::vector<core::model::DbObject>, std::less<>> objects;
    std::string failDatabase; ///< objects() deste banco lança QueryError

    std::atomic<int> databaseCalls{0};
    std::atomic<int> summaryCalls{0};
    std::atomic<int> objectCalls{0};
    std::atomic<int> tableCalls{0};
    std::atomic<int> pingCalls{0};
    std::atomic<int> cancelCalls{0};
    std::atomic<bool> pingResult{true};
    std::atomic<bool> closed{false};
    std::atomic<std::thread::id> lastThread;
    std::string usedDatabase;
    core::IQueryLogger* logger{nullptr};

    /// Resultados de cada execute(); sem roteiro, cada execute() devolve um resultado sem linhas.
    std::function<std::vector<FakeResult>(std::string_view sql)> script;
    std::vector<std::string> executed; ///< o SQL de cada execute(), protegido por `mutex`

    // Trava o próximo execute() até release(), para testar cancelamento e fila.
    std::condition_variable gate;
    bool blockNext{false};
    bool blocked{false};
    bool released{false};

    void release() {
        {
            const std::scoped_lock lock(mutex);
            released = true;
        }
        gate.notify_all();
    }

    bool waitBlocked(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return gate.wait_for(lock, timeout, [this] { return blocked; });
    }
};

class FakeMetadata final : public core::IMetadataProvider {
public:
    explicit FakeMetadata(FakeServer& server) : m_server(&server) {}

    std::vector<std::string> databases() override {
        ++m_server->databaseCalls;
        log("SHOW DATABASES");
        const std::scoped_lock lock(m_server->mutex);
        return m_server->databases;
    }
    std::vector<core::model::DatabaseSummary> databaseSummaries() override {
        ++m_server->summaryCalls;
        std::vector<core::model::DatabaseSummary> list;
        for (const std::string& name : databases()) {
            core::model::DatabaseSummary summary;
            summary.name = name;
            list.push_back(std::move(summary));
        }
        return list;
    }
    std::vector<core::model::DbObject> objects(std::string_view database) override {
        ++m_server->objectCalls;
        const std::scoped_lock lock(m_server->mutex);
        if (database == m_server->failDatabase) {
            throw core::QueryError(core::DbErrorInfo{
                .message = "Access denied", .code = 1044, .sqlState = "42000", .hint = "Confira os privilégios."});
        }
        const auto found = m_server->objects.find(database);
        return found == m_server->objects.end() ? std::vector<core::model::DbObject>{} : found->second;
    }
    core::model::TableDefinition table(const core::model::ObjectRef& ref) override {
        ++m_server->tableCalls;
        core::model::TableDefinition definition;
        definition.ref = ref;
        return definition;
    }
    std::string createStatement(const core::model::ObjectRef& ref) override { return "CREATE TABLE " + ref.name; }

private:
    void log(std::string_view sql) {
        if (m_server->logger != nullptr) {
            m_server->logger->log(core::LogCategory::Sql, sql);
        }
    }

    FakeServer* m_server;
};

class FakeResultSet final : public core::IResultSet {
public:
    explicit FakeResultSet(const FakeResult& result) : m_result(&result) {}

    std::span<const core::ColumnInfo> columns() const override { return m_result->columns; }
    bool next() override { return ++m_row < m_result->rows.size(); }
    bool isNull(std::size_t column) const override { return !m_result->rows[m_row][column].has_value(); }
    std::string_view text(std::size_t column) const override {
        const auto& value = m_result->rows[m_row][column];
        return value ? std::string_view(*value) : std::string_view();
    }
    std::span<const std::byte> bytes(std::size_t column) const override {
        const std::string_view value = text(column);
        return std::as_bytes(std::span(value.data(), value.size()));
    }

private:
    const FakeResult* m_result;
    std::size_t m_row{static_cast<std::size_t>(-1)};
};

class FakeExecution final : public core::IExecution {
public:
    explicit FakeExecution(std::vector<FakeResult> results) : m_results(std::move(results)) {}

    bool nextResult() override {
        m_rows.reset();
        if (++m_position >= m_results.size()) {
            return false;
        }
        const FakeResult& current = m_results[m_position];
        if (!current.error.empty()) {
            m_position = m_results.size();
            throw core::QueryError(current.error);
        }
        if (!current.columns.empty()) {
            m_rows = std::make_unique<FakeResultSet>(current);
        }
        return true;
    }
    core::IResultSet* resultSet() override { return m_rows.get(); }
    std::int64_t rowsAffected() const override { return m_results[m_position].rowsAffected; }
    std::uint32_t warnings() const override { return m_results[m_position].warnings; }
    Timing timing() const override {
        return Timing{.server = std::chrono::microseconds(static_cast<std::int64_t>(m_position) + 1),
                      .network = std::chrono::microseconds(0)};
    }

private:
    std::vector<FakeResult> m_results;
    std::size_t m_position{static_cast<std::size_t>(-1)};
    std::unique_ptr<FakeResultSet> m_rows;
};

class FakeDialect final : public core::ISqlDialect {
public:
    std::string quoteIdentifier(std::string_view identifier) const override {
        return "`" + std::string(identifier) + "`";
    }
    std::string quoteString(std::string_view value) const override { return "'" + std::string(value) + "'"; }
    std::string quoteBinary(std::span<const std::byte> /*value*/) const override { return "X''"; }
    std::string applyLimit(std::string_view select, std::int64_t /*limit*/, std::int64_t /*offset*/) const override {
        return std::string(select);
    }
    std::span<const core::model::DataType> dataTypes() const override { return {}; }
    bool supports(core::Feature /*feature*/) const override { return false; }
    core::sql::SqlSyntax syntax() const override { return core::sql::SqlSyntax::mysql(); }
};

class FakeServerConnection final : public core::IConnection {
public:
    explicit FakeServerConnection(FakeServer& server) : m_server(&server), m_metadata(server) {}

    void open() override {}
    void close() noexcept override { m_server->closed = true; }
    bool ping(bool /*reconnect*/) override {
        ++m_server->pingCalls;
        return m_server->pingResult;
    }
    std::unique_ptr<core::IExecution> execute(std::string_view sql) override {
        m_server->lastThread = std::this_thread::get_id();
        if (m_server->logger != nullptr) {
            m_server->logger->log(core::LogCategory::Sql, sql);
        }
        std::unique_lock lock(m_server->mutex);
        m_server->executed.emplace_back(sql);
        if (m_server->blockNext) {
            m_server->blockNext = false;
            m_server->blocked = true;
            m_server->gate.notify_all();
            m_server->gate.wait(lock, [this] { return m_server->released; });
            m_server->blocked = false;
            if (m_cancelled.exchange(false)) {
                throw core::CancelledError();
            }
        }
        std::vector<FakeResult> results(1);
        results[0].rowsAffected = 0;
        if (m_server->script) {
            lock.unlock();
            results = m_server->script(sql);
        }
        if (!results.empty() && !results.front().error.empty()) {
            throw core::QueryError(results.front().error);
        }
        return std::make_unique<FakeExecution>(std::move(results));
    }
    void cancelCurrent() override {
        ++m_server->cancelCalls;
        m_cancelled = true;
        m_server->release();
    }
    void useDatabase(std::string_view database) override {
        m_server->lastThread = std::this_thread::get_id();
        const std::scoped_lock lock(m_server->mutex);
        if (database == m_server->failDatabase) {
            throw core::QueryError("Unknown database");
        }
        m_server->usedDatabase = std::string(database);
    }
    core::ServerInfo serverInfo() const override {
        return core::ServerInfo{.productName = "Fake", .versionString = "1.0", .version = 10000, .connectionId = 7};
    }
    const core::ISqlDialect& dialect() const override { return m_dialect; }
    core::IMetadataProvider& metadata() override { return m_metadata; }
    core::Capabilities capabilities() override { return {}; }
    void setQueryLogger(core::IQueryLogger* logger) noexcept override { m_server->logger = logger; }

private:
    FakeServer* m_server;
    FakeMetadata m_metadata;
    FakeDialect m_dialect;
    std::atomic<bool> m_cancelled{false};
};

} // namespace mydb::tests
