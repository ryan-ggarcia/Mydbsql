#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace mydb::app {

struct CommandLineOptions {
    /// --measure-startup[=<arquivo>]: mede a inicialização, imprime JSON e encerra (CLAUDE.md §2.5).
    bool measureStartup{false};
    /// Destino do JSON; sem valor, vai para a saída padrão.
    std::optional<std::filesystem::path> measureOutput;
    /// --settle-ms=<n>: espera após a primeira exposição antes de ler a memória (padrão 500 ms).
    int settleMs{500};
    /// Mensagem para o usuário quando um argumento do Mydbsql é inválido; vazio se tudo está certo.
    std::string error;
};

/// Lê só os argumentos do Mydbsql. Os do Qt (ex.: -platform) e os desconhecidos são ignorados,
/// porque o QApplication os trata depois.
[[nodiscard]] CommandLineOptions parseCommandLine(std::span<const char* const> args);

} // namespace mydb::app
