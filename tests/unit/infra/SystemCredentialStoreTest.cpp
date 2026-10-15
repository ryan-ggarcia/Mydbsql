// Usa o cofre de verdade do SO (Credential Manager, Keychain ou libsecret). Grava uma chave de teste
// própria e a remove no fim; sem cofre disponível (ex.: Linux da CI sem secret service), é pulado.

#include "infra/platform/SystemCredentialStore.hpp"

#include <gtest/gtest.h>

#include <random>
#include <string>

namespace mydb::infra::platform {
namespace {

TEST(SystemCredentialStoreTest, WritesReadsOverwritesAndErases) {
    const auto store = createSystemCredentialStore();
    if (!store) {
        GTEST_SKIP() << "este SO não tem um cofre de senhas disponível";
    }
    EXPECT_TRUE(store->persistent());
    const std::string key = "mydbsql-test/" + std::to_string(std::random_device{}());
    try {
        EXPECT_FALSE(store->read(key).has_value());
        store->write(key, "primeira ação");
        EXPECT_EQ(store->read(key), "primeira ação");
        store->write(key, "segunda");
        EXPECT_EQ(store->read(key), "segunda");
        store->erase(key);
        EXPECT_FALSE(store->read(key).has_value());
        store->erase(key); // apagar o que não existe não é erro
    } catch (...) {
        store->erase(key);
        throw;
    }
}

} // namespace
} // namespace mydb::infra::platform
