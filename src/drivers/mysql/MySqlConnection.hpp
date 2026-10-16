#pragma once

#include "core/interfaces/ConnectionParams.hpp"
#include "core/interfaces/IConnection.hpp"
#include "core/interfaces/ICredentialStore.hpp"
#include "drivers/mysql/MySqlDialect.hpp"
#include "drivers/mysql/MySqlHandles.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace mydb::drivers::mysql {

class MySqlExecution;
class MySqlMetadataProvider;
class MySqlServerAdmin;

/// Conexão MySQL/MariaDB sobre a libmariadb. Usada por um único ConnectionWorker; só cancelCurrent()
/// é chamado de outra thread (abre uma conexão auxiliar e envia KILL QUERY, CLAUDE.md §4.5).
class MySqlConnection final : public core::IConnection {
public:
    /// `pluginDirectory`: onde estão os plugins de autenticação da libmariadb (ex.: caching_sha2_password);
    /// vazio usa o padrão da biblioteca.
    MySqlConnection(core::ConnectionParams params, core::ICredentialStore& credentials,
                    std::filesystem::path pluginDirectory);
    MySqlConnection(const MySqlConnection&) = delete;
    MySqlConnection& operator=(const MySqlConnection&) = delete;
    MySqlConnection(MySqlConnection&&) = delete;
    MySqlConnection& operator=(MySqlConnection&&) = delete;
    ~MySqlConnection() override;

    void open() override;
    void close() noexcept override;
    bool ping(bool reconnect) override;
    [[nodiscard]] std::unique_ptr<core::IExecution> execute(std::string_view sql) override;
    void cancelCurrent() override;
    void useDatabase(std::string_view database) override;
    [[nodiscard]] core::ServerInfo serverInfo() const override { return m_serverInfo; }
    [[nodiscard]] const core::ISqlDialect& dialect() const override { return *m_dialect; }
    [[nodiscard]] core::IMetadataProvider& metadata() override;
    [[nodiscard]] core::Capabilities capabilities() override;
    void setQueryLogger(core::IQueryLogger* logger) noexcept override { m_logger = logger; }

    [[nodiscard]] const MySqlDialect& mysqlDialect() const noexcept { return *m_dialect; }
    [[nodiscard]] const core::ConnectionParams& params() const noexcept { return m_params; }

    // Usados por MySqlExecution e MySqlResultSet.
    [[nodiscard]] MYSQL* handle() const noexcept { return m_handle.get(); }
    /// Converte o último erro de `handle` na exceção certa (QueryError, ConnectionError ou CancelledError).
    [[noreturn]] void raise(MYSQL* handle) const;
    void executionFinished(const MySqlExecution* execution) noexcept;

private:
    [[nodiscard]] MysqlHandle connect(const std::string& password) const;
    [[nodiscard]] std::string queryScalar(std::string_view sql);
    void logSql(std::string_view sql) const noexcept;
    void runStartupStatements();
    void requireOpen() const;

    core::ConnectionParams m_params;
    core::ICredentialStore& m_credentials;
    std::filesystem::path m_pluginDirectory;

    MysqlHandle m_handle;
    core::ServerInfo m_serverInfo;
    std::unique_ptr<MySqlDialect> m_dialect;
    std::unique_ptr<MySqlMetadataProvider> m_metadata;
    std::unique_ptr<MySqlServerAdmin> m_serverAdmin;
    std::atomic<core::IQueryLogger*> m_logger{nullptr};
    MySqlExecution* m_activeExecution{nullptr};

    // Lidos por cancelCurrent() em outra thread.
    mutable std::mutex m_cancelMutex;
    std::string m_password;
    std::atomic<unsigned long> m_threadId{0}; // NOLINT(google-runtime-int): tipo de mysql_thread_id
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace mydb::drivers::mysql
