#include "app/ModuleDriverCatalog.hpp"

#include <string_view>
#include <utility>

namespace mydb::app {

namespace {

constexpr std::string_view kDriverPrefix = "mydb_driver_";

} // namespace

ModuleDriverCatalog::ModuleDriverCatalog(ModuleHost& host, std::filesystem::path moduleDirectory)
    : m_host(&host), m_moduleDirectory(std::move(moduleDirectory)) {}

std::vector<std::string> ModuleDriverCatalog::availableDrivers() const {
    std::vector<std::string> ids;
    for (const std::filesystem::path& file : m_host->discover(m_moduleDirectory)) {
        const std::string stem = file.stem().string();
        if (stem.starts_with(kDriverPrefix) && stem.size() > kDriverPrefix.size()) {
            ids.push_back(stem.substr(kDriverPrefix.size()));
        }
    }
    return ids;
}

const core::IDriver* ModuleDriverCatalog::driver(std::string_view id) {
    if (const auto found = m_drivers.find(id); found != m_drivers.end()) {
        return found->second.get();
    }
    if (m_host->driver(id, m_moduleDirectory) == nullptr) {
        return nullptr;
    }
    auto created = m_host->drivers().create(id);
    const core::IDriver* result = created.get();
    m_drivers.emplace(std::string(id), std::move(created));
    return result;
}

} // namespace mydb::app
