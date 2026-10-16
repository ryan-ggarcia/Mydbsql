#pragma once

// Faz o papel da thread da UI nos testes de services: guarda os callbacks e os executa quando o teste manda.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace mydb::tests {

class TestDispatcher {
public:
    [[nodiscard]] std::function<void(std::function<void()>)> dispatcher() {
        return [this](std::function<void()> callback) {
            {
                const std::scoped_lock lock(m_mutex);
                m_queue.push_back(std::move(callback));
            }
            m_wake.notify_all();
        };
    }

    /// Executa callbacks até `done()` ficar verdadeiro ou o tempo acabar. Devolve `done()`.
    template <typename Predicate>
    bool runUntil(Predicate done, std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            std::function<void()> callback;
            {
                std::unique_lock lock(m_mutex);
                if (!m_wake.wait_until(lock, deadline, [this] { return !m_queue.empty(); })) {
                    return done();
                }
                callback = std::move(m_queue.front());
                m_queue.pop_front();
            }
            callback();
        }
        return true;
    }

    /// Executa o que já estiver na fila.
    void runPending() {
        for (;;) {
            std::function<void()> callback;
            {
                const std::scoped_lock lock(m_mutex);
                if (m_queue.empty()) {
                    return;
                }
                callback = std::move(m_queue.front());
                m_queue.pop_front();
            }
            callback();
        }
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::function<void()>> m_queue;
};

} // namespace mydb::tests
