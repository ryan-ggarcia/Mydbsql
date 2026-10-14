#pragma once

#include <filesystem>
#include <optional>

namespace mydb::infra::logging {

struct LogOptions {
    /// Pasta do arquivo de log rotativo; sem valor, não grava em disco.
    std::optional<std::filesystem::path> directory;
    bool console{false}; ///< também escreve no stderr (padrão nos builds de debug)
    bool verbose{false}; ///< nível debug em vez de info
};

/// Configura o logger padrão do spdlog em modo assíncrono: a escrita acontece numa thread própria
/// e nunca bloqueia a thread da UI (CLAUDE.md §2.3). Nunca lança: se o arquivo não puder ser
/// aberto, segue só com o console.
void initialize(const LogOptions& options) noexcept;

/// Esvazia a fila e encerra a thread de log. Chamar antes de sair de main().
void shutdown() noexcept;

} // namespace mydb::infra::logging
