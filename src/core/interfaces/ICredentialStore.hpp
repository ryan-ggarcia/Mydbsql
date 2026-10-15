#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mydb::core {

/// Cofre de senhas do SO (libsecret, Keychain, Credential Manager), CLAUDE.md §0 e §2.6, regra 5; ADR-0009.
/// As implementações de cada SO ficam em infra/platform. As chaves são estáveis, ex.: "<sessionId>/password".
/// Falhas do cofre do SO são lançadas como std::runtime_error.
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    [[nodiscard]] virtual std::optional<std::string> read(std::string_view key) = 0;
    virtual void write(std::string_view key, std::string_view secret) = 0;
    virtual void erase(std::string_view key) = 0;

    /// false quando os segredos só duram até o app fechar (SO sem cofre, ex.: Linux sem secret service).
    /// Nesse caso o app avisa o usuário e pede a senha a cada conexão; nunca grava em texto puro.
    [[nodiscard]] virtual bool persistent() const noexcept = 0;

protected:
    ICredentialStore() = default;
    ICredentialStore(const ICredentialStore&) = default;
    ICredentialStore(ICredentialStore&&) = default;
    ICredentialStore& operator=(const ICredentialStore&) = default;
    ICredentialStore& operator=(ICredentialStore&&) = default;
};

} // namespace mydb::core
