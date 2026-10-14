#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mydb::app {

/// Resultado de --measure-startup (CLAUDE.md §2.5). Os nomes dos campos do JSON são o contrato
/// com tools/check_budget.py e com bench/baseline/*.json.
struct StartupReport {
    std::string platform;
    std::string version;
    double startupMs{0.0};     ///< de main() até a primeira exposição da janela
    std::uint64_t rssKb{0};    ///< RSS / working set após o tempo de acomodação
    std::uint64_t anonKb{0};   ///< memória anônima privada / private bytes / phys_footprint
    std::size_t libsLoaded{0}; ///< bibliotecas dinâmicas mapeadas
    bool debugBuild{false};
};

/// Uma linha de JSON, sem dependências externas.
[[nodiscard]] std::string toJson(const StartupReport& report);

} // namespace mydb::app
