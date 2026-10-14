#include "app/StartupProbe.hpp"

#include "app/StartupReport.hpp"
#include "infra/platform/IPlatform.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <exception>
#include <fstream>
#include <utility>

namespace mydb::app {

namespace {

constexpr int kGiveUpAfterMs = 15'000; // sem exposição até aqui, algo está errado com a sessão gráfica

void failMeasurement(const char* reason) {
    spdlog::error("--measure-startup: {}", reason);
    std::fprintf(stderr, "--measure-startup: %s\n", reason);
    QCoreApplication::exit(1);
}

} // namespace

StartupProbe::StartupProbe(const infra::platform::IPlatform& platform, std::chrono::steady_clock::time_point startedAt,
                           CommandLineOptions options, QObject* parent)
    : QObject(parent), m_platform(platform), m_startedAt(startedAt), m_options(std::move(options)) {}

void StartupProbe::attach(QWidget& window) {
    QWindow* handle = window.windowHandle();
    if (handle == nullptr) {
        failMeasurement("a janela não tem QWindow nativo");
        return;
    }
    handle->installEventFilter(this);
    QTimer::singleShot(kGiveUpAfterMs, this, [this] {
        if (m_startupMs < 0.0) {
            failMeasurement("a janela não foi exposta a tempo");
        }
    });
}

bool StartupProbe::eventFilter(QObject* watched, QEvent* event) {
    if (m_startupMs < 0.0 && event->type() == QEvent::Expose) {
        const auto* window = qobject_cast<QWindow*>(watched);
        if (window != nullptr && window->isExposed()) {
            const auto elapsed = std::chrono::steady_clock::now() - m_startedAt;
            m_startupMs = std::chrono::duration<double, std::milli>(elapsed).count();
            QTimer::singleShot(m_options.settleMs, this, [this] { finish(); });
        }
    }
    return QObject::eventFilter(watched, event);
}

void StartupProbe::finish() {
    try {
        StartupReport report;
        report.platform = std::string(m_platform.name());
        report.version = MYDB_VERSION;
        report.startupMs = m_startupMs;
        const infra::platform::ProcessMemory memory = m_platform.processMemory();
        report.rssKb = memory.residentKb;
        report.anonKb = memory.privateKb;
        report.libsLoaded = m_platform.loadedLibraries().size();
        report.debugBuild = MYDB_DEBUG_BUILD != 0;

        const std::string json = toJson(report) + "\n";
        if (m_options.measureOutput) {
            std::ofstream output(*m_options.measureOutput, std::ios::binary | std::ios::trunc);
            output << json;
            if (!output) {
                failMeasurement("não foi possível gravar o arquivo de saída");
                return;
            }
        } else {
            std::fwrite(json.data(), 1, json.size(), stdout);
            std::fflush(stdout);
        }
        QCoreApplication::exit(0);
    } catch (const std::exception& error) {
        spdlog::error("--measure-startup: {}", error.what());
        QCoreApplication::exit(1);
    }
}

} // namespace mydb::app
