#include "app/ModuleHost.hpp"

#include "infra/platform/IPlatform.hpp"

#include <algorithm>
#include <exception>
#include <system_error>
#include <utility>

namespace mydb::app {

ModuleHost::ModuleHost(const infra::platform::IPlatform& platform) : m_platform(platform) {}

ModuleHost::~ModuleHost() = default;

void ModuleHost::setDisabled(std::set<std::string, std::less<>> moduleIds) {
    m_disabled = std::move(moduleIds);
}

bool ModuleHost::addBuiltin(sdk::IModule& module) {
    if (m_disabled.contains(module.id())) {
        return false;
    }
    registerModule(module, {});
    return true;
}

bool ModuleHost::loadDynamic(const std::filesystem::path& file) {
    const std::string stem = file.stem().string();
    if (isDisabledStem(stem)) {
        return false;
    }

    infra::platform::DynamicLibrary library = [&file] {
        try {
            return infra::platform::DynamicLibrary::open(file);
        } catch (const infra::platform::DynamicLibraryError& error) {
            throw ModuleLoadError(error.what());
        }
    }();

    const auto entry = library.function<sdk::ModuleEntryFunction>(std::string(sdk::kModuleEntrySymbol).c_str());
    if (entry == nullptr) {
        throw ModuleLoadError(file.string() + " não é um módulo do Mydbsql (não exporta " +
                              std::string(sdk::kModuleEntrySymbol) + ")");
    }
    sdk::IModule* module = entry();
    if (module == nullptr) {
        throw ModuleLoadError(file.string() + ": " + std::string(sdk::kModuleEntrySymbol) + " devolveu nulo");
    }
    const sdk::Version version = module->apiVersion();
    if (!sdk::isCompatible(sdk::kApiVersion, version)) {
        throw ModuleLoadError("O módulo " + std::string(module->id()) + " foi compilado para a API " +
                              version.toString() + ", mas este Mydbsql oferece a API " + sdk::kApiVersion.toString() +
                              ". Atualize o módulo.");
    }
    if (sdk::moduleFileStem(module->id()) != stem) {
        throw ModuleLoadError(file.string() + ": o arquivo deveria se chamar " + sdk::moduleFileStem(module->id()) +
                              " para o módulo " + std::string(module->id()));
    }

    // A biblioteca fica viva antes do registro: as fábricas registradas apontam para o código dela.
    m_libraries.push_back(std::move(library));
    registerModule(*module, file);
    return true;
}

std::vector<std::filesystem::path> ModuleHost::discover(const std::filesystem::path& directory) const {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return files;
    }
    const std::string suffix(m_platform.sharedLibrarySuffix());
    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        const std::filesystem::path& path = item.path();
        if (item.is_regular_file(error) && path.extension().string() == suffix &&
            path.stem().string().starts_with("mydb_")) {
            files.push_back(path);
        }
    }
    std::ranges::sort(files);
    return files;
}

void ModuleHost::registerModule(sdk::IModule& module, const std::filesystem::path& file) {
    const std::string id(module.id());
    if (std::ranges::find(m_modules, id, &LoadedModule::id) != m_modules.end()) {
        throw ModuleLoadError("Módulo carregado duas vezes: " + id);
    }
    m_registeringFile = file;
    try {
        module.registerInto(*this);
    } catch (const std::exception& error) {
        m_registeringFile.clear();
        throw ModuleLoadError("O módulo " + id + " falhou ao se registrar: " + error.what());
    }
    m_registeringFile.clear();
    m_modules.push_back(LoadedModule{.id = id, .apiVersion = module.apiVersion(), .file = file});
}

const sdk::DriverRegistry::Entry* ModuleHost::driver(std::string_view driverId,
                                                     const std::filesystem::path& moduleDirectory) {
    if (const auto* entry = m_drivers.find(driverId); entry != nullptr) {
        return entry;
    }
    const std::string moduleId = "driver." + std::string(driverId);
    const std::filesystem::path file =
        moduleDirectory / (sdk::moduleFileStem(moduleId) + std::string(m_platform.sharedLibrarySuffix()));
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || !loadDynamic(file)) {
        return nullptr;
    }
    return m_drivers.find(driverId);
}

bool ModuleHost::isDisabledStem(std::string_view stem) const {
    return std::ranges::any_of(m_disabled, [stem](const std::string& id) { return sdk::moduleFileStem(id) == stem; });
}

} // namespace mydb::app
