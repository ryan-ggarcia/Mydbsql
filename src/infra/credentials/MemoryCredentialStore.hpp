#pragma once

#include "core/interfaces/ICredentialStore.hpp"

#include <map>
#include <mutex>
#include <string>

namespace mydb::infra::credentials {

/// Segredos só na memória do processo. Usado nos testes e como alternativa quando o SO não tem cofre
/// (CLAUDE.md §2.6, regra 5): nada vai para o disco, e as senhas somem quando o app fecha.
class MemoryCredentialStore final : public core::ICredentialStore {
public:
    [[nodiscard]] std::optional<std::string> read(std::string_view key) override {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_secrets.find(key);
        return found == m_secrets.end() ? std::nullopt : std::optional(found->second);
    }

    void write(std::string_view key, std::string_view secret) override {
        const std::scoped_lock lock(m_mutex);
        m_secrets.insert_or_assign(std::string(key), std::string(secret));
    }

    void erase(std::string_view key) override {
        const std::scoped_lock lock(m_mutex);
        if (const auto found = m_secrets.find(key); found != m_secrets.end()) {
            m_secrets.erase(found);
        }
    }

    [[nodiscard]] bool persistent() const noexcept override { return false; }

private:
    std::mutex m_mutex;
    std::map<std::string, std::string, std::less<>> m_secrets;
};

} // namespace mydb::infra::credentials
