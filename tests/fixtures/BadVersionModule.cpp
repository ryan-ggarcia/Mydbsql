// Módulo de teste compilado "para uma API futura": o ModuleHost deve recusá-lo com uma mensagem clara.

#include "sdk/IModule.hpp"

#include <string_view>

namespace {

class BadVersionModule final : public mydb::sdk::IModule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "test.bad_version"; }
    [[nodiscard]] mydb::sdk::Version apiVersion() const noexcept override { return {.major = 99, .minor = 0}; }
    void registerInto(mydb::sdk::ModuleContext& /*context*/) override {}
};

} // namespace

MYDB_DYNAMIC_MODULE(BadVersionModule)
