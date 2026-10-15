#pragma once

#include "core/interfaces/IDriver.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mydb::core {

/// Drivers instalados, sem carregá-los antes da hora (CLAUDE.md §2.2, regra 2). A implementação concreta
/// (app/) descobre os plugins pelo nome do arquivo e só os carrega em driver().
class IDriverCatalog {
public:
    virtual ~IDriverCatalog() = default;

    /// Ids dos drivers disponíveis (ex.: "mysql"), em ordem alfabética, sem carregar nenhum.
    [[nodiscard]] virtual std::vector<std::string> availableDrivers() const = 0;

    /// Carrega o driver na primeira chamada. nullptr se não existir; lança se existir e não carregar.
    /// O objeto vale enquanto o catálogo existir.
    [[nodiscard]] virtual const IDriver* driver(std::string_view id) = 0;

protected:
    IDriverCatalog() = default;
    IDriverCatalog(const IDriverCatalog&) = default;
    IDriverCatalog(IDriverCatalog&&) = default;
    IDriverCatalog& operator=(const IDriverCatalog&) = default;
    IDriverCatalog& operator=(IDriverCatalog&&) = default;
};

} // namespace mydb::core
