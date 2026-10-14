#include "infra/platform/DynamicLibrary.hpp"

#include "infra/platform/IPlatform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace mydb::infra::platform {
namespace {

const std::filesystem::path kPluginPath{MYDB_TEST_PLUGIN_PATH};

bool isLoaded(const IPlatform& platform, const std::filesystem::path& library) {
    const auto libraries = platform.loadedLibraries();
    return std::any_of(libraries.begin(), libraries.end(), [&](const std::filesystem::path& loaded) {
        std::error_code error;
        return std::filesystem::equivalent(loaded, library, error);
    });
}

TEST(DynamicLibraryTest, LoadsResolvesAndUnloadsOnDemand) {
    const auto platform = createPlatform();
    ASSERT_TRUE(std::filesystem::exists(kPluginPath)) << kPluginPath;

    // O mesmo contrato que os drivers terão (§2.5): nada mapeado antes do primeiro uso.
    EXPECT_FALSE(isLoaded(*platform, kPluginPath));
    {
        const DynamicLibrary library = DynamicLibrary::open(kPluginPath);
        EXPECT_TRUE(library.isOpen());
        EXPECT_TRUE(isLoaded(*platform, kPluginPath));

        const auto answer = library.function<int()>("mydb_test_answer");
        ASSERT_NE(answer, nullptr);
        EXPECT_EQ(answer(), 42);
        EXPECT_EQ(library.symbol("simbolo_que_nao_existe"), nullptr);
    }
    EXPECT_FALSE(isLoaded(*platform, kPluginPath));
}

TEST(DynamicLibraryTest, MissingLibraryThrowsWithPathInMessage) {
    const std::filesystem::path missing = kPluginPath.parent_path() / "nao_existe_mydb_plugin.bin";
    try {
        (void)DynamicLibrary::open(missing);
        FAIL() << "deveria lançar DynamicLibraryError";
    } catch (const DynamicLibraryError& error) {
        EXPECT_NE(std::string(error.what()).find("nao_existe_mydb_plugin"), std::string::npos) << error.what();
    }
}

TEST(DynamicLibraryTest, MoveTransfersOwnership) {
    DynamicLibrary first = DynamicLibrary::open(kPluginPath);
    DynamicLibrary second = std::move(first);

    EXPECT_FALSE(first.isOpen()); // NOLINT(bugprone-use-after-move): o estado após o move é parte do contrato
    EXPECT_TRUE(second.isOpen());
    EXPECT_NE(second.symbol("mydb_test_answer"), nullptr);

    second.close();
    EXPECT_FALSE(second.isOpen());
    EXPECT_EQ(second.symbol("mydb_test_answer"), nullptr);
}

} // namespace
} // namespace mydb::infra::platform
