#pragma once

#include "core/interfaces/IConnection.hpp"
#include "sdk/EventBus.hpp"
#include "services/common/Dispatcher.hpp"
#include "services/common/Outcome.hpp"
#include "services/log/LogBuffer.hpp"
#include "services/session/CachedMetadataProvider.hpp"
#include "services/session/ConnectionWorker.hpp"
#include "services/session/Session.hpp"

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mydb::services::session {

/// O que um trabalho recebe na thread do worker da sessão.
class WorkContext {
public:
    WorkContext(core::IConnection& connection, CachedMetadataProvider& metadata) noexcept
        : m_connection(&connection), m_metadata(&metadata) {}

    [[nodiscard]] core::IConnection& connection() const noexcept { return *m_connection; }
    [[nodiscard]] CachedMetadataProvider& metadata() const noexcept { return *m_metadata; }

private:
    core::IConnection* m_connection;
    CachedMetadataProvider* m_metadata;
};

/// Uma sessão aberta. Os campos são lidos e alterados só na thread da UI.
class OpenSession {
public:
    OpenSession(Session session, std::unique_ptr<core::IConnection> connection, log::LogBuffer& log,
                ConnectionWorker::PingObserver onPing);
    OpenSession(const OpenSession&) = delete;
    OpenSession& operator=(const OpenSession&) = delete;
    OpenSession(OpenSession&&) = delete;
    OpenSession& operator=(OpenSession&&) = delete;
    ~OpenSession();

    [[nodiscard]] const Session& session() const noexcept { return m_session; }
    [[nodiscard]] const std::string& id() const noexcept { return m_session.id; }
    [[nodiscard]] const core::ServerInfo& server() const noexcept { return m_server; }
    /// A conexão oferece IServerAdmin (aba Host: variáveis, status e processos).
    [[nodiscard]] bool hasServerAdmin() const noexcept { return m_hasServerAdmin; }
    [[nodiscard]] const std::string& currentDatabase() const noexcept { return m_currentDatabase; }
    [[nodiscard]] bool alive() const noexcept { return m_alive; }

private:
    friend class SessionService;

    Session m_session;
    core::ServerInfo m_server;
    bool m_hasServerAdmin{false};
    std::string m_currentDatabase;
    bool m_alive{true};
    // Ordem importa: o worker (dono da conexão) é destruído antes do cache e do logger que a conexão usa.
    log::SessionLogger m_logger;
    std::unique_ptr<CachedMetadataProvider> m_metadata;
    std::unique_ptr<ConnectionWorker> m_worker;
};

/// Sessões abertas e o acesso assíncrono a elas (CLAUDE.md §4.5). Usado só na thread da UI: o trabalho roda
/// no ConnectionWorker de cada sessão e o resultado volta pelo Dispatcher. Publica os eventos de
/// sdk/SessionEvents.hpp no EventBus.
class SessionService {
public:
    SessionService(sdk::EventBus& events, Dispatcher dispatcher, log::LogBuffer& log);
    SessionService(const SessionService&) = delete;
    SessionService& operator=(const SessionService&) = delete;
    SessionService(SessionService&&) = delete;
    SessionService& operator=(SessionService&&) = delete;
    ~SessionService();

    /// Passa a ser dono de `connection`, já aberta. false (e a conexão é fechada) se a sessão já estiver aberta.
    bool add(Session session, std::unique_ptr<core::IConnection> connection);
    /// Cancela o que estiver rodando, fecha a conexão e publica SessionClosed.
    void close(std::string_view sessionId);
    void closeAll();

    /// Na ordem em que foram abertas.
    [[nodiscard]] std::vector<const OpenSession*> sessions() const;
    [[nodiscard]] const OpenSession* find(std::string_view sessionId) const;

    /// Roda `work(WorkContext&) -> R` no worker da sessão e entrega o resultado (ou a exceção) a `done` na
    /// thread da UI. Erros também vão para o log. Se a sessão não estiver aberta, `done` recebe ConnectionError.
    template <typename R, typename Work>
    void run(std::string_view sessionId, Work work, Completion<R> done);

    void databases(std::string_view sessionId, Completion<std::vector<std::string>> done);
    void databaseSummaries(std::string_view sessionId, Completion<std::vector<core::model::DatabaseSummary>> done);
    void objects(std::string_view sessionId, std::string database, Completion<std::vector<core::model::DbObject>> done);
    /// Em caso de sucesso, publica DatabaseChanged antes de chamar `done`.
    void useDatabase(std::string_view sessionId, const std::string& database, Completion<Done> done);

    /// Descarta o cache afetado e publica ObjectsChanged. `database` vazio: o servidor inteiro.
    void refresh(std::string_view sessionId, std::string database = {},
                 const std::optional<core::model::ObjectRef>& object = std::nullopt);

    /// Interrompe o trabalho que está rodando na sessão, sem bloquear.
    void cancel(std::string_view sessionId);

private:
    using Job = std::function<void(WorkContext&)>;

    [[nodiscard]] OpenSession* findOpen(std::string_view sessionId) const;
    static void post(OpenSession& session, Job job);
    void logError(std::string_view sessionId, const std::exception_ptr& error) noexcept;
    void onPing(const std::string& sessionId, bool alive) noexcept;
    [[nodiscard]] static std::exception_ptr notOpen(std::string_view sessionId);

    sdk::EventBus& m_events;
    Dispatcher m_dispatcher;
    log::LogBuffer& m_log;
    std::vector<std::unique_ptr<OpenSession>> m_sessions;
};

/// Texto de uma exceção para o log e para a UI: mensagem e, se for um DbError com dica, a dica.
[[nodiscard]] std::string describe(const std::exception_ptr& error);

template <typename R, typename Work>
void SessionService::run(std::string_view sessionId, Work work, Completion<R> done) {
    OpenSession* session = findOpen(sessionId);
    if (session == nullptr) {
        m_dispatcher([done = std::move(done), error = notOpen(sessionId)] { done(Outcome<R>::failure(error)); });
        return;
    }
    post(*session, [this, id = session->id(), work = std::move(work), done = std::move(done)](WorkContext& context) {
        std::optional<Outcome<R>> outcome;
        try {
            outcome.emplace(Outcome<R>::success(work(context)));
        } catch (...) {
            outcome.emplace(Outcome<R>::failure(std::current_exception()));
            logError(id, outcome->error());
        }
        m_dispatcher([done, result = std::move(*outcome)]() mutable { done(std::move(result)); });
    });
}

} // namespace mydb::services::session
