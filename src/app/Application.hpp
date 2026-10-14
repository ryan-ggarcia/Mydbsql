#pragma once

#include "app/EventLoopWatchdog.hpp"

#include <QApplication>

#include <optional>

namespace mydb::app {

/// QApplication com o watchdog do event loop (ligado nos builds de debug).
class Application final : public QApplication {
public:
    Application(int& argc, char** argv, bool enableWatchdog);

    bool notify(QObject* receiver, QEvent* event) override;

private:
    std::optional<EventLoopWatchdog> m_watchdog;
};

} // namespace mydb::app
