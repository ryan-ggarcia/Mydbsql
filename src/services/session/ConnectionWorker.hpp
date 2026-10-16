#pragma once

#include "core/interfaces/IConnection.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace mydb::services::session {

/// A thread de uma sessão aberta (CLAUDE.md §4.5): a única que usa a IConnection, porque as bibliotecas C
/// não são thread-safe por handle. Executa os trabalhos em ordem de chegada e, se a sessão pedir
/// keep-alive, faz ping depois de cada período ocioso.
class ConnectionWorker {
public:
    using Job = std::function<void(core::IConnection&)>;
    /// Chamado na thread do worker depois de cada ping do keep-alive.
    using PingObserver = std::function<void(bool alive)>;

    /// `keepAlive` zero desliga o ping periódico.
    ConnectionWorker(std::unique_ptr<core::IConnection> connection, std::chrono::milliseconds keepAlive,
                     PingObserver onPing = {});
    ConnectionWorker(const ConnectionWorker&) = delete;
    ConnectionWorker& operator=(const ConnectionWorker&) = delete;
    ConnectionWorker(ConnectionWorker&&) = delete;
    ConnectionWorker& operator=(ConnectionWorker&&) = delete;
    /// Descarta os trabalhos pendentes, cancela o que estiver rodando, fecha a conexão e espera a thread.
    ~ConnectionWorker();

    /// Thread-safe. Exceções que escaparem do trabalho são engolidas: quem posta trata os próprios erros.
    void post(Job job);

    /// Thread-safe e sem bloquear quem chama: o cancelamento (que no MySQL abre uma conexão auxiliar) roda
    /// numa thread própria e só atinge o trabalho que estava rodando na hora do pedido.
    void cancelCurrent();

    /// Trabalhos na fila, sem contar o que está rodando.
    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] bool busy() const;

private:
    void run(const std::stop_token& stop);
    void ping();
    /// Cancela o trabalho `generation` se ele ainda estiver rodando.
    void cancelRunning(std::uint64_t generation) noexcept;

    std::unique_ptr<core::IConnection> m_connection;
    std::chrono::milliseconds m_keepAlive;
    PingObserver m_onPing;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Job> m_jobs;

    // Separa "um trabalho está rodando" do resto da fila, para o cancelamento não atingir o próximo.
    mutable std::mutex m_runningMutex;
    bool m_running{false};
    std::uint64_t m_generation{0}; ///< conta os trabalhos iniciados

    std::mutex m_cancelMutex;
    std::vector<std::future<void>> m_cancels;
    std::jthread m_thread; // por último: a thread começa depois que o resto existe
};

} // namespace mydb::services::session
