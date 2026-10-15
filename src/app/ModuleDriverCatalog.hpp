#pragma once

#include "app/ModuleHost.hpp"
#include "core/interfaces/IDriverCatalog.hpp"

#include <filesystem>
#include <map>
#include <memory>

namespace mydb::app {

/// Catálogo de drivers sobre os plugins da pasta de módulos: lista pelos nomes dos arquivos
/// (mydb_driver_<id>) e carrega o plugin só quando o driver é pedido.
class ModuleDriverCatalog final : public core::IDriverCatalog {
public:
    ModuleDriverCatalog(ModuleHost& host, std::filesystem::path moduleDirectory);

    [[nodiscard]] std::vector<std::string> availableDrivers() const override;
    [[nodiscard]] const core::IDriver* driver(std::string_view id) override;

private:
    ModuleHost* m_host;
    std::filesystem::path m_moduleDirectory;
    std::map<std::string, std::unique_ptr<core::IDriver>, std::less<>> m_drivers;
};

} // namespace mydb::app
