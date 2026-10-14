#pragma once

#include <QEvent>

#include <chrono>

class QObject;

namespace mydb::app {

/// Registra no log todo evento que ocupa a thread da UI por mais que o limite (CLAUDE.md §2.3, regra 1).
/// Só mede o despacho mais externo: eventos aninhados contam dentro do evento que os disparou.
class EventLoopWatchdog final {
public:
    using Clock = std::chrono::steady_clock;

    explicit EventLoopWatchdog(std::chrono::milliseconds threshold) noexcept : m_threshold(threshold) {}

    /// RAII em volta de um QApplication::notify.
    class Scope final {
    public:
        Scope(EventLoopWatchdog& watchdog, const QObject* receiver, const QEvent* event) noexcept;
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

    private:
        EventLoopWatchdog& m_watchdog;
        bool m_outermost;
        Clock::time_point m_start;
        QEvent::Type m_eventType;
        const char* m_receiverClass; // vem do QMetaObject: continua válido se o receptor for destruído
    };

    [[nodiscard]] std::chrono::milliseconds threshold() const noexcept { return m_threshold; }

private:
    std::chrono::milliseconds m_threshold;
    int m_depth{0};
};

} // namespace mydb::app
