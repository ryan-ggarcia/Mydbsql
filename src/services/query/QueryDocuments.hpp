#pragma once

#include "core/interfaces/ISettingsStore.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mydb::services::query {

struct Snippet {
    std::string name;
    std::string sql;

    friend bool operator==(const Snippet&, const Snippet&) = default;
};

/// Snippets do usuário, no documento "snippets" do ISettingsStore. Lança session::SettingsError ao ler um
/// documento corrompido ou de versão mais nova. Não é thread-safe.
class SnippetStore {
public:
    static constexpr std::string_view kDocument = "snippets";

    explicit SnippetStore(core::ISettingsStore& settings) noexcept : m_settings(&settings) {}

    void load();
    [[nodiscard]] const std::vector<Snippet>& snippets() const noexcept { return m_snippets; }
    /// Substitui o de mesmo nome ou acrescenta; grava na hora.
    void put(Snippet snippet);
    void remove(std::string_view name);

private:
    void save() const;

    core::ISettingsStore* m_settings;
    std::vector<Snippet> m_snippets;
};

/// Uma aba de query aberta, para restaurar ao reabrir o app.
struct QueryTabState {
    std::string title;
    std::string sessionId; ///< sessão em que a aba foi usada pela última vez; pode não estar aberta
    std::string sql;
    std::string file; ///< arquivo .sql associado (UTF-8), vazio se nunca foi salvo

    friend bool operator==(const QueryTabState&, const QueryTabState&) = default;
};

/// Abas de query abertas, no documento "query-tabs" do ISettingsStore. Lança session::SettingsError ao ler um
/// documento corrompido ou de versão mais nova. Não é thread-safe.
class QueryTabsStore {
public:
    static constexpr std::string_view kDocument = "query-tabs";

    explicit QueryTabsStore(core::ISettingsStore& settings) noexcept : m_settings(&settings) {}

    [[nodiscard]] std::vector<QueryTabState> load() const;
    void save(const std::vector<QueryTabState>& tabs) const;

private:
    core::ISettingsStore* m_settings;
};

} // namespace mydb::services::query
