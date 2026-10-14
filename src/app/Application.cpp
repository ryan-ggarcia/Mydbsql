#include "app/Application.hpp"

#include <QString>

#include <chrono>

namespace mydb::app {

namespace {

constexpr std::chrono::milliseconds kFrameBudget{16}; // um quadro a 60 fps (§2.3)

} // namespace

Application::Application(int& argc, char** argv, bool enableWatchdog) : QApplication(argc, argv) {
    setApplicationName(QStringLiteral("Mydbsql"));
    setApplicationVersion(QStringLiteral(MYDB_VERSION));
    if (enableWatchdog) {
        m_watchdog.emplace(kFrameBudget);
    }
}

bool Application::notify(QObject* receiver, QEvent* event) {
    if (!m_watchdog) {
        return QApplication::notify(receiver, event);
    }
    const EventLoopWatchdog::Scope scope(*m_watchdog, receiver, event);
    return QApplication::notify(receiver, event);
}

} // namespace mydb::app
