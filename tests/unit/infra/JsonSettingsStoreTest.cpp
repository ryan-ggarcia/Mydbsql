#include "infra/settings/JsonSettingsStore.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace mydb::infra::settings {
namespace {

class JsonSettingsStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_directory = std::filesystem::temp_directory_path() /
                      ("mydbsql-settings-test-" + std::to_string(std::random_device{}()));
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    std::filesystem::path m_directory;
};

TEST_F(JsonSettingsStoreTest, MissingDocumentIsEmpty) {
    JsonSettingsStore store(m_directory);
    EXPECT_FALSE(store.read("sessions").has_value());
    EXPECT_FALSE(std::filesystem::exists(m_directory)); // ler não cria nada
}

TEST_F(JsonSettingsStoreTest, WritesAtomicallyAndReadsBack) {
    JsonSettingsStore store(m_directory);
    store.write("sessions", R"({"a": "ação"})");
    store.write("sessions", R"({"b": 2})");
    EXPECT_EQ(store.read("sessions"), R"({"b": 2})");
    EXPECT_TRUE(std::filesystem::exists(m_directory / "sessions.json"));
    EXPECT_FALSE(std::filesystem::exists(m_directory / "sessions.json.tmp"));
}

TEST_F(JsonSettingsStoreTest, RejectsDocumentNamesThatEscapeTheDirectory) {
    JsonSettingsStore store(m_directory);
    EXPECT_THROW(store.write("../fora", "{}"), std::invalid_argument);
    EXPECT_THROW(store.write("sub/doc", "{}"), std::invalid_argument);
    EXPECT_THROW((void)store.read(""), std::invalid_argument);
    EXPECT_THROW((void)store.read(".oculto"), std::invalid_argument);
}

} // namespace
} // namespace mydb::infra::settings
