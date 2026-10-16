#include "services/session/ConnectionWorker.hpp"

#include <utility>

namespace mydb::services::session {

ConnectionWorker::ConnectionWorker(std::unique_ptr<core::IConnection> connection, std::chrono::milliseconds keepAlive,
                                   PingObserver onPing)
    : m_connection(std::move(connection)),
      m_keepAlive(keepAlive),
      m_onPing(std::move(onPing)),
      m_thread([this](const std::stop_token& stop) { run(stop); }) {}

ConnectionWorker::~ConnectionWorker() {
    {
        const std::scoped_lock lock(m_mutex);
        m_jobs.clear();
    }
    m_thread.request_stop();
    {
        const std::scoped_lock lock(m_cancelMutex);
        m_cancels.clear(); // espera os cancelamentos em andamento
    }
    // Aqui pode bloquear: fechar a sessão espera a consulta em andamento ser interrompida.
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(m_runningMutex);
        generation = m_generation;
    }
    cancelRunning(generation);
    m_thread.join();
}

void ConnectionWorker::post(Job job) {
    {
        const std::scoped_lock lock(m_mutex);
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void ConnectionWorker::cancelCurrent() {
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(m_runningMutex);
        if (!m_running) {
            return;
        }
        generation = m_generation;
    }
    const std::scoped_lock lock(m_cancelMutex);
    std::erase_if(m_cancels, [](const std::future<void>& cancel) {
        return cancel.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    });
    m_cancels.push_back(std::async(std::launch::async, [this, generation] { cancelRunning(generation); }));
}

void ConnectionWorker::cancelRunning(std::uint64_t generation) noexcept {
    const std::scoped_lock lock(m_runningMutex);
    if (!m_running || m_generation != generation) {
        return;
    }
    try {
        m_connection->cancelCurrent();
    } catch (...) { // NOLINT(bugprone-empty-catch): sem cancelar, a consulta só termina sozinha
    }
}

std::size_t ConnectionWorker::pending() const {
    const std::scoped_lock lock(m_mutex);
    return m_jobs.size();
}

bool ConnectionWorker::busy() const {
    const std::scoped_lock lock(m_runningMutex);
    return m_running;
}

void ConnectionWorker::run(const std::stop_token& stop) {
    const auto hasJob = [this] {
        return !m_jobs.empty();
    };
    while (!stop.stop_requested()) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            const bool woke = m_keepAlive.count() > 0 ? m_wake.wait_for(lock, stop, m_keepAlive, hasJob)
                                                      : m_wake.wait(lock, stop, hasJob);
            if (stop.stop_requested()) {
                break;
            }
            if (!woke) {
                lock.unlock();
                ping();
                continue;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        {
            const std::scoped_lock lock(m_runningMutex);
            m_running = true;
            ++m_generation;
        }
        try {
            job(*m_connection);
        } catch (...) { // NOLINT(bugprone-empty-catch): o trabalho trata os próprios erros
        }
        {
            const std::scoped_lock lock(m_runningMutex);
            m_running = false;
        }
    }
    m_connection->close();
}

void ConnectionWorker::ping() {
    bool alive = false;
    try {
        alive = m_connection->ping(/*reconnect=*/true);
    } catch (...) { // NOLINT(bugprone-empty-catch): conta como conexão perdida
    }
    if (m_onPing) {
        m_onPing(alive);
    }
}

} // namespace mydb::services::session
