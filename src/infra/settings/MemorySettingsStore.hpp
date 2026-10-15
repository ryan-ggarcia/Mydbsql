#pragma once

#include "core/interfaces/ISettingsStore.hpp"

#include <map>
#include <string>

namespace mydb::infra::settings {

/// Documentos só em memória, para testes.
class MemorySettingsStore final : public core::ISettingsStore {
public:
    [[nodiscard]] std::optional<std::string> read(std::string_view document) override {
        const auto found = m_documents.find(document);
        return found == m_documents.end() ? std::nullopt : std::optional(found->second);
    }
    void write(std::string_view document, std::string_view json) override {
        m_documents.insert_or_assign(std::string(document), std::string(json));
    }

private:
    std::map<std::string, std::string, std::less<>> m_documents;
};

} // namespace mydb::infra::settings
