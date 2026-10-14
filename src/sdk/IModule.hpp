#pragma once

#include "sdk/ModuleContext.hpp"
#include "sdk/Version.hpp"

#include <string>
#include <string_view>

namespace mydb::sdk {

class IModule {
public:
    virtual ~IModule() = default;

    /// Estável e único, com pontos: "export.markdown", "driver.mysql".
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual Version apiVersion() const noexcept = 0;
    /// Só registra fábricas; nada pesado é criado aqui.
    virtual void registerInto(ModuleContext& context) = 0;

protected:
    IModule() = default;
    IModule(const IModule&) = default;
    IModule(IModule&&) = default;
    IModule& operator=(const IModule&) = default;
    IModule& operator=(IModule&&) = default;
};

/// Nome do único símbolo exportado por um módulo dinâmico.
inline constexpr std::string_view kModuleEntrySymbol = "mydb_module_entry";

/// Assinatura de mydb_module_entry. O objeto devolvido pertence ao módulo (armazenamento estático):
/// o host nunca o destrói.
using ModuleEntryFunction = IModule*();

/// Nome do arquivo (sem prefixo nem sufixo) de um módulo dinâmico: "example.hello" → "mydb_example_hello".
/// Permite pular um módulo desligado sem carregá-lo.
[[nodiscard]] std::string moduleFileStem(std::string_view moduleId);

} // namespace mydb::sdk

/// Define o ponto de entrada de um módulo dinâmico. Use uma vez, num .cpp do módulo.
/// MYDB_MODULE_EXPORT é definido pelo CMake (mydb_add_dynamic_module).
#define MYDB_DYNAMIC_MODULE(ModuleType)                                                                            \
    extern "C" MYDB_MODULE_EXPORT ::mydb::sdk::IModule* mydb_module_entry() {                                      \
        static ModuleType instance;                                                                                \
        return &instance;                                                                                          \
    }
