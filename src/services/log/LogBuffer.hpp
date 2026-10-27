#pragma once

#include "core/interfaces/IQueryLogger.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace mydb::services::log {

struct LogEntry {
    std::uint64_t sequence{0}; ///< crescente, começa em 1
    std::chrono::system_clock::time_point time;
    core::LogCategory category{core::LogCategory::Info};
    std::string sessionId; ///< vazio para mensagens do app
    std::string text;
};

/// Buffer circular do painel de log: guarda as últimas `capacity` entradas e descarta as mais antigas.
/// Thread-safe: os workers das sessões escrevem e a UI lê.
class LogBuffer {
public:
    /// Textos maiores que isto são cortados (um script de 1 GB não vai inteiro para o log).
    static constexpr std::size_t kMaxTextLength = 4096;

    explicit LogBuffer(std::size_t capacity = 5000);

    void append(core::LogCategory category, std::string_view sessionId, std::string_view text);

    /// Entradas com sequence > `after` que ainda estão no buffer, da mais antiga para a mais nova.
    [[nodiscard]] std::vector<LogEntry> since(std::uint64_t after) const;

    /// `listener` é chamado na thread de quem escreve quando chega a primeira entrada depois da última leitura
    /// com since(). Assim a UI recebe um aviso por rajada, não um por linha.
    void setListener(std::function<void()> listener);

    void clear();
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t size() const;

private:
    std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    std::uint64_t m_nextSequence{1};
    mutable bool m_notified{false};
    std::function<void()> m_listener;
};

/// Liga a IConnection de uma sessão ao LogBuffer.
class SessionLogger final : public core::IQueryLogger {
public:
    SessionLogger(LogBuffer& buffer, std::string sessionId) : m_buffer(&buffer), m_sessionId(std::move(sessionId)) {}

    void log(core::LogCategory category, std::string_view text) noexcept override;

    /// Enquanto ligado, o SQL que a thread que ligou envia entra como UserSql em vez de Sql (o SQL digitado
    /// aparece uma vez, na categoria certa). As outras threads, como a do cancelamento, não mudam.
    void setUserSql(bool on) noexcept;

private:
    LogBuffer* m_buffer;
    std::string m_sessionId;
    std::atomic<std::thread::id> m_userThread;
};

} // namespace mydb::services::log
