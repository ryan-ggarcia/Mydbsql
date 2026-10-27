#pragma once

#include "sdk/IModule.hpp"

namespace mydb::modules::query {

/// Módulo estático com os comandos das abas de query (Fase 5): nova aba, executar, cancelar, EXPLAIN, arquivos,
/// formatar, comentar, localizar, histórico e snippets.
[[nodiscard]] sdk::IModule& module();

} // namespace mydb::modules::query
