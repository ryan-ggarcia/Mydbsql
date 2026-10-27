#pragma once

#include <QStringList>

#include <string_view>

namespace mydb::core {
class ISettingsStore;
} // namespace mydb::core

namespace mydb::ui {

/// Preferências do editor de query, no documento "query-editor" do ISettingsStore: histórico de localizar e
/// substituir e a retenção do histórico de queries.
struct EditorPreferences {
    static constexpr std::string_view kDocument = "query-editor";
    static constexpr int kDefaultKeepDays = 30;
    static constexpr qsizetype kMaxHistory = 20;

    QStringList findHistory;
    QStringList replaceHistory;
    int historyKeepDays{kDefaultKeepDays};

    /// Um documento ausente ou ilegível dá os valores padrão.
    [[nodiscard]] static EditorPreferences load(core::ISettingsStore& settings);
    /// Lança o erro do ISettingsStore se não conseguir gravar.
    void save(core::ISettingsStore& settings) const;
};

/// Põe `text` no começo de `history`, sem repetir, com no máximo kMaxHistory itens.
void rememberText(QStringList& history, const QString& text);

} // namespace mydb::ui
