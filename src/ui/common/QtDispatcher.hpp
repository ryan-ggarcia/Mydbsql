#pragma once

#include "services/common/Dispatcher.hpp"

#include <QMetaObject>
#include <QObject>

#include <exception>
#include <functional>
#include <utility>

namespace mydb::ui {

/// Dispatcher dos services que entrega os callbacks na thread de `context` (a da UI), pela fila de eventos.
/// Se `context` for destruído antes, os callbacks pendentes são descartados pelo Qt. `context` precisa viver
/// mais que todas as threads que usam o dispatcher.
[[nodiscard]] inline services::Dispatcher qtDispatcher(QObject* context) {
    return [context](std::function<void()> callback) {
        QMetaObject::invokeMethod(
            context,
            [callback = std::move(callback)] {
                // Uma exceção não pode atravessar o laço de eventos do Qt.
                try {
                    callback();
                } catch (const std::exception& error) {
                    qWarning("Erro num callback da UI: %s", error.what());
                } catch (...) {
                    qWarning("Erro desconhecido num callback da UI");
                }
            },
            Qt::QueuedConnection);
    };
}

} // namespace mydb::ui
