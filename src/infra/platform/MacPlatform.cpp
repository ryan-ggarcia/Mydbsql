#include "infra/platform/IPlatform.hpp"
#include "infra/platform/PosixSupport.hpp"

#include <mach-o/dyld.h>
#include <mach/mach.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace mydb::infra::platform {

namespace {

constexpr std::string_view kAppFolder = "Mydbsql";

class MacPlatform final : public IPlatform {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "macos"; }

    [[nodiscard]] std::filesystem::path standardDirectory(StandardDirectory which) const override {
        const std::filesystem::path library = posix::homeDirectory() / "Library";
        switch (which) {
        case StandardDirectory::Config:
        case StandardDirectory::Data:
            return library / "Application Support" / kAppFolder;
        case StandardDirectory::Cache:
            return library / "Caches" / kAppFolder;
        }
        throw std::invalid_argument("StandardDirectory inválido");
    }

    [[nodiscard]] std::filesystem::path executablePath() const override {
        std::uint32_t size = 0;
        ::_NSGetExecutablePath(nullptr, &size); // devolve o tamanho necessário
        std::string buffer(size, '\0');
        if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
            throw std::runtime_error("_NSGetExecutablePath falhou");
        }
        buffer.resize(std::char_traits<char>::length(buffer.c_str()));
        return std::filesystem::weakly_canonical(buffer);
    }

    [[nodiscard]] std::filesystem::path pluginDirectory() const override {
        // Fora de um bundle: <prefix>/bin/mydbsql → <prefix>/lib/mydbsql/modules. O .app (Fase 12)
        // usará Contents/PlugIns.
        return (executablePath().parent_path().parent_path() / "lib" / "mydbsql" / "modules").lexically_normal();
    }

    [[nodiscard]] std::string_view sharedLibrarySuffix() const noexcept override { return ".dylib"; }

    [[nodiscard]] ProcessMemory processMemory() const override {
        ProcessMemory memory;

        mach_task_basic_info_data_t basic{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): contrato de task_info
        if (::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&basic), &count) ==
            KERN_SUCCESS) {
            memory.residentKb = basic.resident_size / 1024;
        }

        task_vm_info_data_t vm{};
        count = TASK_VM_INFO_COUNT;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): contrato de task_info
        if (::task_info(::mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS) {
            memory.privateKb = vm.phys_footprint / 1024; // a métrica que o Activity Monitor chama de "Memory"
        }
        return memory;
    }

    [[nodiscard]] std::vector<std::filesystem::path> loadedLibraries() const override {
        const std::uint32_t count = ::_dyld_image_count();
        std::vector<std::filesystem::path> result;
        result.reserve(count);
        std::unordered_set<std::string> seen;
        for (std::uint32_t i = 0; i < count; ++i) {
            const char* name = ::_dyld_get_image_name(i);
            if (name != nullptr && seen.insert(name).second) {
                result.emplace_back(name);
            }
        }
        return result;
    }

    void openUrl(std::string_view url) const override {
        if (!isAllowedExternalUrl(url)) {
            throw std::invalid_argument("URL recusada (só http, https e mailto): " + std::string(url));
        }
        posix::spawnDetached({"open", std::string(url)});
    }
};

} // namespace

std::unique_ptr<IPlatform> createPlatform() {
    return std::make_unique<MacPlatform>();
}

} // namespace mydb::infra::platform
