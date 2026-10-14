#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace mydb::infra::platform {

/// Diretórios-padrão do usuário, já com a subpasta do Mydbsql.
enum class StandardDirectory : std::uint8_t {
    Config, ///< XDG_CONFIG_HOME, %APPDATA% ou ~/Library/Application Support
    Cache,  ///< XDG_CACHE_HOME, %LOCALAPPDATA% ou ~/Library/Caches
    Data,   ///< XDG_DATA_HOME, %APPDATA% ou ~/Library/Application Support
};

/// Memória do processo atual, em KiB. As métricas de cada SO são aproximações umas das outras
/// (CLAUDE.md §1.5): compare valores da mesma plataforma.
struct ProcessMemory {
    std::uint64_t residentKb{0}; ///< RSS (Linux, macOS) ou working set (Windows)
    std::uint64_t privateKb{0};  ///< anônima privada (Linux), private bytes (Windows), phys_footprint (macOS)
};

/// Fronteira com o sistema operacional. Fora de src/infra/platform/ nenhum código depende de SO.
class IPlatform {
public:
    virtual ~IPlatform() = default;
    IPlatform(const IPlatform&) = delete;
    IPlatform& operator=(const IPlatform&) = delete;
    IPlatform(IPlatform&&) = delete;
    IPlatform& operator=(IPlatform&&) = delete;

    /// "linux", "windows" ou "macos".
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Caminho absoluto; o diretório pode ainda não existir.
    [[nodiscard]] virtual std::filesystem::path standardDirectory(StandardDirectory which) const = 0;

    [[nodiscard]] virtual std::filesystem::path executablePath() const = 0;

    /// Onde ficam os módulos e drivers dinâmicos (CLAUDE.md §2.4).
    [[nodiscard]] virtual std::filesystem::path pluginDirectory() const = 0;

    /// ".so", ".dll" ou ".dylib".
    [[nodiscard]] virtual std::string_view sharedLibrarySuffix() const noexcept = 0;

    [[nodiscard]] virtual ProcessMemory processMemory() const = 0;

    /// Bibliotecas dinâmicas mapeadas no processo, sem repetição (§2.5: nenhum driver antes de conectar).
    [[nodiscard]] virtual std::vector<std::filesystem::path> loadedLibraries() const = 0;

    /// Abre a URL no aplicativo padrão. Só aceita http, https e mailto (veja isAllowedExternalUrl);
    /// lança std::invalid_argument para outros esquemas e std::runtime_error se o SO falhar.
    virtual void openUrl(std::string_view url) const = 0;

protected:
    IPlatform() = default;
};

/// Cria a implementação do SO em que o programa foi compilado.
[[nodiscard]] std::unique_ptr<IPlatform> createPlatform();

/// Política comum aos três SOs: impede que um texto qualquer (ex.: valor de uma célula da grade)
/// vire a execução de um programa local.
[[nodiscard]] bool isAllowedExternalUrl(std::string_view url) noexcept;

} // namespace mydb::infra::platform
