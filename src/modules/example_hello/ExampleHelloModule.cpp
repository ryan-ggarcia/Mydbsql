// Módulo de exemplo (Fase 1): prova que um módulo dinâmico entra no aplicativo sem mudar nada fora
// da própria pasta. Registra um driver que só sabe dizer o próprio nome.

#include "core/errors/DbError.hpp"
#include "core/interfaces/IDriver.hpp"
#include "sdk/IModule.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace {

class HelloDriver final : public mydb::core::IDriver {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "example.hello"; }
    [[nodiscard]] std::string displayName() const override { return "Hello (exemplo)"; }

    [[nodiscard]] std::unique_ptr<mydb::core::IConnection>
    createConnection(const mydb::core::ConnectionParams& /*params*/,
                     mydb::core::ICredentialStore& /*credentials*/) const override {
        throw mydb::core::ConnectionError("O driver de exemplo não conecta a bancos");
    }
};

class ExampleHelloModule final : public mydb::sdk::IModule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "example.hello"; }
    [[nodiscard]] mydb::sdk::Version apiVersion() const noexcept override { return mydb::sdk::kApiVersion; }

    void registerInto(mydb::sdk::ModuleContext& context) override {
        context.drivers().add("example.hello", "Hello (exemplo)", [] { return std::make_unique<HelloDriver>(); });
    }
};

} // namespace

MYDB_DYNAMIC_MODULE(ExampleHelloModule)
