#pragma once

#include "core/interfaces/IExecution.hpp"
#include "core/interfaces/IMetadataProvider.hpp"
#include "core/interfaces/ISqlDialect.hpp"
#include "core/interfaces/OptionalCapabilities.hpp"
#include "core/interfaces/ServerInfo.hpp"

#include <memory>
#include <string_view>

namespace mydb::core {

/// Uma conexão aberta. Não é thread-safe: cada sessão usa a sua a partir de um único ConnectionWorker
/// (CLAUDE.md §4.5). A exceção é cancelCurrent(), que pode ser chamado de qualquer thread.
/// Falhas são lançadas como DbError (ConnectionError, QueryError, CancelledError).
class IConnection {
public:
    virtual ~IConnection() = default;

    virtual void open() = 0;
    virtual void close() noexcept = 0;
    /// Com reconnect, tenta reabrir se a conexão tiver caído. Devolve se a conexão está viva.
    virtual bool ping(bool reconnect) = 0;

    /// Envia o lote (uma ou mais sentenças) e devolve a execução, posicionada antes do primeiro resultado.
    /// Lança QueryError se a primeira sentença falhar. Uma execução anterior ainda aberta é descartada.
    [[nodiscard]] virtual std::unique_ptr<IExecution> execute(std::string_view sql) = 0;
    /// Thread-safe. Interrompe a execução em andamento, que termina com CancelledError.
    virtual void cancelCurrent() = 0;
    virtual void useDatabase(std::string_view database) = 0;

    [[nodiscard]] virtual ServerInfo serverInfo() const = 0;
    [[nodiscard]] virtual const ISqlDialect& dialect() const = 0;
    [[nodiscard]] virtual IMetadataProvider& metadata() = 0;
    [[nodiscard]] virtual Capabilities capabilities() = 0;

protected:
    IConnection() = default;
    IConnection(const IConnection&) = default;
    IConnection(IConnection&&) = default;
    IConnection& operator=(const IConnection&) = default;
    IConnection& operator=(IConnection&&) = default;
};

} // namespace mydb::core
