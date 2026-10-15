#include "drivers/mysql/MySqlConnection.hpp"

#include "core/errors/DbError.hpp"
#include "core/sql/SqlSplitter.hpp"
#include "drivers/mysql/MySqlExecution.hpp"
#include "drivers/mysql/MySqlMetadataProvider.hpp"

#include <chrono>
#include <limits>
#include <system_error>
#include <utility>

namespace mydb::drivers::mysql {

namespace {

// Códigos de erro do servidor (mysqld_error.h) e do cliente (errmsg.h) que mudam a reação do app.
constexpr unsigned kQueryInterrupted = 1317;
constexpr unsigned kAccessDenied = 1045;
constexpr unsigned kDatabaseAccessDenied = 1044;
constexpr unsigned kUnknownDatabase = 1049;
constexpr unsigned kClientErrorFirst = 2000;
constexpr unsigned kClientErrorLast = 2999;
constexpr unsigned kCantConnectLocal = 2002;
constexpr unsigned kCantConnectHost = 2003;
constexpr unsigned kUnknownHost = 2005;
constexpr unsigned kServerGone = 2006;
constexpr unsigned kServerLost = 2013;

constexpr unsigned kConnectTimeoutSeconds = 10;
constexpr std::uint16_t kDefaultPort = 3306;

std::string hintFor(unsigned code) {
    switch (code) {
    case kAccessDenied:
    case kDatabaseAccessDenied:
        return "Confira o usuário, a senha e os privilégios da conta.";
    case kUnknownDatabase:
        return "Confira o nome do banco padrão da sessão.";
    case kCantConnectLocal:
    case kCantConnectHost:
    case kUnknownHost:
        return "Confira o host e a porta e se o servidor está no ar e aceita conexões remotas.";
    case kServerGone:
    case kServerLost:
        return "A conexão caiu. Reconecte; se acontecer em consultas grandes, aumente max_allowed_packet.";
    default:
        return {};
    }
}

core::DbErrorInfo errorInfo(MYSQL* handle) {
    const unsigned code = mysql_errno(handle);
    return core::DbErrorInfo{.message = mysql_error(handle),
                             .code = static_cast<int>(code),
                             .sqlState = mysql_sqlstate(handle),
                             .hint = hintFor(code)};
}

bool isConnectionError(unsigned code) {
    return (code >= kClientErrorFirst && code <= kClientErrorLast) || code == kAccessDenied ||
           code == kDatabaseAccessDenied;
}

std::chrono::microseconds since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
}

/// mysql_real_query recebe o tamanho como unsigned long (32 bits no Windows).
unsigned long queryLength(std::string_view sql) {                 // NOLINT(google-runtime-int): tipo da API C
    if (sql.size() > std::numeric_limits<unsigned long>::max()) { // NOLINT(google-runtime-int)
        throw core::QueryError("Sentença grande demais para enviar de uma vez (mais de 4 GB)");
    }
    return static_cast<unsigned long>(sql.size()); // NOLINT(google-runtime-int)
}

} // namespace

MySqlConnection::MySqlConnection(core::ConnectionParams params, core::ICredentialStore& credentials,
                                 std::filesystem::path pluginDirectory)
    : m_params(std::move(params)),
      m_credentials(credentials),
      m_pluginDirectory(std::move(pluginDirectory)),
      m_dialect(std::make_unique<MySqlDialect>(MySqlServerVersion{})) {}

MySqlConnection::~MySqlConnection() {
    close();
}

MysqlHandle MySqlConnection::connect(const std::string& password) const {
    static std::once_flag libraryInit;
    std::call_once(libraryInit, [] { mysql_library_init(0, nullptr, nullptr); });

    MysqlHandle handle(mysql_init(nullptr));
    if (!handle) {
        throw core::ConnectionError("Sem memória para criar a conexão");
    }
    MYSQL* h = handle.get();
    unsigned timeout = kConnectTimeoutSeconds;
    mysql_optionsv(h, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_optionsv(h, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned localInfile = 0; // LOAD DATA LOCAL só quando a importação pedir (Fase 9)
    mysql_optionsv(h, MYSQL_OPT_LOCAL_INFILE, &localInfile);
    if (m_params.compressed) {
        mysql_optionsv(h, MYSQL_OPT_COMPRESS, nullptr);
    }
    std::error_code ignored;
    const std::string pluginDirectory = m_pluginDirectory.string();
    if (!m_pluginDirectory.empty() && std::filesystem::is_directory(m_pluginDirectory, ignored)) {
        mysql_optionsv(h, MYSQL_PLUGIN_DIR, pluginDirectory.c_str());
    }

    // Sem configuração explícita, TLS é usado quando o servidor oferece, sem verificar o certificado.
    // A libmariadb 3.4 não separa "verificar a cadeia" de "verificar o nome": os dois modos ligam a verificação.
    using Mode = core::SslOptions::Mode;
    const core::SslOptions ssl = m_params.ssl.value_or(core::SslOptions{});
    my_bool verify = ssl.mode == Mode::VerifyCa || ssl.mode == Mode::VerifyIdentity ? 1 : 0;
    my_bool enforce = ssl.mode == Mode::Required || verify != 0 ? 1 : 0;
    mysql_optionsv(h, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
    mysql_optionsv(h, MYSQL_OPT_SSL_ENFORCE, &enforce);
    const std::string key = ssl.privateKey.string();
    const std::string certificate = ssl.certificate.string();
    const std::string authority = ssl.certificateAuthority.string();
    if (!key.empty()) {
        mysql_optionsv(h, MYSQL_OPT_SSL_KEY, key.c_str());
    }
    if (!certificate.empty()) {
        mysql_optionsv(h, MYSQL_OPT_SSL_CERT, certificate.c_str());
    }
    if (!authority.empty()) {
        mysql_optionsv(h, MYSQL_OPT_SSL_CA, authority.c_str());
    }
    if (!ssl.cipher.empty()) {
        mysql_optionsv(h, MYSQL_OPT_SSL_CIPHER, ssl.cipher.c_str());
    }

    const char* host = m_params.host.empty() ? "localhost" : m_params.host.c_str();
    const char* database = m_params.defaultDatabase.empty() ? nullptr : m_params.defaultDatabase.c_str();
    const unsigned port = m_params.port == 0 ? kDefaultPort : m_params.port;
    if (mysql_real_connect(h, host, m_params.user.c_str(), password.c_str(), database, port, nullptr,
                           CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS) == nullptr) {
        throw core::ConnectionError(errorInfo(h));
    }
    return handle;
}

void MySqlConnection::open() {
    close();
    std::string password = m_credentials.read(m_params.sessionId + "/password").value_or("");
    m_handle = connect(password);
    {
        const std::scoped_lock lock(m_cancelMutex);
        m_password = std::move(password);
    }
    MYSQL* h = m_handle.get();
    m_threadId = mysql_thread_id(h);

    MySqlServerVersion version = MySqlServerVersion::parse(mysql_get_server_info(h));
    m_serverInfo = core::ServerInfo{.productName = version.mariaDb ? "MariaDB" : "MySQL",
                                    .versionString = version.text,
                                    .version = version.number,
                                    .connectionId = m_threadId.load()};
    const MySqlDialect::SqlMode mode = MySqlDialect::parseSqlMode(queryScalar("SELECT @@SESSION.sql_mode"));
    m_dialect = std::make_unique<MySqlDialect>(std::move(version), mode);
    runStartupStatements();
}

void MySqlConnection::runStartupStatements() {
    const auto run = [this](std::string_view sql) {
        const auto execution = execute(sql);
        while (execution->nextResult()) {
        }
    };
    const auto seconds = m_params.queryTimeout.count();
    if (seconds > 0) {
        // Limite no servidor: no MySQL, max_execution_time só vale para SELECT.
        run(m_dialect->version().mariaDb ? "SET SESSION max_statement_time = " + std::to_string(seconds)
                                         : "SET SESSION max_execution_time = " + std::to_string(seconds * 1000));
    }
    if (!m_params.startupScript.empty()) {
        for (const core::sql::SqlStatement& statement :
             core::sql::SqlSplitter::split(m_params.startupScript, m_dialect->syntax())) {
            run(statement.text);
        }
    }
}

std::string MySqlConnection::queryScalar(std::string_view sql) {
    MYSQL* h = m_handle.get();
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): o tamanho vai no terceiro argumento
    if (mysql_real_query(h, sql.data(), queryLength(sql)) != 0) {
        raise(h);
    }
    const ResultHandle result(mysql_store_result(h));
    if (!result) {
        raise(h);
    }
    MYSQL_ROW row = mysql_fetch_row(result.get());
    if (row == nullptr || row[0] == nullptr) {
        return {};
    }
    return {row[0], mysql_fetch_lengths(result.get())[0]};
}

void MySqlConnection::close() noexcept {
    if (m_activeExecution != nullptr) {
        m_activeExecution->detach();
        m_activeExecution = nullptr;
    }
    m_handle.reset();
    m_threadId = 0;
}

bool MySqlConnection::ping(bool reconnect) {
    if (m_handle && mysql_ping(m_handle.get()) == 0) {
        return true;
    }
    if (!reconnect) {
        return false;
    }
    try {
        open();
        return true;
    } catch (const core::DbError&) {
        return false;
    }
}

std::unique_ptr<core::IExecution> MySqlConnection::execute(std::string_view sql) {
    requireOpen();
    if (m_activeExecution != nullptr) {
        m_activeExecution->detach();
        m_activeExecution = nullptr;
    }
    m_cancelRequested = false;
    const auto started = std::chrono::steady_clock::now();
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): o tamanho vai no terceiro argumento
    if (mysql_real_query(m_handle.get(), sql.data(), queryLength(sql)) != 0) {
        raise(m_handle.get());
    }
    auto execution = std::make_unique<MySqlExecution>(*this, since(started));
    m_activeExecution = execution.get();
    return execution;
}

void MySqlConnection::cancelCurrent() {
    const auto threadId = m_threadId.load();
    if (threadId == 0) {
        return;
    }
    std::string password;
    {
        const std::scoped_lock lock(m_cancelMutex);
        password = m_password;
    }
    m_cancelRequested = true;
    // A conexão principal está ocupada com a consulta: o KILL QUERY vai por uma conexão auxiliar.
    const MysqlHandle helper = connect(password);
    const std::string kill = "KILL QUERY " + std::to_string(threadId);
    if (mysql_real_query(helper.get(), kill.data(), queryLength(kill)) != 0) {
        throw core::QueryError(errorInfo(helper.get()));
    }
}

void MySqlConnection::useDatabase(std::string_view database) {
    requireOpen();
    if (m_activeExecution != nullptr) {
        m_activeExecution->detach();
        m_activeExecution = nullptr;
    }
    if (mysql_select_db(m_handle.get(), std::string(database).c_str()) != 0) {
        raise(m_handle.get());
    }
}

core::IMetadataProvider& MySqlConnection::metadata() {
    if (!m_metadata) {
        m_metadata = std::make_unique<MySqlMetadataProvider>(*this);
    }
    return *m_metadata;
}

void MySqlConnection::raise(MYSQL* handle) const {
    core::DbErrorInfo info = errorInfo(handle);
    const auto code = static_cast<unsigned>(info.code);
    if (code == kQueryInterrupted || (m_cancelRequested && (code == kServerLost || code == kServerGone))) {
        throw core::CancelledError();
    }
    if (isConnectionError(code)) {
        throw core::ConnectionError(std::move(info));
    }
    throw core::QueryError(std::move(info));
}

void MySqlConnection::executionFinished(const MySqlExecution* execution) noexcept {
    if (m_activeExecution == execution) {
        m_activeExecution = nullptr;
    }
}

void MySqlConnection::requireOpen() const {
    if (!m_handle) {
        throw core::ConnectionError("A conexão não está aberta");
    }
}

} // namespace mydb::drivers::mysql
