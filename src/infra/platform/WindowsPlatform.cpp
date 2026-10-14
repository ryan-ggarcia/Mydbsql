#include "infra/platform/IPlatform.hpp"
#include "infra/platform/WindowsSupport.hpp"

// windows.h precisa vir antes de psapi.h e shlobj.h.
#include <knownfolders.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace mydb::infra::platform {

namespace {

constexpr std::wstring_view kAppFolder = L"Mydbsql";

std::filesystem::path knownFolder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    const HRESULT result = ::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> owner(raw, &::CoTaskMemFree);
    if (FAILED(result) || raw == nullptr) {
        throw std::runtime_error("SHGetKnownFolderPath falhou: " +
                                 windows::errorMessage(static_cast<DWORD>(HRESULT_CODE(result))));
    }
    return {raw};
}

std::filesystem::path moduleFileName(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("GetModuleFileNameW falhou: " + windows::errorMessage(::GetLastError()));
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return {buffer};
        }
        buffer.resize(buffer.size() * 2); // caminho longo (\\?\...): tenta de novo com mais espaço
    }
}

class WindowsPlatform final : public IPlatform {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "windows"; }

    [[nodiscard]] std::filesystem::path standardDirectory(StandardDirectory which) const override {
        switch (which) {
        case StandardDirectory::Config:
        case StandardDirectory::Data:
            return knownFolder(FOLDERID_RoamingAppData) / kAppFolder;
        case StandardDirectory::Cache:
            return knownFolder(FOLDERID_LocalAppData) / kAppFolder / L"cache";
        }
        throw std::invalid_argument("StandardDirectory inválido");
    }

    [[nodiscard]] std::filesystem::path executablePath() const override { return moduleFileName(nullptr); }

    [[nodiscard]] std::filesystem::path pluginDirectory() const override {
        return executablePath().parent_path() / L"modules";
    }

    [[nodiscard]] std::string_view sharedLibrarySuffix() const noexcept override { return ".dll"; }

    [[nodiscard]] ProcessMemory processMemory() const override {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a API recebe a struct base
        auto* base = reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters);
        if (::GetProcessMemoryInfo(::GetCurrentProcess(), base, sizeof(counters)) == 0) {
            throw std::runtime_error("GetProcessMemoryInfo falhou: " + windows::errorMessage(::GetLastError()));
        }
        return {.residentKb = counters.WorkingSetSize / 1024, .privateKb = counters.PrivateUsage / 1024};
    }

    [[nodiscard]] std::vector<std::filesystem::path> loadedLibraries() const override {
        const HANDLE process = ::GetCurrentProcess();
        std::vector<HMODULE> modules(256);
        for (;;) {
            DWORD needed = 0;
            const auto bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
            if (::EnumProcessModules(process, modules.data(), bytes, &needed) == 0) {
                throw std::runtime_error("EnumProcessModules falhou: " + windows::errorMessage(::GetLastError()));
            }
            if (needed <= bytes) {
                modules.resize(needed / sizeof(HMODULE));
                break;
            }
            modules.resize((needed / sizeof(HMODULE)) + 16); // mais módulos carregados entre as chamadas
        }

        std::vector<std::filesystem::path> result;
        result.reserve(modules.size());
        std::unordered_set<std::wstring> seen;
        for (HMODULE module : modules) {
            std::filesystem::path path = moduleFileName(module);
            if (seen.insert(path.native()).second) {
                result.push_back(std::move(path));
            }
        }
        return result;
    }

    void openUrl(std::string_view url) const override {
        if (!isAllowedExternalUrl(url)) {
            throw std::invalid_argument("URL recusada (só http, https e mailto): " + std::string(url));
        }
        const std::wstring wide = windows::widen(url);
        const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        // ShellExecuteW devolve um código > 32 em caso de sucesso (contrato herdado do Win16).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            throw std::runtime_error("Não foi possível abrir a URL: " + windows::errorMessage(::GetLastError()));
        }
    }
};

} // namespace

std::unique_ptr<IPlatform> createPlatform() {
    return std::make_unique<WindowsPlatform>();
}

} // namespace mydb::infra::platform
