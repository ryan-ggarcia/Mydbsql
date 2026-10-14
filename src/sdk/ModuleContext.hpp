#pragma once

#include "core/interfaces/IDriver.hpp"
#include "sdk/EventBus.hpp"
#include "sdk/Registry.hpp"

namespace mydb::sdk {

using DriverRegistry = Registry<core::IDriver>;

/// O que um módulo enxerga do aplicativo: só registries e serviços públicos (CLAUDE.md §2.4).
/// Os demais registries (exportadores, ferramentas, comandos…) entram com as fases que os usam;
/// cada acréscimo aqui muda o major de kApiVersion.
class ModuleContext {
public:
    [[nodiscard]] virtual DriverRegistry& drivers() = 0;
    [[nodiscard]] virtual EventBus& events() = 0;

protected:
    ModuleContext() = default;
    ModuleContext(const ModuleContext&) = default;
    ModuleContext(ModuleContext&&) = default;
    ModuleContext& operator=(const ModuleContext&) = default;
    ModuleContext& operator=(ModuleContext&&) = default;
    ~ModuleContext() = default;
};

} // namespace mydb::sdk
