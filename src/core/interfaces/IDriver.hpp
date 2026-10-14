#pragma once

#include "core/interfaces/ConnectionParams.hpp"
#include "core/interfaces/IConnection.hpp"
#include "core/interfaces/ICredentialStore.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace mydb::core {

/// Fábrica de conexões de um SGBD, registrada no DriverRegistry (CLAUDE.md §2.4).
/// Identificado por texto, e não por enum, para que um SGBD novo não exija editar código central (OCP).
class IDriver {
public:
    virtual ~IDriver() = default;

    /// Estável e único, ex.: "mysql", "postgresql", "sqlite". É o ConnectionParams::driverId.
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;

    /// Cria a conexão sem abri-la; open() é chamado pelo ConnectionWorker.
    [[nodiscard]] virtual std::unique_ptr<IConnection> createConnection(const ConnectionParams& params,
                                                                        ICredentialStore& credentials) const = 0;

protected:
    IDriver() = default;
    IDriver(const IDriver&) = default;
    IDriver(IDriver&&) = default;
    IDriver& operator=(const IDriver&) = default;
    IDriver& operator=(IDriver&&) = default;
};

} // namespace mydb::core
