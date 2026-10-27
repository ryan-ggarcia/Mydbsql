#pragma once

#include "core/model/ObjectRef.hpp"
#include "core/sql/SqlSyntax.hpp"
#include "services/common/Outcome.hpp"
#include "services/query/CompletionIndex.hpp"
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
#include <vector>

namespace mydb::services::query {

/// O que o editor precisa de uma sessão: as regras léxicas do dialeto e o índice inicial do autocomplete.
struct CompletionSeed {
    core::sql::SqlSyntax syntax;
    std::shared_ptr<const CompletionIndex> index;
};

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

    /// No máximo tantos erros do runFile chegam a `onStatement`; os demais só entram em summary.failed.
    static constexpr std::size_t kFileMaxErrors = 100;

    /// Roda um arquivo .sql inteiro em streaming, com memória constante (§2.1): lê em blocos, divide e executa
    /// sentença a sentença, sem guardar linhas de resultado (só contagens e erros). `onStatement` recebe
    /// StatementResult sem `table`, e não um por sentença: para a fila da thread da UI não crescer quando a UI
    /// for mais lenta que o servidor, chegam só os primeiros kFileMaxErrors erros e, como progresso, uma
    /// sentença bem-sucedida quando não houver nenhuma entrega pendente na UI (o `index` dela diz quantas já
    /// foram). Assim há no máximo kFileMaxErrors + 1 resultados na fila. Com stopOnError, para no primeiro erro.
    void runFile(std::string_view sessionId, std::filesystem::path file, bool stopOnError,
                 StatementCallback onStatement, services::Completion<QuerySummary> done);

    void cancel(std::string_view sessionId);

    /// Monta no worker da sessão as regras léxicas do dialeto e o índice do autocomplete (palavras-chave,
    /// funções de `functionCatalog` (JSON de parseFunctionCatalog; vazio = sem funções), bancos e objetos do
    /// banco atual), entregues na thread da UI.
    void completionSeed(std::string_view sessionId, std::string functionCatalog,
                        services::Completion<CompletionSeed> done);

    /// Uma cópia de `base` com as colunas de `tables`, montada no worker da sessão e entregue na thread da UI.
    /// Tabela cujas colunas não puderam ser lidas fica sem colunas.
    void withColumns(std::string_view sessionId, std::shared_ptr<const CompletionIndex> base,
                     std::vector<core::model::ObjectRef> tables,
                     services::Completion<std::shared_ptr<const CompletionIndex>> done);

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
