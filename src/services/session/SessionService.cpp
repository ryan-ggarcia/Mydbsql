#include "services/session/SessionService.hpp"

#include "core/errors/DbError.hpp"
#include "sdk/SessionEvents.hpp"

#include <algorithm>

namespace mydb::services::session {

std::string describe(const std::exception_ptr& error) {
    if (!error) {
        return {};
    }
    try {
        std::rethrow_exception(error);
    } catch (const core::DbError& e) {
        std::string text = e.what();
        if (!e.info().hint.empty()) {
            text += "\n" + e.info().hint;
        }
        return text;
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "Erro desconhecido";
    }
}

OpenSession::OpenSession(Session session, std::unique_ptr<core::IConnection> connection, log::LogBuffer& log,
                         ConnectionWorker::PingObserver onPing)
    : m_session(std::move(session)),
      m_server(connection->serverInfo()),
      m_hasServerAdmin(connection->capabilities().serverAdmin != nullptr),
      m_currentDatabase(m_session.params.defaultDatabase),
      m_logger(log, m_session.id),
      m_metadata(std::make_unique<CachedMetadataProvider>(connection->metadata())) {
    // Até aqui a conexão só foi tocada nesta thread; a partir do worker, só ele a usa.
    connection->setQueryLogger(&m_logger);
    const auto keepAlive = std::chrono::duration_cast<std::chrono::milliseconds>(m_session.params.keepAlive);
    m_worker = std::make_unique<ConnectionWorker>(std::move(connection), keepAlive, std::move(onPing));
}

OpenSession::~OpenSession() = default;

SessionService::SessionService(sdk::EventBus& events, Dispatcher dispatcher, log::LogBuffer& log)
    : m_events(events), m_dispatcher(std::move(dispatcher)), m_log(log) {}

SessionService::~SessionService() {
    try {
        closeAll();
    } catch (...) { // NOLINT(bugprone-empty-catch): os workers já foram destruídos; só a notificação se perdeu
    }
}

bool SessionService::add(Session session, std::unique_ptr<core::IConnection> connection) {
    if (findOpen(session.id) != nullptr) {
        connection->close();
        return false;
    }
    // Sem const: o lambda copia o id e precisa ser movível sem lançar.
    std::string id = session.id;
    std::string name = session.name;
    m_sessions.push_back(std::make_unique<OpenSession>(std::move(session), std::move(connection), m_log,
                                                       [this, id](bool alive) { onPing(id, alive); }));
    const OpenSession& opened = *m_sessions.back();
    m_log.append(core::LogCategory::Info, id,
                 "Conectado a " + name + " (" + opened.server().productName + " " + opened.server().versionString +
                     ", conexão " + std::to_string(opened.server().connectionId) + ")");
    m_events.publish(sdk::SessionOpened{.sessionId = id});
    return true;
}

void SessionService::close(std::string_view sessionId) {
    const auto found = std::ranges::find_if(m_sessions, [&](const auto& s) { return s->id() == sessionId; });
    if (found == m_sessions.end()) {
        return;
    }
    std::unique_ptr<OpenSession> closing = std::move(*found);
    m_sessions.erase(found);
    const std::string id = closing->id();
    m_events.publish(sdk::SessionClosed{.sessionId = id});
    closing.reset();
    m_log.append(core::LogCategory::Info, id, "Desconectado");
}

void SessionService::closeAll() {
    while (!m_sessions.empty()) {
        close(std::string(m_sessions.back()->id()));
    }
}

std::vector<const OpenSession*> SessionService::sessions() const {
    std::vector<const OpenSession*> list;
    list.reserve(m_sessions.size());
    for (const auto& session : m_sessions) {
        list.push_back(session.get());
    }
    return list;
}

const OpenSession* SessionService::find(std::string_view sessionId) const {
    return findOpen(sessionId);
}

OpenSession* SessionService::findOpen(std::string_view sessionId) const {
    const auto found = std::ranges::find_if(m_sessions, [&](const auto& s) { return s->id() == sessionId; });
    return found == m_sessions.end() ? nullptr : found->get();
}

void SessionService::post(OpenSession& session, Job job) {
    CachedMetadataProvider* metadata = session.m_metadata.get();
    session.m_worker->post([metadata, job = std::move(job)](core::IConnection& connection) {
        WorkContext context(connection, *metadata);
        job(context);
    });
}

void SessionService::databases(std::string_view sessionId, Completion<std::vector<std::string>> done) {
    run<std::vector<std::string>>(
        sessionId, [](WorkContext& context) { return context.metadata().databases(); }, std::move(done));
}

void SessionService::databaseSummaries(std::string_view sessionId,
                                       Completion<std::vector<core::model::DatabaseSummary>> done) {
    run<std::vector<core::model::DatabaseSummary>>(
        sessionId, [](WorkContext& context) { return context.metadata().databaseSummaries(); }, std::move(done));
}

void SessionService::objects(std::string_view sessionId, std::string database,
                             Completion<std::vector<core::model::DbObject>> done) {
    run<std::vector<core::model::DbObject>>(
        sessionId,
        [database = std::move(database)](WorkContext& context) { return context.metadata().objects(database); },
        std::move(done));
}

void SessionService::useDatabase(std::string_view sessionId, const std::string& database, Completion<Done> done) {
    run<Done>(
        sessionId,
        [target = database](WorkContext& context) {
            context.connection().useDatabase(target);
            return Done{};
        },
        [this, owner = std::string(sessionId), target = database, done = std::move(done)](Outcome<Done> outcome) {
            try {
                if (outcome.ok()) {
                    if (OpenSession* session = findOpen(owner)) {
                        session->m_currentDatabase = target;
                        m_events.publish(sdk::DatabaseChanged{.sessionId = owner, .database = target});
                    }
                }
                if (done) {
                    done(std::move(outcome));
                }
            } catch (...) {
                logError(owner, std::current_exception());
            }
        });
}

void SessionService::refresh(std::string_view sessionId, std::string database,
                             const std::optional<core::model::ObjectRef>& object) {
    OpenSession* session = findOpen(sessionId);
    if (session == nullptr) {
        return;
    }
    // A invalidação entra na fila do worker: quem pedir dados depois do evento recebe a versão nova.
    post(*session, [database, target = object](WorkContext& context) {
        if (target) {
            context.metadata().invalidateObject(*target);
        } else if (!database.empty()) {
            context.metadata().invalidateDatabase(database);
        } else {
            context.metadata().invalidateAll();
        }
    });
    m_events.publish(
        sdk::ObjectsChanged{.sessionId = std::string(sessionId), .database = std::move(database), .object = object});
}

void SessionService::cancel(std::string_view sessionId) {
    if (OpenSession* session = findOpen(sessionId)) {
        session->m_worker->cancelCurrent();
    }
}

void SessionService::logError(std::string_view sessionId, const std::exception_ptr& error) noexcept {
    try {
        m_log.append(core::LogCategory::Error, sessionId, describe(error));
    } catch (...) { // NOLINT(bugprone-empty-catch): sem memória para o log, a linha se perde
    }
}

void SessionService::onPing(const std::string& sessionId, bool alive) noexcept {
    // Thread do worker: o estado da sessão só muda na thread da UI.
    try {
        m_dispatcher([this, id = sessionId, alive] {
            try {
                OpenSession* session = findOpen(id);
                if (session == nullptr || session->m_alive == alive) {
                    return;
                }
                session->m_alive = alive;
                m_log.append(alive ? core::LogCategory::Info : core::LogCategory::Error, id,
                             alive ? "Conexão restabelecida" : "Conexão perdida; nova tentativa no próximo keep-alive");
                m_events.publish(sdk::ConnectionStateChanged{.sessionId = id, .alive = alive});
            } catch (...) {
                logError(id, std::current_exception());
            }
        });
    } catch (...) { // NOLINT(bugprone-empty-catch): sem memória; o próximo ping tenta de novo
    }
}

std::exception_ptr SessionService::notOpen(std::string_view sessionId) {
    return std::make_exception_ptr(core::ConnectionError("A sessão não está aberta: " + std::string(sessionId)));
}

} // namespace mydb::services::session
