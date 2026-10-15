#pragma once

// Cofre de senhas só em memória, para testes (o real, sobre o keychain do SO, chega na Fase 3).

#include "core/interfaces/ICredentialStore.hpp"

#include <map>
#include <string>

namespace mydb::tests {

class MemoryCredentialStore final : public core::ICredentialStore {
public:
    [[nodiscard]] std::optional<std::string> read(std::string_view key) override {
        const auto found = m_secrets.find(key);
        return found == m_secrets.end() ? std::nullopt : std::optional(found->second);
    }
    void write(std::string_view key, std::string_view secret) override { m_secrets[std::string(key)] = secret; }
    void erase(std::string_view key) override {
        if (const auto found = m_secrets.find(key); found != m_secrets.end()) {
            m_secrets.erase(found);
        }
    }

private:
    std::map<std::string, std::string, std::less<>> m_secrets;
};

} // namespace mydb::tests
