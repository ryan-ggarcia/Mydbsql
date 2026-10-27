#pragma once

#include <QDialog>
#include <QTextDocument>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;

namespace mydb::core {
class ISettingsStore;
} // namespace mydb::core

namespace mydb::ui {

class SqlEditor;

/// Localizar e substituir no editor da aba de query em foco, com histórico dos textos procurados e
/// substituídos (EditorPreferences). Não modal; criado sob demanda e destruído ao fechar.
class FindReplaceDialog final : public QDialog {
    Q_OBJECT

public:
    /// O editor em foco agora, ou nullptr.
    using EditorProvider = std::function<SqlEditor*()>;

    FindReplaceDialog(EditorProvider editor, core::ISettingsStore& settings, QWidget* parent = nullptr);

    void setFindText(const QString& text);
    void setReplaceText(const QString& text);
    /// Seleciona a próxima ocorrência (volta ao começo no fim do texto).
    bool findNext();
    /// Substitui a ocorrência selecionada e vai para a próxima.
    bool replaceNext();
    int replaceAll();

private:
    [[nodiscard]] QTextDocument::FindFlags flags() const;
    void remember(bool replacing);

    EditorProvider m_editor;
    core::ISettingsStore* m_settings;
    QComboBox* m_find;
    QComboBox* m_replace;
    QCheckBox* m_caseSensitive;
    QCheckBox* m_wholeWords;
    QCheckBox* m_backward;
    QLabel* m_status;
};

} // namespace mydb::ui
