#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mydb::core {

/// Cofre de senhas do SO (libsecret, Keychain, Credential Manager), CLAUDE.md §0 e §2.6, regra 5.
/// Implementado em infra/credentials (Fase 3). As chaves são estáveis, ex.: "<sessionId>/password".
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    [[nodiscard]] virtual std::optional<std::string> read(std::string_view key) = 0;
    virtual void write(std::string_view key, std::string_view secret) = 0;
    virtual void erase(std::string_view key) = 0;

protected:
    ICredentialStore() = default;
    ICredentialStore(const ICredentialStore&) = default;
    ICredentialStore(ICredentialStore&&) = default;
    ICredentialStore& operator=(const ICredentialStore&) = default;
    ICredentialStore& operator=(ICredentialStore&&) = default;
};

} // namespace mydb::core
