#include "infra/platform/DynamicLibrary.hpp"
#include "infra/platform/WindowsSupport.hpp"

#include <windows.h>

#include <utility>

namespace mydb::infra::platform {

DynamicLibrary DynamicLibrary::open(const std::filesystem::path& path) {
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR: dependências da DLL são procuradas na pasta dela, e não no
    // diretório atual (proteção contra DLL hijacking, como o HeidiSQL faz com a DWrite.dll, §1.5).
    constexpr DWORD kFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    const std::filesystem::path absolute = std::filesystem::absolute(path);
    HMODULE handle = ::LoadLibraryExW(absolute.c_str(), nullptr, kFlags);
    if (handle == nullptr) {
        const DWORD error = ::GetLastError();
        throw DynamicLibraryError("Falha ao carregar " + windows::narrow(absolute.native()) + ": " +
                                  windows::errorMessage(error));
    }
    return {static_cast<void*>(handle), path};
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
    const FARPROC address = ::GetProcAddress(static_cast<HMODULE>(m_handle), name);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): FARPROC e Symbol são ponteiros de função
    return reinterpret_cast<Symbol>(address);
}

void DynamicLibrary::close() noexcept {
    if (m_handle != nullptr) {
        ::FreeLibrary(static_cast<HMODULE>(m_handle));
        m_handle = nullptr;
    }
}

} // namespace mydb::infra::platform
