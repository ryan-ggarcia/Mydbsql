#include "infra/platform/DynamicLibrary.hpp"

#include <dlfcn.h>

#include <string>
#include <utility>

namespace mydb::infra::platform {

DynamicLibrary DynamicLibrary::open(const std::filesystem::path& path) {
    // RTLD_LOCAL: os símbolos do driver não vazam para outros módulos.
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* reason = ::dlerror();
        throw DynamicLibraryError("Falha ao carregar " + path.string() + ": " +
                                  (reason != nullptr ? reason : "erro desconhecido"));
    }
    return {handle, path};
}

DynamicLibrary::DynamicLibrary(void* handle, std::filesystem::path path) noexcept
    : m_handle(handle), m_path(std::move(path)) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)), m_path(std::move(other.m_path)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, nullptr);
        m_path = std::move(other.m_path);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() {
    close();
}

DynamicLibrary::Symbol DynamicLibrary::symbol(const char* name) const noexcept {
    if (m_handle == nullptr) {
        return nullptr;
    }
    void* address = ::dlsym(m_handle, name);
    // dlsym devolve void*; o POSIX garante que a conversão objeto→função vale ("conditionally-supported" no C++).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<Symbol>(address);
}

void DynamicLibrary::close() noexcept {
    if (m_handle != nullptr) {
        ::dlclose(m_handle);
        m_handle = nullptr;
    }
}

} // namespace mydb::infra::platform
