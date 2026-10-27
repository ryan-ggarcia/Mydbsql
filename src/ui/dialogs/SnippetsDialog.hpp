#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPlainTextEdit;

namespace mydb::services::query {
class SnippetStore;
} // namespace mydb::services::query

namespace mydb::ui {

/// Snippets do usuário: criar, editar, excluir e inserir no editor. Não modal; criado sob demanda e destruído
/// ao fechar.
class SnippetsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SnippetsDialog(services::query::SnippetStore& snippets, QWidget* parent = nullptr);

    /// Grava o snippet do formulário (substitui o de mesmo nome).
    bool saveCurrent(const QString& name, const QString& sql);
    void removeCurrent();
    [[nodiscard]] QListWidget& list() const noexcept { return *m_list; }

Q_SIGNALS:
    void sqlChosen(const QString& sql);

private:
    void reload(const QString& select = {});
    void showSelected();

    services::query::SnippetStore* m_snippets;
    QListWidget* m_list;
    QLineEdit* m_name;
    QPlainTextEdit* m_sql;
};

} // namespace mydb::ui
