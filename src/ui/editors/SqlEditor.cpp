#include "ui/editors/SqlEditor.hpp"

#include "core/sql/SqlTokenizer.hpp"
#include "services/query/CompletionIndex.hpp"
#include "ui/common/QtText.hpp"

#include <QAbstractItemView>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QHash>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPolygonF>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>

#include <algorithm>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace mydb::ui {

namespace {

using core::sql::SqlToken;
using core::sql::SqlTokenKind;

constexpr int kTabWidth = 4;
constexpr int kMinTypedForPopup = 2;
constexpr std::size_t kMaxCompletions = 200;
// shortcut: o autocomplete olha no máximo estas linhas em volta do cursor (uma sentença maior completa sem o
// começo); trocar pelos limites de sentença do realce se isso incomodar.
constexpr int kWindowBefore = 300;
constexpr int kWindowAfter = 100;
// shortcut: uma string ou um comentário aberto colore no máximo estas linhas seguintes. Sem o limite, digitar
// uma aspa no meio de um arquivo de 50 mil linhas refaz o realce de todas elas na thread da UI (~1 s); trocar
// por realce em segundo plano se strings tão longas precisarem de cor.
constexpr int kMaxContinuationLines = 200;
constexpr int kLinesBits = 8; // cabe kMaxContinuationLines
constexpr unsigned kHashMask = 0x3fffffU;

std::string toLowerAscii(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

/// Posição em UTF-16 (QString) de cada byte de `utf8`, e do fim.
std::vector<int> utf16Positions(const QByteArray& utf8) {
    std::vector<int> positions(static_cast<std::size_t>(utf8.size()) + 1);
    int unit = 0;
    qsizetype i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        int length = 4;
        if (lead < 0x80) {
            length = 1;
        } else if (lead < 0xE0) {
            length = 2;
        } else if (lead < 0xF0) {
            length = 3;
        }
        for (int k = 0; k < length && i + k < utf8.size(); ++k) {
            positions[static_cast<std::size_t>(i + k)] = unit;
        }
        unit += length == 4 ? 2 : 1;
        i += length;
    }
    positions.back() = unit;
    return positions;
}

/// O começo de um token que continua na próxima linha (comentário, string ou identificador sem fechamento):
/// a próxima linha é analisada com ele na frente, e o tokenizer continua de onde parou.
std::string openerOf(std::string_view token, SqlTokenKind kind, bool nestedComments) {
    switch (kind) {
    case SqlTokenKind::BlockComment: {
        int depth = 1;
        if (nestedComments) {
            depth = 0;
            for (std::size_t i = 0; i + 1 < token.size(); ++i) {
                if (token[i] == '/' && token[i + 1] == '*') {
                    ++depth;
                    ++i;
                } else if (token[i] == '*' && token[i + 1] == '/') {
                    --depth;
                    ++i;
                }
            }
        }
        std::string result;
        for (int i = 0; i < std::max(depth, 1); ++i) {
            result += "/*";
        }
        return result;
    }
    case SqlTokenKind::String:
        if (token.starts_with('$')) {
            const std::size_t close = token.find('$', 1);
            return std::string(token.substr(0, close == std::string_view::npos ? 1 : close + 1));
        }
        if (token.size() > 1 && (token[0] == 'E' || token[0] == 'e')) {
            return std::string(token.substr(0, 2));
        }
        return std::string(token.substr(0, 1));
    case SqlTokenKind::QuotedIdentifier:
        return std::string(token.substr(0, 1));
    default:
        return {};
    }
}

/// Largura da indentação em colunas; -1 numa linha em branco.
int indentOf(const QString& text) {
    int width = 0;
    for (const QChar c : text) {
        if (c == QLatin1Char(' ')) {
            ++width;
        } else if (c == QLatin1Char('\t')) {
            width += kTabWidth;
        } else {
            return width;
        }
    }
    return -1;
}

class OpenerData final : public QTextBlockUserData {
public:
    OpenerData(std::string token, int crossed) : opener(std::move(token)), lines(crossed) {}
    std::string opener; // NOLINT(misc-non-private-member-variables-in-classes)
    int lines;          // NOLINT(misc-non-private-member-variables-in-classes): linhas que o token já atravessou
};

QTextCharFormat makeFormat(const QColor& color, bool bold = false, bool italic = false) {
    QTextCharFormat format;
    format.setForeground(color);
    if (bold) {
        format.setFontWeight(QFont::Bold);
    }
    format.setFontItalic(italic);
    return format;
}

} // namespace

/// Realce por bloco (linha). O estado do bloco diz se ele termina dentro de um comentário, string ou
/// identificador; o texto que abriu esse token fica no QTextBlockUserData.
class SqlEditor::Highlighter final : public QSyntaxHighlighter {
public:
    explicit Highlighter(QTextDocument* document)
        : QSyntaxHighlighter(document),
          m_keyword(makeFormat(QColor(0x1f, 0x6f, 0xd0), true)),
          m_string(makeFormat(QColor(0xc0, 0x5a, 0x1a))),
          m_identifier(makeFormat(QColor(0x0b, 0x8a, 0x8a))),
          m_number(makeFormat(QColor(0x9a, 0x3f, 0xc0))),
          m_comment(makeFormat(QColor(0x80, 0x80, 0x80), false, true)) {}

    void configure(core::sql::SqlSyntax syntax, const std::vector<std::string>& keywords) {
        m_syntax = syntax;
        m_keywords.clear();
        for (const std::string& keyword : keywords) {
            m_keywords.insert(toLowerAscii(keyword));
        }
        rehighlight();
    }

protected:
    void highlightBlock(const QString& text) override {
        std::string opener;
        int lines = 0;
        if (previousBlockState() > 0) {
            if (const auto* previous = dynamic_cast<const OpenerData*>(currentBlock().previous().userData())) {
                opener = previous->opener;
                lines = previous->lines;
            }
        }
        const QByteArray body = text.toUtf8();
        std::string input = opener;
        input.append(body.constData(), static_cast<std::size_t>(body.size()));
        const bool ascii = body.size() == text.size();
        const std::vector<int> positions = ascii ? std::vector<int>() : utf16Positions(body);
        const auto toUtf16 = [&](std::size_t byte) {
            return ascii ? static_cast<int>(byte) : positions[byte];
        };

        const core::sql::SqlTokenizer tokenizer(m_syntax);
        SqlToken token;
        std::size_t offset = 0;
        std::string next;
        bool continued = false; // o token aberto no fim é o mesmo que veio da linha anterior
        while (tokenizer.next(input, offset, true, token) == core::sql::SqlTokenizer::Status::Token &&
               token.length > 0) {
            offset = token.offset + token.length;
            const std::string_view raw = std::string_view(input).substr(token.offset, token.length);
            if (const QTextCharFormat* format = formatFor(raw, token.kind)) {
                const std::size_t start = std::max(token.offset, opener.size()) - opener.size();
                const std::size_t end = offset - opener.size();
                if (end > start) {
                    setFormat(toUtf16(start), toUtf16(end) - toUtf16(start), *format);
                }
            }
            if (!token.complete) {
                next = openerOf(raw, token.kind, m_syntax.nestedBlockComments);
                continued = !opener.empty() && token.offset == 0;
            }
        }
        lines = continued ? lines + 1 : 1;
        if (next.empty() || lines > kMaxContinuationLines) {
            setCurrentBlockState(0);
            setCurrentBlockUserData(nullptr);
            return;
        }
        // O estado muda a cada linha atravessada, então o Qt só para de refazer o realce depois do limite.
        const auto hash = static_cast<unsigned>(qHash(QByteArray::fromStdString(next)));
        setCurrentBlockState(
            static_cast<int>((((hash & kHashMask) << kLinesBits) | static_cast<unsigned>(lines)) | (1U << 30U)));
        setCurrentBlockUserData(new OpenerData(std::move(next), lines)); // o bloco fica dono
    }

private:
    [[nodiscard]] const QTextCharFormat* formatFor(std::string_view raw, SqlTokenKind kind) const {
        switch (kind) {
        case SqlTokenKind::LineComment:
        case SqlTokenKind::BlockComment:
            return &m_comment;
        case SqlTokenKind::String:
            return &m_string;
        case SqlTokenKind::QuotedIdentifier:
            return &m_identifier;
        case SqlTokenKind::Number:
            return &m_number;
        case SqlTokenKind::Word:
            return m_keywords.contains(toLowerAscii(raw)) ? &m_keyword : nullptr;
        default:
            return nullptr;
        }
    }

    core::sql::SqlSyntax m_syntax;
    std::unordered_set<std::string> m_keywords;
    QTextCharFormat m_keyword;
    QTextCharFormat m_string;
    QTextCharFormat m_identifier;
    QTextCharFormat m_number;
    QTextCharFormat m_comment;
};

/// Números de linha e marcadores de dobra, à esquerda do texto.
class SqlEditor::Gutter final : public QWidget {
public:
    explicit Gutter(SqlEditor* editor) : QWidget(editor), m_editor(editor) {}

    [[nodiscard]] QSize sizeHint() const override { return {m_editor->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { m_editor->paintGutter(event->rect()); }
    void mousePressEvent(QMouseEvent* event) override { m_editor->gutterClicked(event->position().toPoint()); }

private:
    SqlEditor* m_editor;
};

SqlEditor::SqlEditor(QWidget* parent)
    : QPlainTextEdit(parent),
      m_highlighter(new Highlighter(document())),
      m_gutter(new Gutter(this)),
      m_completer(new QCompleter(this)),
      m_completionModel(new QStandardItemModel(this)) {
    setObjectName(QStringLiteral("sqlEditor"));
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * kTabWidth);

    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setMaxVisibleItems(12);
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this, &SqlEditor::insertCompletion);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &SqlEditor::updateGutterWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy != 0) {
            m_gutter->scroll(0, dy);
        } else {
            m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        }
        if (rect.contains(viewport()->rect())) {
            updateGutterWidth();
        }
    });
    updateGutterWidth();
}

SqlEditor::~SqlEditor() = default;

void SqlEditor::setSyntax(core::sql::SqlSyntax syntax, const std::vector<std::string>& keywords) {
    m_syntax = syntax;
    m_highlighter->configure(syntax, keywords);
}

void SqlEditor::setCompletionIndex(std::shared_ptr<const services::query::CompletionIndex> index) {
    m_index = std::move(index);
    if (!m_index) {
        m_completer->popup()->hide();
    } else if (m_completer->popup()->isVisible()) {
        showCompletion(); // chegaram colunas novas enquanto o popup estava aberto
    }
}

// ---- Gutter ----

int SqlEditor::gutterWidth() const {
    const int digits = static_cast<int>(QString::number(std::max(1, blockCount())).size());
    constexpr int kMinDigits = 3;
    constexpr int kPadding = 8;
    return kPadding + (fontMetrics().horizontalAdvance(QLatin1Char('9')) * std::max(digits, kMinDigits)) +
           fontMetrics().height();
}

void SqlEditor::updateGutterWidth() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

void SqlEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_gutter->setGeometry(QRect(area.left(), area.top(), gutterWidth(), area.height()));
}

void SqlEditor::paintGutter(const QRect& area) {
    QPainter painter(m_gutter);
    painter.fillRect(area, palette().color(QPalette::Window));
    const int lineHeight = fontMetrics().height();
    const int numberWidth = m_gutter->width() - lineHeight - 4;
    const QColor color = palette().color(QPalette::PlaceholderText);
    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && top <= area.bottom()) {
        const qreal height = blockBoundingRect(block).height();
        if (block.isVisible() && top + height >= area.top()) {
            const int y = qRound(top);
            painter.setPen(color);
            painter.drawText(0, y, numberWidth, lineHeight, Qt::AlignRight, QString::number(block.blockNumber() + 1));
            if (isFoldable(block)) {
                const QRectF box(numberWidth + 4 + (lineHeight * 0.25), y + (lineHeight * 0.25), lineHeight * 0.5,
                                 lineHeight * 0.5);
                QPolygonF arrow;
                if (isFolded(block)) {
                    arrow << box.topLeft() << QPointF(box.right(), box.center().y()) << box.bottomLeft();
                } else {
                    arrow << box.topLeft() << box.topRight() << QPointF(box.center().x(), box.bottom());
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawPolygon(arrow);
            }
        }
        top += height;
        block = block.next();
    }
}

void SqlEditor::gutterClicked(const QPoint& position) {
    if (position.x() < m_gutter->width() - fontMetrics().height() - 4) {
        return;
    }
    const QTextBlock block = cursorForPosition(QPoint(0, position.y())).block();
    if (isFoldable(block) || isFolded(block)) {
        toggleFold(block);
    }
}

// ---- Dobra ----

bool SqlEditor::isFoldable(const QTextBlock& block) {
    const int indent = indentOf(block.text());
    if (indent < 0) {
        return false;
    }
    QTextBlock next = block.next();
    while (next.isValid() && indentOf(next.text()) < 0) {
        next = next.next();
    }
    return next.isValid() && indentOf(next.text()) > indent;
}

bool SqlEditor::isFolded(const QTextBlock& block) {
    return block.next().isValid() && !block.next().isVisible();
}

void SqlEditor::toggleFold(const QTextBlock& block) {
    QTextBlock last = block;
    if (isFolded(block)) {
        for (QTextBlock next = block.next(); next.isValid() && !next.isVisible(); next = next.next()) {
            next.setVisible(true);
            last = next;
        }
    } else {
        // Esconde as linhas mais indentadas que a primeira, sem as linhas em branco do fim.
        const int base = indentOf(block.text());
        std::vector<QTextBlock> hidden;
        for (QTextBlock next = block.next(); next.isValid(); next = next.next()) {
            const int indent = indentOf(next.text());
            if (indent >= 0 && indent <= base) {
                break;
            }
            hidden.push_back(next);
        }
        while (!hidden.empty() && indentOf(hidden.back().text()) < 0) {
            hidden.pop_back();
        }
        for (QTextBlock& line : hidden) {
            line.setVisible(false);
        }
        if (!hidden.empty()) {
            last = hidden.back();
        }
        if (!textCursor().block().isVisible()) {
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::EndOfBlock);
            setTextCursor(cursor);
        }
    }
    document()->markContentsDirty(block.position(), last.position() + last.length() - block.position());
    viewport()->update();
    m_gutter->update();
}

// ---- Edição ----

namespace {

/// Primeiro e último bloco da seleção (uma seleção que termina no começo de uma linha não inclui essa linha).
std::pair<QTextBlock, QTextBlock> selectedBlocks(const QTextCursor& cursor) {
    const QTextDocument* document = cursor.document();
    const QTextBlock first = document->findBlock(cursor.selectionStart());
    QTextBlock last = document->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && last != first && last.position() == cursor.selectionEnd()) {
        last = last.previous();
    }
    return {first, last};
}

} // namespace

void SqlEditor::toggleComment() {
    const auto [first, last] = selectedBlocks(textCursor());
    const QString marker = QStringLiteral("--");
    bool allCommented = true;
    bool any = false;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString trimmed = block.text().trimmed();
        if (!trimmed.isEmpty()) {
            any = true;
            allCommented = allCommented && trimmed.startsWith(marker);
        }
        if (block == last) {
            break;
        }
    }
    if (!any) {
        return;
    }
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const QString text = block.text();
        int indent = 0;
        while (indent < text.size() && text.at(indent).isSpace()) {
            ++indent;
        }
        if (!text.trimmed().isEmpty()) {
            edit.setPosition(block.position() + indent);
            if (allCommented) {
                // "-- " (com espaço) é o que o MySQL exige; tira o espaço junto, se houver.
                const bool space = text.mid(indent + 2, 1) == QStringLiteral(" ");
                edit.setPosition(block.position() + indent + 2 + (space ? 1 : 0), QTextCursor::KeepAnchor);
                edit.removeSelectedText();
            } else {
                edit.insertText(QStringLiteral("-- "));
            }
        }
        if (block == last) {
            break;
        }
    }
    edit.endEditBlock();
}

void SqlEditor::moveLines(int direction) {
    QTextCursor cursor = textCursor();
    const auto [first, last] = selectedBlocks(cursor);
    const QTextBlock neighbour = direction < 0 ? first.previous() : last.next();
    if (!neighbour.isValid()) {
        return;
    }
    const int start = first.position();
    const int end = last.position() + last.length() - 1; // sem a quebra de linha final
    const int anchor = cursor.anchor() - start;
    const int position = cursor.position() - start;

    QTextCursor edit(document());
    edit.setPosition(start);
    edit.setPosition(end, QTextCursor::KeepAnchor);
    const QString lines = edit.selectedText();
    const QString other = neighbour.text();
    int newStart = 0;
    edit.beginEditBlock();
    if (direction < 0) {
        newStart = neighbour.position();
        edit.setPosition(neighbour.position());
        edit.setPosition(end, QTextCursor::KeepAnchor);
        edit.insertText(lines + QLatin1Char('\n') + other);
    } else {
        newStart = start + static_cast<int>(other.size()) + 1;
        edit.setPosition(start);
        edit.setPosition(neighbour.position() + neighbour.length() - 1, QTextCursor::KeepAnchor);
        edit.insertText(other + QLatin1Char('\n') + lines);
    }
    edit.endEditBlock();
    cursor.setPosition(newStart + anchor);
    cursor.setPosition(newStart + position, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
}

void SqlEditor::setWordWrap(bool wrap) {
    setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

bool SqlEditor::wordWrap() const {
    return lineWrapMode() != QPlainTextEdit::NoWrap;
}

int SqlEditor::replaceAll(const QString& text, const QString& replacement, QTextDocument::FindFlags flags) {
    if (text.isEmpty()) {
        return 0;
    }
    flags &= ~QTextDocument::FindFlags(QTextDocument::FindBackward);
    int count = 0;
    QTextCursor edit(document());
    edit.beginEditBlock();
    QTextCursor found = document()->find(text, 0, flags);
    while (!found.isNull()) {
        found.insertText(replacement);
        ++count;
        found = document()->find(text, found, flags);
    }
    edit.endEditBlock();
    return count;
}

QString SqlEditor::selectedSql() const {
    QString text = textCursor().selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

void SqlEditor::contextMenuEvent(QContextMenuEvent* event) {
    const std::unique_ptr<QMenu> menu(createStandardContextMenu());
    menu->addSeparator();
    menu->addAction(tr("Comentar ou descomentar"), this, &SqlEditor::toggleComment);
    menu->addAction(tr("Mover linhas para cima\tAlt+Up"), this, [this] { moveLines(-1); });
    menu->addAction(tr("Mover linhas para baixo\tAlt+Down"), this, [this] { moveLines(1); });
    QAction* wrap = menu->addAction(tr("Quebra de linha automática"));
    wrap->setCheckable(true);
    wrap->setChecked(wordWrap());
    connect(wrap, &QAction::toggled, this, &SqlEditor::setWordWrap);
    menu->exec(event->globalPos());
}

// ---- Autocomplete ----

SqlEditor::CompletionWindow SqlEditor::completionWindow() const {
    const QTextCursor cursor = textCursor();
    const QTextBlock current = cursor.block();
    // A janela para numa linha que termina uma sentença fora de strings e comentários (estado 0 do realce).
    const auto endsStatement = [](const QTextBlock& block) {
        return block.userState() <= 0 && block.text().trimmed().endsWith(QLatin1Char(';'));
    };
    QTextBlock first = current;
    for (int i = 0; i < kWindowBefore && first.previous().isValid() && !endsStatement(first.previous()); ++i) {
        first = first.previous();
    }
    QTextBlock last = current;
    for (int i = 0; i < kWindowAfter && last.next().isValid() && !endsStatement(last); ++i) {
        last = last.next();
    }
    CompletionWindow window;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        if (block == current) {
            window.cursor = window.text.size() +
                            static_cast<std::size_t>(current.text().left(cursor.positionInBlock()).toUtf8().size());
        }
        window.text += block.text().toStdString();
        if (block == last) {
            break;
        }
        window.text += '\n';
    }
    return window;
}

void SqlEditor::showCompletion() {
    if (!m_index) {
        return;
    }
    const CompletionWindow window = completionWindow();
    const std::vector<services::query::Completion> items =
        m_index->complete(window.text, window.cursor, kMaxCompletions);
    if (m_missingTables) {
        std::vector<core::model::ObjectRef> missing = m_index->missingTables(window.text, window.cursor);
        if (!missing.empty()) {
            m_missingTables(std::move(missing));
        }
    }
    QAbstractItemView* popup = m_completer->popup();
    if (items.empty()) {
        popup->hide();
        return;
    }
    m_completionModel->clear();
    for (const services::query::Completion& item : items) {
        auto* row = new QStandardItem(toQt(item.text)); // o modelo fica dono
        row->setToolTip(toQt(item.detail));
        row->setEditable(false);
        m_completionModel->appendRow(row);
    }
    m_completer->setCompletionPrefix({});
    popup->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    QRect rect = cursorRect();
    rect.setWidth(popup->sizeHintForColumn(0) + popup->verticalScrollBar()->sizeHint().width());
    m_completer->complete(rect);
}

int SqlEditor::prefixStart() const {
    const QTextCursor cursor = textCursor();
    const QString line = cursor.block().text();
    int start = cursor.positionInBlock();
    while (start > 0) {
        const QChar c = line.at(start - 1);
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('$')) {
            break;
        }
        --start;
    }
    return cursor.block().position() + start;
}

void SqlEditor::insertCompletion(const QString& text) {
    QTextCursor cursor = textCursor();
    cursor.setPosition(prefixStart(), QTextCursor::KeepAnchor);
    cursor.insertText(text);
    setTextCursor(cursor);
}

void SqlEditor::autoComplete(const QString& typed) {
    if (!m_index) {
        return;
    }
    QAbstractItemView* popup = m_completer->popup();
    const bool visible = popup->isVisible();
    const int length = textCursor().position() - prefixStart();
    const bool dot = typed == QStringLiteral(".");
    const bool word = !typed.isEmpty() && (typed.back().isLetterOrNumber() || typed.back() == QLatin1Char('_'));
    if (dot || (word && length >= (visible ? 1 : kMinTypedForPopup))) {
        showCompletion();
    } else if (visible) {
        popup->hide();
    }
}

void SqlEditor::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (m_completer->popup()->isVisible()) {
        switch (key) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore(); // o QCompleter trata
            return;
        default:
            break;
        }
    }
    if (key == Qt::Key_Space && (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) != 0) {
        showCompletion();
        return;
    }
    if ((modifiers & Qt::AltModifier) != 0 && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        moveLines(key == Qt::Key_Up ? -1 : 1);
        return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier) {
        // A linha nova começa com a indentação da atual.
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        qsizetype indent = 0;
        while (indent < line.size() && (line.at(indent) == QLatin1Char(' ') || line.at(indent) == QLatin1Char('\t'))) {
            ++indent;
        }
        cursor.insertText(QLatin1Char('\n') + line.left(std::min<qsizetype>(indent, cursor.positionInBlock())));
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        if (m_completer->popup()->isVisible()) {
            if (textCursor().position() > prefixStart()) {
                showCompletion();
            } else {
                m_completer->popup()->hide();
            }
        }
        return;
    }
    if (!event->text().isEmpty()) {
        autoComplete(event->text());
    }
}

} // namespace mydb::ui
