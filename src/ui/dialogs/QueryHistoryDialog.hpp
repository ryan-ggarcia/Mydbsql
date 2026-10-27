#pragma once

#include <QDialog>

#include <functional>
#include <string>

class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;

namespace mydb::core {
class ISettingsStore;
} // namespace mydb::core

namespace mydb::services::query {
class QueryHistoryStore;
} // namespace mydb::services::query

namespace mydb::ui {

/// Histórico de queries: busca, retenção em dias, limpar e usar uma query no editor. Não modal; criado sob
/// demanda e destruído ao fechar.
class QueryHistoryDialog final : public QDialog {
    Q_OBJECT

public:
    using SessionName = std::function<QString(const std::string& sessionId)>;

    QueryHistoryDialog(services::query::QueryHistoryStore& history, core::ISettingsStore& settings,
                       SessionName sessionName, QWidget* parent = nullptr);

    void setSearchText(const QString& text);
    void setKeepDays(int days);
    /// Apaga tudo, sem perguntar (o botão pergunta antes).
    void clearHistory();
    [[nodiscard]] QTreeWidget& list() const noexcept { return *m_list; }

Q_SIGNALS:
    void sqlChosen(const QString& sql);

private:
    void reload();
    void chooseCurrent();

    services::query::QueryHistoryStore* m_history;
    core::ISettingsStore* m_settings;
    SessionName m_sessionName;
    QLineEdit* m_search;
    QTreeWidget* m_list;
    QSpinBox* m_keepDays;
    QLabel* m_status;
};

} // namespace mydb::ui
