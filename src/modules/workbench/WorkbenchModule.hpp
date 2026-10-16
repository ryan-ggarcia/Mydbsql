#pragma once

#include "sdk/IModule.hpp"

namespace mydb::modules::workbench {

/// Módulo estático com os comandos básicos da janela principal: sessões, atualizar, painéis, favoritos,
/// sobre e sair.
[[nodiscard]] sdk::IModule& module();

} // namespace mydb::modules::workbench
