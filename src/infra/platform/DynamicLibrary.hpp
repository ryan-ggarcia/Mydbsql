#pragma once

#include <filesystem>
#include <stdexcept>

namespace mydb::infra::platform {

class DynamicLibraryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Biblioteca dinâmica (.so, .dll, .dylib) com posse RAII: fecha no destrutor.
/// Base dos drivers e módulos carregados sob demanda (CLAUDE.md §2.2, regra 2).
class DynamicLibrary final {
public:
    /// Ponteiro de função genérico; converta com function<T>().
    using Symbol = void (*)();

    /// Lança DynamicLibraryError com a mensagem do SO se a biblioteca não carregar.
    [[nodiscard]] static DynamicLibrary open(const std::filesystem::path& path);

    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    ~DynamicLibrary();

    /// nullptr se o símbolo não existir.
    [[nodiscard]] Symbol symbol(const char* name) const noexcept;

    /// Converte o símbolo para o tipo de função informado, ex.: function<int()>("answer").
    template <typename Function>
    [[nodiscard]] Function* function(const char* name) const noexcept {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): é o contrato de dlsym/GetProcAddress
        return reinterpret_cast<Function*>(symbol(name));
    }

    [[nodiscard]] bool isOpen() const noexcept { return m_handle != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    /// Descarrega a biblioteca. Ponteiros obtidos dela deixam de valer.
    void close() noexcept;

private:
    DynamicLibrary(void* handle, std::filesystem::path path) noexcept;

    void* m_handle{nullptr};
    std::filesystem::path m_path;
};

} // namespace mydb::infra::platform
