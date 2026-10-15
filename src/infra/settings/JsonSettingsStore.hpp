#pragma once

#include "core/interfaces/ISettingsStore.hpp"

#include <filesystem>

namespace mydb::infra::settings {

/// Um arquivo `<documento>.json` por documento, na pasta de configuração do SO (IPlatform). A gravação
/// escreve num arquivo temporário e troca pelo definitivo com rename, que é atômico (§2.6, regra 8).
class JsonSettingsStore final : public core::ISettingsStore {
public:
    explicit JsonSettingsStore(std::filesystem::path directory);

    [[nodiscard]] std::optional<std::string> read(std::string_view document) override;
    void write(std::string_view document, std::string_view json) override;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_directory; }

private:
    [[nodiscard]] std::filesystem::path fileFor(std::string_view document) const;

    std::filesystem::path m_directory;
};

} // namespace mydb::infra::settings
