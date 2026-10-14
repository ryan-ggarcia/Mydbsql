#include "app/EventLoopWatchdog.hpp"

#include <QMetaObject>
#include <QObject>

#include <spdlog/spdlog.h>

#include <cstdio>

namespace mydb::app {

EventLoopWatchdog::Scope::Scope(EventLoopWatchdog& watchdog, const QObject* receiver, const QEvent* event) noexcept
    : m_watchdog(watchdog),
      m_outermost(watchdog.m_depth == 0),
      m_start(Clock::now()),
      m_eventType(event != nullptr ? event->type() : QEvent::None),
      m_receiverClass(receiver != nullptr ? receiver->metaObject()->className() : "?") {
    ++m_watchdog.m_depth;
}

EventLoopWatchdog::Scope::~Scope() {
    --m_watchdog.m_depth;
    if (!m_outermost) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_start);
    if (elapsed <= m_watchdog.m_threshold) {
        return;
    }
    try {
        spdlog::warn("UI bloqueada por {} ms (evento {}, receptor {})", elapsed.count(), static_cast<int>(m_eventType),
                     m_receiverClass);
    } catch (...) {
        // Diagnóstico de debug: uma falha ao registrar o aviso não pode derrubar o event loop.
        std::fputs("mydbsql: watchdog não conseguiu registrar no log\n", stderr);
    }
}

} // namespace mydb::app
