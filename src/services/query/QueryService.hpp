#pragma once

#include "services/common/Outcome.hpp"
#include "services/query/QueryHistoryStore.hpp"
#include "services/query/QueryTypes.hpp"
#include "services/session/SessionService.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace mydb::services::query {

/// Executa SQL do usuário numa sessão aberta (CLAUDE.md §5, Fase 5), no ConnectionWorker dela.
///
/// Contrato:
/// - Divide o texto com o SqlSplitter do dialeto da sessão (respeita DELIMITER e BEGIN…END).
/// - Cada sentença vira um StatementResult entregue a `onStatement` na thread da UI, na ordem; `done` vem por
///   último com o resumo. Com stopOnError, a primeira falha encerra a execução (summary.failed = 1).
/// - cancel() interrompe a sentença em andamento; o resumo vem com cancelled = true.
/// - O SQL aparece no painel de log uma vez, na categoria UserSql (não duplicado como Sql).
/// - Depois de um DDL bem-sucedido (CREATE, ALTER, DROP, RENAME, TRUNCATE), chama SessionService::refresh do
///   banco atual (ou do servidor, para CREATE/DROP DATABASE/SCHEMA); depois de "USE x", o banco atual da
///   sessão passa a ser x e DatabaseChanged é publicado.
/// - Cada execução vai para o histórico (se houver um), com o texto, a sessão, o banco, a data e a duração.
/// Usado só na thread da UI.
class QueryService {
public:
    using StatementCallback = std::function<void(const StatementResult&)>;

    QueryService(session::SessionService& sessions, QueryHistoryStore* history);

    void execute(std::string_view sessionId, QueryRequest request, StatementCallback onStatement,
                 services::Completion<QuerySummary> done);

    /// Roda um arquivo .sql inteiro em streaming, com memória constante (§2.1): lê em blocos, divide e executa
    /// sentença a sentença, sem guardar linhas de resultado (só contagens e erros). `onStatement` recebe
    /// StatementResult sem `table`. Com stopOnError, para no primeiro erro.
    void runFile(std::string_view sessionId, std::filesystem::path file, bool stopOnError,
                 StatementCallback onStatement, services::Completion<QuerySummary> done);

    void cancel(std::string_view sessionId);

private:
    class Runner;
    using Body = std::function<void(Runner&)>;

    /// Roda `body` no worker da sessão, entrega os resultados na thread da UI e aplica os efeitos (DDL, USE).
    /// `historyText` é o que vai para o histórico.
    void start(std::string_view sessionId, Body body, StatementCallback onStatement,
               services::Completion<QuerySummary> done, std::string historyText);

    session::SessionService* m_sessions;
    QueryHistoryStore* m_history;
    /// Pedido de cancelamento da execução em andamento de cada sessão.
    std::map<std::string, std::shared_ptr<std::atomic<bool>>, std::less<>> m_cancelFlags;
};

} // namespace mydb::services::query
