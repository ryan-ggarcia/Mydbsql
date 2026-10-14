#include "app/ModuleHost.hpp"

#include "infra/platform/IPlatform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace mydb::app {
namespace {

const std::filesystem::path kExampleModule{MYDB_EXAMPLE_MODULE_PATH};
const std::filesystem::path kBadVersionModule{MYDB_BAD_VERSION_MODULE_PATH};
const std::filesystem::path kNotAModule{MYDB_TEST_PLUGIN_PATH};

class StaticModule final : public sdk::IModule {
public:
    explicit StaticModule(std::string id) : m_id(std::move(id)) {}
    [[nodiscard]] std::string_view id() const noexcept override { return m_id; }
    [[nodiscard]] sdk::Version apiVersion() const noexcept override { return sdk::kApiVersion; }
    void registerInto(sdk::ModuleContext& /*context*/) override { ++registrations; }

    int registrations{0};

private:
    std::string m_id;
};

class FailingModule final : public sdk::IModule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "test.failing"; }
    [[nodiscard]] sdk::Version apiVersion() const noexcept override { return sdk::kApiVersion; }
    void registerInto(sdk::ModuleContext& /*context*/) override { throw std::runtime_error("sem recursos"); }
};

bool isLoaded(const infra::platform::IPlatform& platform, const std::filesystem::path& library) {
    const auto libraries = platform.loadedLibraries();
    return std::ranges::any_of(libraries, [&](const std::filesystem::path& loaded) {
        std::error_code error;
        return std::filesystem::equivalent(loaded, library, error);
    });
}

std::string loadError(ModuleHost& host, const std::filesystem::path& file) {
    try {
        (void)host.loadDynamic(file);
    } catch (const ModuleLoadError& error) {
        return error.what();
    }
    return {};
}

TEST(ModuleHostTest, BuiltinModulesRegisterOnceAndCanBeDisabled) {
    const auto platform = infra::platform::createPlatform();
    ModuleHost host(*platform);
    StaticModule enabled("test.enabled");
    StaticModule disabled("test.disabled");
    host.setDisabled({"test.disabled"});

    EXPECT_TRUE(host.addBuiltin(enabled));
    EXPECT_FALSE(host.addBuiltin(disabled));
    EXPECT_EQ(enabled.registrations, 1);
    EXPECT_EQ(disabled.registrations, 0);
    ASSERT_EQ(host.modules().size(), 1U);
    EXPECT_EQ(host.modules()[0].id, "test.enabled");
    EXPECT_TRUE(host.modules()[0].file.empty());

    EXPECT_THROW(host.addBuiltin(enabled), ModuleLoadError);
}

TEST(ModuleHostTest, RegistrationFailureIsReportedWithTheModuleId) {
    const auto platform = infra::platform::createPlatform();
    ModuleHost host(*platform);
    FailingModule failing;
    try {
        host.addBuiltin(failing);
        FAIL() << "deveria lançar ModuleLoadError";
    } catch (const ModuleLoadError& error) {
        EXPECT_NE(std::string(error.what()).find("test.failing"), std::string::npos) << error.what();
        EXPECT_NE(std::string(error.what()).find("sem recursos"), std::string::npos) << error.what();
    }
    EXPECT_TRUE(host.modules().empty());
}

TEST(ModuleHostTest, DynamicExampleModuleRegistersItsDriver) {
    const auto platform = infra::platform::createPlatform();
    ModuleHost host(*platform);
    ASSERT_TRUE(std::filesystem::exists(kExampleModule)) << kExampleModule;

    EXPECT_FALSE(isLoaded(*platform, kExampleModule));
    ASSERT_TRUE(host.loadDynamic(kExampleModule));
    EXPECT_TRUE(isLoaded(*platform, kExampleModule));

    ASSERT_EQ(host.modules().size(), 1U);
    EXPECT_EQ(host.modules()[0].id, "example.hello");
    EXPECT_EQ(host.modules()[0].apiVersion, sdk::kApiVersion);

    const auto* entry = host.drivers().find("example.hello");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->displayName, "Hello (exemplo)");
    const std::unique_ptr<core::IDriver> driver = host.drivers().create("example.hello");
    EXPECT_EQ(driver->id(), "example.hello");
    EXPECT_EQ(driver->displayName(), "Hello (exemplo)");

    EXPECT_NE(loadError(host, kExampleModule).find("duas vezes"), std::string::npos);
}

TEST(ModuleHostTest, DisabledDynamicModuleIsNeverOpened) {
    const auto platform = infra::platform::createPlatform();
    ModuleHost host(*platform);
    host.setDisabled({"example.hello"});
    EXPECT_FALSE(host.loadDynamic(kExampleModule));
    EXPECT_FALSE(isLoaded(*platform, kExampleModule));
    EXPECT_TRUE(host.modules().empty());
    EXPECT_EQ(host.drivers().size(), 0U);
}

TEST(ModuleHostTest, IncompatibleModulesAreRejectedWithClearMessages) {
    const auto platform = infra::platform::createPlatform();
    ModuleHost host(*platform);

    const std::string badVersion = loadError(host, kBadVersionModule);
    EXPECT_NE(badVersion.find("API 99.0"), std::string::npos) << badVersion;
    EXPECT_NE(badVersion.find(sdk::kApiVersion.toString()), std::string::npos) << badVersion;

    const std::string notAModule = loadError(host, kNotAModule);
    EXPECT_NE(notAModule.find("não é um módulo"), std::string::npos) << notAModule;

    const std::string missing = loadError(host, kExampleModule.parent_path() / "mydb_nao_existe.bin");
    EXPECT_NE(missing.find("mydb_nao_existe"), std::string::npos) << missing;

    EXPECT_TRUE(host.modules().empty());
}

TEST(ModuleHostTest, DiscoverListsModulesWithoutLoadingThem) {
    const auto platform = infra::platform::createPlatform();
    ModuleHost host(*platform);
    const auto files = host.discover(kExampleModule.parent_path());
    EXPECT_NE(std::ranges::find_if(files,
                                   [](const std::filesystem::path& f) { return f.stem() == kExampleModule.stem(); }),
              files.end());
    EXPECT_TRUE(std::ranges::is_sorted(files));
    EXPECT_TRUE(host.modules().empty());
    EXPECT_TRUE(host.discover(kExampleModule.parent_path() / "nao_existe").empty());
}

} // namespace
} // namespace mydb::app
