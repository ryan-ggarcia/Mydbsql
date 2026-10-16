#pragma once

#include <cstdint>
#include <string_view>

namespace mydb::core {

/// Categorias do painel de log (as mesmas do HeidiSQL, CLAUDE.md §1.3).
enum class LogCategory : std::uint8_t {
    Info,
    Sql,     ///< SQL que o próprio app envia (introspecção, SET, KILL…)
    UserSql, ///< SQL digitado pelo usuário
    Error,
    Debug,
    Script,
};

/// Recebe o que uma conexão envia ao servidor. Separado da IConnection (CLAUDE.md §1.4): a conexão só
/// avisa; quem guarda, filtra e mostra é o services/ e a UI. Pode ser chamado de qualquer thread.
class IQueryLogger {
public:
    virtual ~IQueryLogger() = default;

    virtual void log(LogCategory category, std::string_view text) noexcept = 0;

protected:
    IQueryLogger() = default;
    IQueryLogger(const IQueryLogger&) = default;
    IQueryLogger(IQueryLogger&&) = default;
    IQueryLogger& operator=(const IQueryLogger&) = default;
    IQueryLogger& operator=(IQueryLogger&&) = default;
};

} // namespace mydb::core
