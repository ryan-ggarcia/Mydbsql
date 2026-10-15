// Cofre de senhas no Linux pela libsecret (GNOME Keyring, KWallet com a ponte do secret service…).
// A biblioteca é aberta com dlopen só aqui: o Mydbsql não depende dela para iniciar, e sem ela (ou sem
// um secret service rodando) createSystemCredentialStore() devolve nullptr.

#include "infra/platform/DynamicLibrary.hpp"
#include "infra/platform/SystemCredentialStore.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace mydb::infra::platform {

namespace {

// Tipos mínimos da GLib e da libsecret (ABI estável e documentada), para não exigir os headers no build.
using gboolean = int;
using gchar = char;
using gint = int;
using gpointer = void*;
struct GError {
    std::uint32_t domain;
    gint code;
    gchar* message;
};
enum SecretSchemaAttributeType : int { kSecretAttributeString = 0 };
enum SecretSchemaFlags : int { kSecretSchemaNone = 0 };
struct SecretSchemaAttribute {
    const gchar* name;
    SecretSchemaAttributeType type;
};
struct SecretSchema {
    const gchar* name;
    SecretSchemaFlags flags;
    std::array<SecretSchemaAttribute, 32> attributes; // NOLINT(readability-magic-numbers): tamanho da ABI
    gint reserved;
    std::array<gpointer, 7> reservedPointers; // NOLINT(readability-magic-numbers): tamanho da ABI
};

using StoreFn = gboolean(const SecretSchema*, const gchar*, const gchar*, const gchar*, void*, GError**, ...);
using LookupFn = gchar*(const SecretSchema*, void*, GError**, ...);
using ClearFn = gboolean(const SecretSchema*, void*, GError**, ...);
using FreeFn = void(gchar*);
using ErrorFreeFn = void(GError*);

constexpr const char* kAttribute = "key";

SecretSchema makeSchema() {
    SecretSchema schema{};
    schema.name = "org.mydbsql.Credential";
    schema.flags = kSecretSchemaNone;
    schema.attributes[0] = SecretSchemaAttribute{.name = kAttribute, .type = kSecretAttributeString};
    return schema;
}

class LinuxCredentialStore final : public core::ICredentialStore {
public:
    explicit LinuxCredentialStore(DynamicLibrary library)
        : m_library(std::move(library)),
          m_store(m_library.function<StoreFn>("secret_password_store_sync")),
          m_lookup(m_library.function<LookupFn>("secret_password_lookup_sync")),
          m_clear(m_library.function<ClearFn>("secret_password_clear_sync")),
          m_free(m_library.function<FreeFn>("secret_password_free")),
          m_errorFree(m_library.function<ErrorFreeFn>("g_error_free")),
          m_schema(makeSchema()) {}

    [[nodiscard]] bool usable() const noexcept {
        return m_store != nullptr && m_lookup != nullptr && m_clear != nullptr && m_free != nullptr &&
               m_errorFree != nullptr;
    }

    [[nodiscard]] std::optional<std::string> read(std::string_view key) override {
        const std::string k(key);
        GError* error = nullptr;
        gchar* secret = m_lookup(&m_schema, nullptr, &error, kAttribute, k.c_str(), nullptr);
        check(error, "leitura");
        if (secret == nullptr) {
            return std::nullopt;
        }
        std::string result(secret);
        m_free(secret);
        return result;
    }

    void write(std::string_view key, std::string_view secret) override {
        const std::string k(key);
        const std::string label = "Mydbsql: " + k;
        const std::string value(secret);
        GError* error = nullptr;
        m_store(&m_schema, "default", label.c_str(), value.c_str(), nullptr, &error, kAttribute, k.c_str(), nullptr);
        check(error, "gravação");
    }

    void erase(std::string_view key) override {
        const std::string k(key);
        GError* error = nullptr;
        m_clear(&m_schema, nullptr, &error, kAttribute, k.c_str(), nullptr);
        check(error, "remoção");
    }

    [[nodiscard]] bool persistent() const noexcept override { return true; }

private:
    void check(GError* error, const char* operation) const {
        if (error == nullptr) {
            return;
        }
        const std::string message = error->message != nullptr ? error->message : "erro desconhecido";
        m_errorFree(error);
        throw std::runtime_error(std::string("libsecret: ") + operation + " falhou: " + message);
    }

    DynamicLibrary m_library;
    StoreFn* m_store;
    LookupFn* m_lookup;
    ClearFn* m_clear;
    FreeFn* m_free;
    ErrorFreeFn* m_errorFree;
    SecretSchema m_schema;
};

} // namespace

std::unique_ptr<core::ICredentialStore> createSystemCredentialStore() {
    try {
        auto store = std::make_unique<LinuxCredentialStore>(DynamicLibrary::open("libsecret-1.so.0"));
        if (!store->usable()) {
            return nullptr;
        }
        // Uma consulta de teste: sem secret service (ex.: sessão sem D-Bus) ela falha aqui, e não no meio
        // de uma conexão.
        (void)store->read("mydbsql-availability-probe");
        return store;
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace mydb::infra::platform
