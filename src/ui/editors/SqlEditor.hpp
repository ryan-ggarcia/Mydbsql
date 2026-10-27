#pragma once

#include "core/model/ObjectRef.hpp"
#include "core/sql/SqlSyntax.hpp"

#include <QPlainTextEdit>
#include <QTextDocument>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QCompleter;
class QStandardItemModel;

namespace mydb::services::query {
class CompletionIndex;
} // namespace mydb::services::query

namespace mydb::ui {

/// Editor SQL próprio (ADR-0013): realce por bloco sobre o SqlTokenizer do dialeto, números de linha, dobra
/// por indentação, comentar e mover linhas, quebra de linha e autocomplete pelo CompletionIndex.
class SqlEditor final : public QPlainTextEdit {
    Q_OBJECT

public:
    using MissingTablesHandler = std::function<void(std::vector<core::model::ObjectRef>)>;

    /// O texto em volta do cursor que vai para o autocomplete (em UTF-8) e a posição do cursor nele, em bytes.
    struct CompletionWindow {
        std::string text;
        std::size_t cursor{0};
    };

    explicit SqlEditor(QWidget* parent = nullptr);
    SqlEditor(const SqlEditor&) = delete;
    SqlEditor& operator=(const SqlEditor&) = delete;
    SqlEditor(SqlEditor&&) = delete;
    SqlEditor& operator=(SqlEditor&&) = delete;
    ~SqlEditor() override;

    /// Regras léxicas e palavras-chave do dialeto da sessão; refaz o realce.
    void setSyntax(core::sql::SqlSyntax syntax, const std::vector<std::string>& keywords);
    [[nodiscard]] const core::sql::SqlSyntax& syntax() const noexcept { return m_syntax; }

    /// O índice é só lido daqui (consultas const são thread-safe); nulo desliga o autocomplete.
    void setCompletionIndex(std::shared_ptr<const services::query::CompletionIndex> index);
    /// Chamado quando o autocomplete cita tabelas cujas colunas ainda não estão no índice.
    void setMissingTablesHandler(MissingTablesHandler handler) { m_missingTables = std::move(handler); }

    void toggleComment();
    /// Move as linhas do cursor (ou da seleção) uma linha para cima (-1) ou para baixo (+1).
    void moveLines(int direction);
    void setWordWrap(bool wrap);
    [[nodiscard]] bool wordWrap() const;

    [[nodiscard]] static bool isFoldable(const QTextBlock& block);
    [[nodiscard]] static bool isFolded(const QTextBlock& block);
    void toggleFold(const QTextBlock& block);

    int replaceAll(const QString& text, const QString& replacement, QTextDocument::FindFlags flags);

    /// Mostra o popup de autocomplete no cursor (Ctrl+Espaço, ou sozinho ao digitar).
    void showCompletion();
    [[nodiscard]] QCompleter* completer() const noexcept { return m_completer; }
    [[nodiscard]] CompletionWindow completionWindow() const;

    /// O texto selecionado, com quebras de linha normais (o Qt usa U+2029 entre parágrafos).
    [[nodiscard]] QString selectedSql() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    class Gutter;
    class Highlighter;

    [[nodiscard]] int gutterWidth() const;
    void paintGutter(const QRect& area);
    void gutterClicked(const QPoint& position);
    void updateGutterWidth();
    void insertCompletion(const QString& text);
    [[nodiscard]] int prefixStart() const;
    void autoComplete(const QString& typed);

    core::sql::SqlSyntax m_syntax;
    Highlighter* m_highlighter;
    Gutter* m_gutter;
    QCompleter* m_completer;
    QStandardItemModel* m_completionModel;
    std::shared_ptr<const services::query::CompletionIndex> m_index;
    MissingTablesHandler m_missingTables;
};

} // namespace mydb::ui
