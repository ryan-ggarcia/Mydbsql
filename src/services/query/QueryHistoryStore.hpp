#pragma once

#include "core/interfaces/ISettingsStore.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::services::query {

struct HistoryEntry {
    std::chrono::system_clock::time_point time;
    std::string sessionId;
    std::string database;
    std::string sql;
    std::chrono::milliseconds duration{0};
    bool failed{false};

    friend bool operator==(const HistoryEntry&, const HistoryEntry&) = default;
};

/// Histórico de queries persistente (CLAUDE.md §1.3, encontrado no binário do HeidiSQL), no documento
/// "query-history" do ISettingsStore. Guarda no máximo `maxEntries` e descarta o que passou de `keepDays`.
/// Não é thread-safe: usado pela thread da UI. Nunca guarda senhas (o texto é o SQL digitado).
class QueryHistoryStore {
public:
    static constexpr int kSchemaVersion = 1;
    static constexpr std::string_view kDocument = "query-history";

    explicit QueryHistoryStore(core::ISettingsStore& settings, int keepDays = 30, std::size_t maxEntries = 1000);

    /// Lança session::SettingsError se o documento estiver corrompido ou vier de uma versão mais nova. Depois
    /// dessa falha o store fica em memória e não grava nada (o arquivo não é sobrescrito) até um clear().
    void load();

    /// Acrescenta e grava (com escrita atômica), salvo depois de um load() com falha. Remove os vencidos e os
    /// excedentes.
    void add(HistoryEntry entry);

    /// Da mais nova para a mais antiga. `text` vazio devolve tudo; senão, as que contêm `text` (sem
    /// diferenciar maiúsculas de minúsculas), filtradas por sessão se `sessionId` não for vazio.
    [[nodiscard]] std::vector<HistoryEntry> search(std::string_view text, std::string_view sessionId = {}) const;

    /// Esvazia e grava, inclusive por cima de um arquivo que load() não conseguiu ler.
    void clear();
    void setKeepDays(int days);

private:
    void prune();
    void save() const;

    core::ISettingsStore* m_settings;
    int m_keepDays;
    std::size_t m_maxEntries;
    std::vector<HistoryEntry> m_entries; ///< da mais antiga para a mais nova
    bool m_writable{true};               ///< false depois de um load() com falha, até um clear()
};

} // namespace mydb::services::query
