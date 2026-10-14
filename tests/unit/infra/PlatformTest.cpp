#include "infra/platform/IPlatform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mydb::infra::platform {
namespace {

TEST(PlatformTest, NameAndSuffixAreKnownValues) {
    const auto platform = createPlatform();
    const std::string_view name = platform->name();
    const std::string_view suffix = platform->sharedLibrarySuffix();

    EXPECT_TRUE(name == "linux" || name == "windows" || name == "macos") << name;
    EXPECT_TRUE((name == "linux" && suffix == ".so") || (name == "windows" && suffix == ".dll") ||
                (name == "macos" && suffix == ".dylib"))
        << name << " " << suffix;
}

TEST(PlatformTest, StandardDirectoriesAreAbsoluteAndAppSpecific) {
    const auto platform = createPlatform();
    for (const auto which : {StandardDirectory::Config, StandardDirectory::Cache, StandardDirectory::Data}) {
        const std::filesystem::path path = platform->standardDirectory(which);
        EXPECT_TRUE(path.is_absolute()) << path;
        // A subpasta do app aparece no caminho, seja no final (config) ou antes de "cache".
        const std::string text = path.generic_string();
        EXPECT_TRUE(text.find("mydbsql") != std::string::npos || text.find("Mydbsql") != std::string::npos) << path;
    }
}

TEST(PlatformTest, ExecutablePathPointsToThisTestBinary) {
    const auto platform = createPlatform();
    const std::filesystem::path exe = platform->executablePath();

    EXPECT_TRUE(exe.is_absolute()) << exe;
    EXPECT_TRUE(std::filesystem::is_regular_file(exe)) << exe;
    EXPECT_NE(exe.stem().string().find("mydb_tests"), std::string::npos) << exe;
    EXPECT_TRUE(platform->pluginDirectory().is_absolute());
}

TEST(PlatformTest, ProcessMemoryIsPlausible) {
    const auto platform = createPlatform();
    const ProcessMemory memory = platform->processMemory();

    constexpr std::uint64_t kOneGiBInKb = 1024ULL * 1024ULL;
    EXPECT_GT(memory.residentKb, 0U);
    EXPECT_GT(memory.privateKb, 0U);
    EXPECT_LT(memory.residentKb, kOneGiBInKb); // um executável de teste não chega perto disso
}

TEST(PlatformTest, LoadedLibrariesAreUniqueAbsolutePaths) {
    const auto platform = createPlatform();
    const auto libraries = platform->loadedLibraries();

    ASSERT_FALSE(libraries.empty());
    for (const auto& library : libraries) {
        EXPECT_TRUE(library.is_absolute()) << library;
    }
    auto sorted = libraries;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end());
}

TEST(PlatformTest, OpenUrlRejectsNonWebSchemesWithoutLaunchingAnything) {
    const auto platform = createPlatform();
    EXPECT_THROW(platform->openUrl("file:///etc/passwd"), std::invalid_argument);
    EXPECT_THROW(platform->openUrl("calc.exe"), std::invalid_argument);
    EXPECT_THROW(platform->openUrl(""), std::invalid_argument);
}

} // namespace
} // namespace mydb::infra::platform
