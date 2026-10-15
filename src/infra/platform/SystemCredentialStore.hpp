#pragma once

#include "core/interfaces/ICredentialStore.hpp"

#include <memory>

namespace mydb::infra::platform {

/// Cofre de senhas do SO: Windows Credential Manager, Keychain do macOS ou libsecret no Linux (ADR-0009).
/// nullptr quando o SO não oferece um cofre utilizável (ex.: Linux sem secret service); quem chama usa
/// então um cofre só em memória e pede a senha a cada conexão (CLAUDE.md §2.6, regra 5).
[[nodiscard]] std::unique_ptr<core::ICredentialStore> createSystemCredentialStore();

} // namespace mydb::infra::platform
