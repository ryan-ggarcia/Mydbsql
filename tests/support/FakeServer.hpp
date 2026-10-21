#pragma once

// Conexão falsa com metadados configuráveis, para testar ConnectionWorker, CachedMetadataProvider e
// SessionService sem servidor. O FakeServer é o estado que o teste enxerga e controla.

#include "core/errors/DbError.hpp"
#include "core/interfaces/IConnection.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mydb::tests {

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

class FakeServerConnection final : public core::IConnection {
public:
    explicit FakeServerConnection(FakeServer& server) : m_server(&server), m_metadata(server) {}

    void open() override {}
    void close() noexcept override { m_server->closed = true; }
    bool ping(bool /*reconnect*/) override {
        ++m_server->pingCalls;
        return m_server->pingResult;
    }
    std::unique_ptr<core::IExecution> execute(std::string_view /*sql*/) override {
        m_server->lastThread = std::this_thread::get_id();
        std::unique_lock lock(m_server->mutex);
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
        return nullptr;
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
    const core::ISqlDialect& dialect() const override { throw std::logic_error("não usado"); }
    core::IMetadataProvider& metadata() override { return m_metadata; }
    core::Capabilities capabilities() override { return {}; }
    void setQueryLogger(core::IQueryLogger* logger) noexcept override { m_server->logger = logger; }

private:
    FakeServer* m_server;
    FakeMetadata m_metadata;
    std::atomic<bool> m_cancelled{false};
};

} // namespace mydb::tests
