#pragma once

// Utilitários comuns ao Linux e ao macOS. Só incluído por fontes compilados nesses SOs.

#include <filesystem>
#include <string>
#include <vector>

namespace mydb::infra::platform::posix {

/// $HOME ou, se não estiver definido, o diretório do usuário no banco de senhas do sistema.
[[nodiscard]] std::filesystem::path homeDirectory();

/// Inicia um programa sem shell e sem esperar por ele (o processo filho é recolhido numa thread
/// separada para não virar zumbi). argv[0] é procurado no PATH. Lança std::runtime_error se falhar.
void spawnDetached(const std::vector<std::string>& argv);

} // namespace mydb::infra::platform::posix
