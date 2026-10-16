#pragma once

#include "infra/platform/DynamicLibrary.hpp"
#include "sdk/IModule.hpp"
#include "sdk/ModuleContext.hpp"

#include <filesystem>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::infra::platform {
class IPlatform;
}

namespace mydb::app {

class ModuleLoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct LoadedModule {
    std::string id;
    sdk::Version apiVersion;
    std::filesystem::path file; ///< vazio para módulos estáticos

    friend bool operator==(const LoadedModule&, const LoadedModule&) = default;
};

/// Carrega módulos (CLAUDE.md §2.4) e é o ModuleContext que eles enxergam. Dono das bibliotecas dinâmicas
/// e dos registries: os registries são destruídos antes das bibliotecas, porque as fábricas registradas
/// moram no código delas.
class ModuleHost final : public sdk::ModuleContext {
public:
    explicit ModuleHost(const infra::platform::IPlatform& platform);
    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    ModuleHost(ModuleHost&&) = delete;
    ModuleHost& operator=(ModuleHost&&) = delete;
    ~ModuleHost();

    /// Ids de módulos desligados em Preferências → Módulos. Um módulo desligado não é carregado.
    void setDisabled(std::set<std::string, std::less<>> moduleIds);

    /// Registra um módulo compilado no executável. Devolve false se ele estiver desligado.
    bool addBuiltin(sdk::IModule& module);

    /// Carrega um .so/.dll/.dylib. Devolve false se o módulo estiver desligado (o arquivo nem é aberto).
    /// Lança ModuleLoadError com uma mensagem clara se o arquivo não for um módulo compatível.
    bool loadDynamic(const std::filesystem::path& file);

    /// Módulos dinâmicos em `directory` (pelo sufixo do SO), em ordem alfabética, sem carregá-los.
    [[nodiscard]] std::vector<std::filesystem::path> discover(const std::filesystem::path& directory) const;

    [[nodiscard]] const std::vector<LoadedModule>& modules() const noexcept { return m_modules; }

    [[nodiscard]] sdk::DriverRegistry& drivers() override { return m_drivers; }
    [[nodiscard]] sdk::CommandRegistry& commands() override { return m_commands; }
    [[nodiscard]] sdk::EventBus& events() override { return m_events; }
    [[nodiscard]] std::filesystem::path currentModuleFile() const override { return m_registeringFile; }

    /// Driver pelo id (ex.: "mysql"). Se ainda não estiver registrado, carrega o módulo "driver.<id>" de
    /// `moduleDirectory` agora, na primeira conexão (CLAUDE.md §2.2, regra 2). nullptr se não existir ou
    /// estiver desligado; ModuleLoadError se o arquivo existir mas não carregar.
    [[nodiscard]] const sdk::DriverRegistry::Entry* driver(std::string_view driverId,
                                                           const std::filesystem::path& moduleDirectory);

private:
    void registerModule(sdk::IModule& module, const std::filesystem::path& file);
    [[nodiscard]] bool isDisabledStem(std::string_view stem) const;

    const infra::platform::IPlatform& m_platform;
    // Ordem importa: membros são destruídos de baixo para cima, então as bibliotecas saem por último.
    std::vector<infra::platform::DynamicLibrary> m_libraries;
    sdk::EventBus m_events;
    sdk::DriverRegistry m_drivers;
    sdk::CommandRegistry m_commands;
    std::vector<LoadedModule> m_modules;
    std::set<std::string, std::less<>> m_disabled;
    std::filesystem::path m_registeringFile;
};

} // namespace mydb::app
