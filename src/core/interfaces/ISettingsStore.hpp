#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mydb::core {

/// Persistência de documentos de configuração em JSON (CLAUDE.md §3). Cada módulo ou service guarda os
/// seus próprios documentos, tipados e versionados por ele: aqui só há texto. Nunca guarda senhas
/// (essas vão para o ICredentialStore).
class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    /// Conteúdo do documento, ou nullopt se ele ainda não existe.
    [[nodiscard]] virtual std::optional<std::string> read(std::string_view document) = 0;

    /// Grava de forma atômica: quem ler depois vê o conteúdo antigo inteiro ou o novo inteiro (§2.6, regra 8).
    virtual void write(std::string_view document, std::string_view json) = 0;

protected:
    ISettingsStore() = default;
    ISettingsStore(const ISettingsStore&) = default;
    ISettingsStore(ISettingsStore&&) = default;
    ISettingsStore& operator=(const ISettingsStore&) = default;
    ISettingsStore& operator=(ISettingsStore&&) = default;
};

} // namespace mydb::core
