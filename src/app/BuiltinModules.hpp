#pragma once

#include "app/ModuleHost.hpp"

namespace mydb::app {

/// Registra os módulos estáticos, compilados no executável (CLAUDE.md §2.4).
void registerBuiltinModules(ModuleHost& host);

} // namespace mydb::app
