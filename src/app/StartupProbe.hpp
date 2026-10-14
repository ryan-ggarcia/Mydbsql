#pragma once

#include "app/CommandLine.hpp"

#include <QObject>

#include <chrono>

class QWidget;

namespace mydb::infra::platform {
class IPlatform;
}

namespace mydb::app {

/// Implementa --measure-startup: espera a primeira exposição da janela, deixa o processo se
/// acomodar por settleMs, lê memória e bibliotecas pelo IPlatform, emite o JSON e encerra o app.
class StartupProbe final : public QObject {
public:
    StartupProbe(const infra::platform::IPlatform& platform, std::chrono::steady_clock::time_point startedAt,
                 CommandLineOptions options, QObject* parent = nullptr);

    /// Chamar depois de window.show(), quando o QWindow nativo já existe.
    void attach(QWidget& window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void finish();

    const infra::platform::IPlatform& m_platform;
    std::chrono::steady_clock::time_point m_startedAt;
    CommandLineOptions m_options;
    double m_startupMs{-1.0};
};

} // namespace mydb::app
