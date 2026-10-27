#include "services/query/QueryService.hpp"

#include "core/errors/DbError.hpp"
#include "core/sql/SqlSplitter.hpp"
#include "core/sql/SqlTokenizer.hpp"
#include "services/query/SqlText.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mydb::services::query {

namespace {

using core::sql::SqlStatement;
using core::sql::SqlToken;
using core::sql::SqlTokenKind;

/// No runFile, o SQL de cada StatementResult é cortado aqui: um script de 1 GB não volta inteiro para a UI.
constexpr std::size_t kFileSqlLimit = 1024;
constexpr std::size_t kFileChunk = std::size_t{64} * 1024;

using Deliver = std::function<void(StatementResult)>;

/// O que a execução mudou na sessão, aplicado na thread da UI no fim.
struct Effects {
    bool used{false};     ///< houve um USE bem-sucedido
    std::string database; ///< banco atual depois da execução
    bool server{false};   ///< DDL de banco (CREATE/DROP DATABASE): recarrega o servidor inteiro
    std::vector<std::string> databases;
};

struct RunResult {
    QuerySummary summary;
    Effects effects;
};

bool isCode(const SqlToken& token) {
    return token.kind != SqlTokenKind::Whitespace && token.kind != SqlTokenKind::LineComment &&
           token.kind != SqlTokenKind::BlockComment;
}

} // namespace

class QueryService::Runner {
public:
    struct Options {
        bool stopOnError{true};
        bool explain{false};
        bool keepRows{true}; ///< false: só conta as linhas (runFile)
        std::size_t maxRows{1000};
        std::size_t sqlLimit{0}; ///< 0: sem corte
        std::uint32_t firstLine{1};
    };

    Runner(session::WorkContext& context, const std::atomic<bool>& cancelled, Deliver deliver, std::string database)
        : m_context(&context),
          m_cancelled(&cancelled),
          m_deliver(std::move(deliver)),
          m_tokenizer(context.connection().dialect().syntax()) {
        m_effects.database = std::move(database);
    }

    void configure(const Options& options) { m_options = options; }

    [[nodiscard]] core::sql::SqlSyntax syntax() const { return m_tokenizer.syntax(); }
    [[nodiscard]] bool stopped() const noexcept { return m_stopped || m_cancelled->load(); }

    /// Executa as sentenças na ordem; em lote, junta as que podem ir na mesma ida ao servidor.
    void run(std::span<const SqlStatement> statements, ExecutionMode mode) {
        std::size_t next = 0;
        while (next < statements.size() && !stopped()) {
            std::size_t count = 1;
            if (mode == ExecutionMode::Batch && batchable(statements[next].text)) {
                while (next + count < statements.size() && batchable(statements[next + count].text)) {
                    ++count;
                }
            }
            next += trip(statements.subspan(next, count));
        }
    }

    [[nodiscard]] RunResult finish() {
        if (m_cancelled->load()) {
            m_summary.cancelled = true;
        }
        return RunResult{.summary = m_summary, .effects = std::move(m_effects)};
    }

private:
    /// Pode ir junto com outras num lote: sem ";" interno (BEGIN…END) e sem comentário de linha no fim.
    [[nodiscard]] bool batchable(std::string_view sql) const {
        const auto tokens = m_tokenizer.tokenize(sql);
        for (const SqlToken& token : tokens) {
            if (token.kind == SqlTokenKind::Operator && sql[token.offset] == ';') {
                return false;
            }
        }
        return tokens.empty() || tokens.back().kind != SqlTokenKind::LineComment;
    }

    /// Uma ida ao servidor. Os resultados correspondem às sentenças, na ordem. Devolve quantas foram consumidas.
    std::size_t trip(std::span<const SqlStatement> group) {
        std::string sql;
        for (const SqlStatement& statement : group) {
            if (!sql.empty()) {
                sql += ";\n";
            }
            sql += sent(statement);
        }
        std::size_t position = 0;
        const auto current = [&]() -> const SqlStatement& {
            return group[std::min(position, group.size() - 1)];
        };
        try {
            const std::unique_ptr<core::IExecution> execution = m_context->connection().execute(sql);
            core::IExecution::Timing before;
            while (execution->nextResult()) {
                StatementResult result = start(current());
                result.rowsAffected = execution->rowsAffected();
                if (core::IResultSet* rows = execution->resultSet()) {
                    read(*rows, result);
                }
                result.warnings = execution->warnings();
                const core::IExecution::Timing now = execution->timing();
                result.timing = core::IExecution::Timing{.server = now.server - before.server,
                                                         .network = now.network - before.network};
                before = now;
                succeeded(current(), std::move(result));
                ++position;
                if (m_cancelled->load()) {
                    break; // o resto do lote é descartado com a execução
                }
            }
            return group.size();
        } catch (const core::CancelledError&) {
            failed(current(), std::current_exception(), true);
            m_stopped = true;
            return group.size();
        } catch (...) {
            failed(current(), std::current_exception(), false);
            if (m_options.stopOnError) {
                m_stopped = true;
            }
            return std::min(position + 1, group.size());
        }
    }

    [[nodiscard]] std::string sent(const SqlStatement& statement) const {
        return m_options.explain ? "EXPLAIN " + statement.text : statement.text;
    }

    StatementResult start(const SqlStatement& statement) {
        StatementResult result;
        result.index = m_index++;
        result.sql = sent(statement);
        if (m_options.sqlLimit > 0 && result.sql.size() > m_options.sqlLimit) {
            result.sql.resize(m_options.sqlLimit);
        }
        result.line = m_options.firstLine + statement.line - 1;
        return result;
    }

    void read(core::IResultSet& rows, StatementResult& result) {
        if (!m_options.keepRows) {
            while (rows.next()) {
                ++m_summary.rowsReturned;
            }
            return;
        }
        ResultTable table;
        const auto columns = rows.columns();
        table.columns.assign(columns.begin(), columns.end());
        while (rows.next()) {
            if (table.rows.size() >= m_options.maxRows) {
                table.truncated = true;
                break;
            }
            std::vector<std::optional<std::string>> row;
            row.reserve(columns.size());
            for (std::size_t column = 0; column < columns.size(); ++column) {
                row.push_back(rows.isNull(column) ? std::nullopt : std::optional<std::string>(rows.text(column)));
            }
            table.rows.push_back(std::move(row));
        }
        m_summary.rowsReturned += table.rows.size();
        result.table = std::move(table);
    }

    void succeeded(const SqlStatement& statement, StatementResult result) {
        ++m_summary.statements;
        if (result.rowsAffected > 0) {
            m_summary.rowsAffected += result.rowsAffected;
        }
        m_summary.warnings += result.warnings;
        m_summary.timing.server += result.timing.server;
        m_summary.timing.network += result.timing.network;
        if (!m_options.explain) {
            track(statement.text);
        }
        m_deliver(std::move(result));
    }

    void failed(const SqlStatement& statement, const std::exception_ptr& error, bool cancelled) {
        StatementResult result = start(statement);
        result.error = session::describe(error);
        ++m_summary.statements;
        if (cancelled) {
            m_summary.cancelled = true;
        } else {
            ++m_summary.failed;
        }
        m_deliver(std::move(result));
    }

    /// Guarda os efeitos de uma sentença bem-sucedida (USE e DDL).
    void track(std::string_view sql) {
        std::vector<SqlToken> code;
        for (const SqlToken& token : m_tokenizer.tokenize(sql)) {
            if (isCode(token)) {
                code.push_back(token);
                if (code.size() == 2) {
                    break;
                }
            }
        }
        if (code.empty() || code[0].kind != SqlTokenKind::Word) {
            return;
        }
        const std::string verb = lowerAscii(sql.substr(code[0].offset, code[0].length));
        const std::string object =
            code.size() > 1 ? lowerAscii(sql.substr(code[1].offset, code[1].length)) : std::string();
        if (verb == "use") {
            if (code.size() > 1 &&
                (code[1].kind == SqlTokenKind::Word || code[1].kind == SqlTokenKind::QuotedIdentifier)) {
                m_effects.used = true;
                m_effects.database = identifierName(sql, code[1]);
            }
            return;
        }
        if (verb != "create" && verb != "alter" && verb != "drop" && verb != "rename" && verb != "truncate") {
            return;
        }
        if (object == "database" || object == "schema" || m_effects.database.empty()) {
            m_effects.server = true;
        } else if (std::ranges::find(m_effects.databases, m_effects.database) == m_effects.databases.end()) {
            m_effects.databases.push_back(m_effects.database);
        }
    }

    session::WorkContext* m_context;
    const std::atomic<bool>* m_cancelled;
    Deliver m_deliver;
    core::sql::SqlTokenizer m_tokenizer;
    Options m_options;
    QuerySummary m_summary;
    Effects m_effects;
    std::size_t m_index{0};
    bool m_stopped{false};
};

QueryService::QueryService(session::SessionService& sessions, QueryHistoryStore* history)
    : m_sessions(&sessions), m_history(history) {}

void QueryService::execute(std::string_view sessionId, QueryRequest request, StatementCallback onStatement,
                           services::Completion<QuerySummary> done) {
    std::string historyText = request.sql;
    start(
        sessionId,
        [query = std::move(request)](Runner& runner) {
            runner.configure(Runner::Options{.stopOnError = query.stopOnError,
                                             .explain = query.explain,
                                             .keepRows = true,
                                             .maxRows = query.maxRows,
                                             .sqlLimit = 0,
                                             .firstLine = query.firstLine});
            const std::vector<SqlStatement> statements = core::sql::SqlSplitter::split(query.sql, runner.syntax());
            runner.run(statements, query.mode);
        },
        std::move(onStatement), std::move(done), std::move(historyText));
}

void QueryService::runFile(std::string_view sessionId, std::filesystem::path file, bool stopOnError,
                           StatementCallback onStatement, services::Completion<QuerySummary> done) {
    const std::u8string name = file.u8string();
    std::string historyText = "-- " + std::string(name.begin(), name.end());
    start(
        sessionId,
        [path = std::move(file), stopOnError](Runner& runner) {
            runner.configure(Runner::Options{.stopOnError = stopOnError,
                                             .explain = false,
                                             .keepRows = false,
                                             .maxRows = 0,
                                             .sqlLimit = kFileSqlLimit,
                                             .firstLine = 1});
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                const std::u8string shown = path.u8string();
                throw std::runtime_error("Não foi possível abrir o arquivo " + std::string(shown.begin(), shown.end()));
            }
            core::sql::SqlSplitter splitter(runner.syntax());
            const core::sql::SqlSplitter::Sink sink = [&runner](SqlStatement&& statement) {
                if (!runner.stopped()) {
                    const SqlStatement one = std::move(statement);
                    runner.run(std::span<const SqlStatement>(&one, 1), ExecutionMode::OneByOne);
                }
            };
            std::vector<char> buffer(kFileChunk);
            while (!runner.stopped()) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto got = static_cast<std::size_t>(in.gcount());
                if (got == 0) {
                    break;
                }
                splitter.feed(std::string_view(buffer.data(), got), sink);
            }
            if (in.bad()) {
                throw std::runtime_error("Erro ao ler o arquivo SQL");
            }
            if (!runner.stopped()) {
                splitter.finish(sink);
            }
        },
        std::move(onStatement), std::move(done), std::move(historyText));
}

namespace {

/// Marca como UserSql o SQL que o worker envia enquanto existir.
class UserSqlScope {
public:
    explicit UserSqlScope(log::SessionLogger& logger) noexcept : m_logger(&logger) { logger.setUserSql(true); }
    UserSqlScope(const UserSqlScope&) = delete;
    UserSqlScope& operator=(const UserSqlScope&) = delete;
    UserSqlScope(UserSqlScope&&) = delete;
    UserSqlScope& operator=(UserSqlScope&&) = delete;
    ~UserSqlScope() { m_logger->setUserSql(false); }

private:
    log::SessionLogger* m_logger;
};

} // namespace

void QueryService::start(std::string_view sessionId, Body body, StatementCallback onStatement,
                         services::Completion<QuerySummary> done, std::string historyText) {
    std::string id(sessionId);
    const session::OpenSession* open = m_sessions->find(sessionId);
    std::string database = open != nullptr ? open->currentDatabase() : std::string();
    auto flag = std::make_shared<std::atomic<bool>>(false);
    m_cancelFlags.insert_or_assign(id, flag);
    auto started = std::chrono::system_clock::now();
    Deliver deliver = [ui = m_sessions->dispatcher(), report = std::move(onStatement)](StatementResult result) {
        ui([callback = report, statement = std::move(result)] {
            if (callback) {
                callback(statement);
            }
        });
    };
    m_sessions->run<RunResult>(
        sessionId,
        [work = std::move(body), flag, send = std::move(deliver), database](session::WorkContext& context) {
            const UserSqlScope userSql(context.logger());
            Runner runner(context, *flag, send, database);
            work(runner);
            return runner.finish();
        },
        [this, id, flag, database, started, text = std::move(historyText),
         finish = std::move(done)](Outcome<RunResult> outcome) {
            const auto found = m_cancelFlags.find(id);
            if (found != m_cancelFlags.end() && found->second == flag) {
                m_cancelFlags.erase(found);
            }
            if (!outcome.ok()) {
                finish(Outcome<QuerySummary>::failure(outcome.error()));
                return;
            }
            RunResult result = std::move(outcome).value();
            if (result.effects.used) {
                m_sessions->setCurrentDatabase(id, result.effects.database);
            }
            if (result.effects.server) {
                m_sessions->refresh(id);
            } else {
                for (std::string& changed : result.effects.databases) {
                    m_sessions->refresh(id, std::move(changed));
                }
            }
            if (m_history != nullptr) {
                HistoryEntry entry;
                entry.time = started;
                entry.sessionId = id;
                entry.database = database;
                entry.sql = text;
                entry.duration = std::chrono::duration_cast<std::chrono::milliseconds>(result.summary.timing.server +
                                                                                       result.summary.timing.network);
                entry.failed = result.summary.failed > 0;
                try {
                    m_history->add(std::move(entry));
                } catch (...) { // NOLINT(bugprone-empty-catch): o histórico não gravado não afeta a execução
                }
            }
            finish(Outcome<QuerySummary>::success(result.summary));
        });
}

void QueryService::cancel(std::string_view sessionId) {
    const auto found = m_cancelFlags.find(sessionId);
    if (found != m_cancelFlags.end()) {
        found->second->store(true);
    }
    m_sessions->cancel(sessionId);
}

} // namespace mydb::services::query
