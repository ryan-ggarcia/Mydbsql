#pragma once

// Eventos das sessões abertas (CLAUDE.md §4.4, Observer). Publicados pelo SessionService no EventBus, sempre
// na thread da UI, para que módulos e views reajam sem depender uns dos outros.

#include "core/model/ObjectRef.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace mydb::sdk {

struct SessionOpened {
    static constexpr std::string_view kEventId = "session.opened";
    std::string sessionId;
};

struct SessionClosed {
    static constexpr std::string_view kEventId = "session.closed";
    std::string sessionId;
};

/// O banco atual da sessão mudou (USE).
struct DatabaseChanged {
    static constexpr std::string_view kEventId = "session.database_changed";
    std::string sessionId;
    std::string database;
};

/// Objetos mudaram no servidor (DDL ou "atualizar"). `database` vazio: o servidor inteiro, inclusive a lista
/// de bancos. `object` vazio: o banco inteiro.
struct ObjectsChanged {
    static constexpr std::string_view kEventId = "session.objects_changed";
    std::string sessionId;
    std::string database;
    std::optional<core::model::ObjectRef> object;
};

/// Resultado de um ping do keep-alive que mudou o estado da conexão.
struct ConnectionStateChanged {
    static constexpr std::string_view kEventId = "session.connection_state";
    std::string sessionId;
    bool alive{true};
};

} // namespace mydb::sdk
