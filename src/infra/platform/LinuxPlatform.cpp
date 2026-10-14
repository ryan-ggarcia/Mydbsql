#include "infra/platform/IPlatform.hpp"
#include "infra/platform/PosixSupport.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace mydb::infra::platform {

namespace {

constexpr std::string_view kAppFolder = "mydbsql";

/// $XDG_*_HOME se for um caminho absoluto (a especificação manda ignorar os relativos), senão o padrão.
std::filesystem::path xdgDirectory(const char* variable, const std::filesystem::path& fallbackUnderHome) {
    if (const char* value = std::getenv(variable); value != nullptr) { // NOLINT(concurrency-mt-unsafe)
        std::filesystem::path path(value);
        if (path.is_absolute()) {
            return path / kAppFolder;
        }
    }
    return posix::homeDirectory() / fallbackUnderHome / kAppFolder;
}

/// Lê o valor em kB de uma linha "Campo:   1234 kB" de /proc/self/status ou smaps_rollup.
std::optional<std::uint64_t> readProcField(const char* file, std::string_view field) {
    std::ifstream input(file);
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() <= field.size() || !line.starts_with(field) || line[field.size()] != ':') {
            continue;
        }
        const std::size_t begin = line.find_first_of("0123456789", field.size());
        if (begin == std::string::npos) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        const auto [end, error] = std::from_chars(line.data() + begin, line.data() + line.size(), value);
        (void)end;
        return error == std::errc{} ? std::optional(value) : std::nullopt;
    }
    return std::nullopt;
}

class LinuxPlatform final : public IPlatform {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "linux"; }

    [[nodiscard]] std::filesystem::path standardDirectory(StandardDirectory which) const override {
        switch (which) {
        case StandardDirectory::Config:
            return xdgDirectory("XDG_CONFIG_HOME", ".config");
        case StandardDirectory::Cache:
            return xdgDirectory("XDG_CACHE_HOME", ".cache");
        case StandardDirectory::Data:
            return xdgDirectory("XDG_DATA_HOME", std::filesystem::path(".local") / "share");
        }
        throw std::invalid_argument("StandardDirectory inválido");
    }

    [[nodiscard]] std::filesystem::path executablePath() const override {
        return std::filesystem::read_symlink("/proc/self/exe");
    }

    [[nodiscard]] std::filesystem::path pluginDirectory() const override {
        // <prefix>/bin/mydbsql → <prefix>/lib/mydbsql/modules (CLAUDE.md §2.4)
        return (executablePath().parent_path().parent_path() / "lib" / "mydbsql" / "modules").lexically_normal();
    }

    [[nodiscard]] std::string_view sharedLibrarySuffix() const noexcept override { return ".so"; }

    [[nodiscard]] ProcessMemory processMemory() const override {
        ProcessMemory memory;
        memory.residentKb = readProcField("/proc/self/status", "VmRSS").value_or(0);
        // smaps_rollup existe desde o kernel 4.14; RssAnon é a alternativa mais barata e próxima.
        std::optional<std::uint64_t> anonymous = readProcField("/proc/self/smaps_rollup", "Anonymous");
        if (!anonymous) {
            anonymous = readProcField("/proc/self/status", "RssAnon");
        }
        memory.privateKb = anonymous.value_or(0);
        return memory;
    }

    [[nodiscard]] std::vector<std::filesystem::path> loadedLibraries() const override {
        // Formato de /proc/self/maps: endereço perms offset dev inode [caminho]
        std::ifstream maps("/proc/self/maps");
        std::vector<std::filesystem::path> result;
        std::unordered_set<std::string> seen;
        std::string line;
        while (std::getline(maps, line)) {
            const std::size_t slash = line.find('/');
            if (slash == std::string::npos) {
                continue;
            }
            std::string path = line.substr(slash);
            const std::size_t deleted = path.rfind(" (deleted)");
            if (deleted != std::string::npos) {
                path.erase(deleted);
            }
            const bool isSharedObject = path.find(".so") != std::string::npos;
            if (isSharedObject && seen.insert(path).second) {
                result.emplace_back(path);
            }
        }
        return result;
    }

    void openUrl(std::string_view url) const override {
        if (!isAllowedExternalUrl(url)) {
            throw std::invalid_argument("URL recusada (só http, https e mailto): " + std::string(url));
        }
        posix::spawnDetached({"xdg-open", std::string(url)});
    }
};

} // namespace

std::unique_ptr<IPlatform> createPlatform() {
    return std::make_unique<LinuxPlatform>();
}

} // namespace mydb::infra::platform
