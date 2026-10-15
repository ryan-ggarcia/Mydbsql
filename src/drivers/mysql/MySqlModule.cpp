// Ponto de entrada do plugin mydb_driver_mysql: a libmariadb só entra na memória quando este módulo é
// carregado, na primeira conexão MySQL/MariaDB (CLAUDE.md §2.2, regra 2).

#include "drivers/mysql/MySqlDriver.hpp"
#include "sdk/IModule.hpp"

#include <memory>
#include <utility>

namespace {

class MySqlModule final : public mydb::sdk::IModule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "driver.mysql"; }
    [[nodiscard]] mydb::sdk::Version apiVersion() const noexcept override { return mydb::sdk::kApiVersion; }

    void registerInto(mydb::sdk::ModuleContext& context) override {
        // Os plugins de autenticação da libmariadb (caching_sha2_password…) vão ao lado do módulo.
        const std::filesystem::path plugins = context.currentModuleFile().parent_path() / "mariadb-plugins";
        // NOLINTNEXTLINE(bugprone-exception-escape): a fábrica pode lançar bad_alloc; o Registry propaga
        auto factory = [plugins] {
            return std::make_unique<mydb::drivers::mysql::MySqlDriver>(plugins);
        };
        context.drivers().add(std::string(mydb::drivers::mysql::MySqlDriver::kId), "MySQL / MariaDB",
                              std::move(factory));
    }
};

} // namespace

MYDB_DYNAMIC_MODULE(MySqlModule)
