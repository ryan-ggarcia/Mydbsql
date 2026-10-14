#include "core/sql/SqlTokenizer.hpp"

namespace mydb::core::sql {

namespace {

using Status = SqlTokenizer::Status;

bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

bool isAsciiLetter(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/// Bytes >= 0x80 fazem parte de identificadores UTF-8 sem aspas.
bool isWordStart(char c) noexcept { return isAsciiLetter(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

bool isWordChar(char c) noexcept { return isWordStart(c) || isDigit(c) || c == '$'; }

/// Lê um token já classificado. Concentra o "preciso de mais dados" para não repetir em cada caso.
class Reader {
public:
    Reader(std::string_view text, std::size_t offset, bool finalInput, SqlToken& out) noexcept
        : m_text(text), m_offset(offset), m_final(finalInput), m_out(out) {}

    [[nodiscard]] std::size_t size() const noexcept { return m_text.size(); }
    [[nodiscard]] bool finalInput() const noexcept { return m_final; }
    [[nodiscard]] bool has(std::size_t index) const noexcept { return index < m_text.size(); }
    [[nodiscard]] char at(std::size_t index) const noexcept { return m_text[index]; }
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }

    Status token(SqlTokenKind kind, std::size_t end, bool complete = true) const noexcept {
        m_out = SqlToken{.kind = kind, .offset = m_offset, .length = end - m_offset, .complete = complete};
        return Status::Token;
    }

    /// Chegou ao fim do texto sem fechar o token.
    Status unterminated(SqlTokenKind kind) const noexcept {
        return m_final ? token(kind, m_text.size(), false) : Status::NeedMore;
    }

    Status lineComment(std::size_t from) const noexcept {
        const std::size_t newline = m_text.find('\n', from);
        if (newline == std::string_view::npos) {
            // Comentário de linha sem quebra no fim da entrada final está completo.
            return m_final ? token(SqlTokenKind::LineComment, m_text.size()) : Status::NeedMore;
        }
        return token(SqlTokenKind::LineComment, newline);
    }

    Status blockComment(bool nested) const noexcept {
        int depth = 1;
        std::size_t i = m_offset + 2;
        while (i < m_text.size()) {
            const char c = m_text[i];
            if ((c == '*' || (nested && c == '/')) && i + 1 >= m_text.size()) {
                break; // "*" ou "/" no fim: o próximo caractere decide
            }
            if (c == '*' && m_text[i + 1] == '/') {
                i += 2;
                if (--depth == 0) {
                    return token(SqlTokenKind::BlockComment, i);
                }
            } else if (nested && c == '/' && m_text[i + 1] == '*') {
                i += 2;
                ++depth;
            } else {
                ++i;
            }
        }
        return unterminated(SqlTokenKind::BlockComment);
    }

    /// Texto entre `close`, com fechamento duplicado como escape ('' ou ``) e, opcionalmente, barra.
    Status quoted(std::size_t bodyStart, char close, bool backslash, SqlTokenKind kind) const noexcept {
        std::size_t i = bodyStart;
        while (i < m_text.size()) {
            const char c = m_text[i];
            if (backslash && c == '\\') {
                if (i + 1 >= m_text.size()) {
                    break;
                }
                i += 2;
                continue;
            }
            if (c == close) {
                if (i + 1 >= m_text.size()) {
                    // Pode ser o início de um fechamento duplicado no próximo bloco.
                    return m_final ? token(kind, i + 1) : Status::NeedMore;
                }
                if (m_text[i + 1] == close) {
                    i += 2;
                    continue;
                }
                return token(kind, i + 1);
            }
            ++i;
        }
        return unterminated(kind);
    }

private:
    std::string_view m_text;
    std::size_t m_offset;
    bool m_final;
    SqlToken& m_out;
};

Status readDollar(const Reader& r, std::size_t offset) {
    // $$corpo$$ ou $tag$corpo$tag$. "$1" (parâmetro) e um "$" solto são operadores.
    std::size_t i = offset + 1;
    if (!r.has(i)) {
        return r.finalInput() ? r.token(SqlTokenKind::Operator, offset + 1) : Status::NeedMore;
    }
    if (r.at(i) != '$') {
        if (!isWordStart(r.at(i))) {
            return r.token(SqlTokenKind::Operator, offset + 1);
        }
        while (r.has(i) && (isWordStart(r.at(i)) || isDigit(r.at(i)))) {
            ++i;
        }
        if (!r.has(i)) {
            return r.finalInput() ? r.token(SqlTokenKind::Operator, offset + 1) : Status::NeedMore;
        }
        if (r.at(i) != '$') {
            return r.token(SqlTokenKind::Operator, offset + 1);
        }
    }
    const std::string_view tag = r.text().substr(offset, i + 1 - offset);
    const std::size_t close = r.text().find(tag, i + 1);
    if (close == std::string_view::npos) {
        return r.unterminated(SqlTokenKind::String);
    }
    return r.token(SqlTokenKind::String, close + tag.size());
}

Status readNumber(const Reader& r, std::size_t offset) {
    const bool hex = r.at(offset) == '0' && r.has(offset + 1) && (r.at(offset + 1) == 'x' || r.at(offset + 1) == 'X');
    std::size_t i = offset;
    while (r.has(i)) {
        const char c = r.at(i);
        if (!hex && (c == 'e' || c == 'E') && r.has(i + 1) && (r.at(i + 1) == '+' || r.at(i + 1) == '-')) {
            i += 2;
        } else if (isDigit(c) || isAsciiLetter(c) || c == '.' || c == '_') {
            ++i;
        } else {
            break;
        }
    }
    if (!r.has(i) && !r.finalInput()) {
        return Status::NeedMore;
    }
    return r.token(SqlTokenKind::Number, i);
}

} // namespace

SqlTokenizer::Status SqlTokenizer::next(std::string_view text, std::size_t offset, bool finalInput,
                                        SqlToken& out) const {
    if (offset >= text.size()) {
        return Status::End;
    }
    const Reader r(text, offset, finalInput, out);
    const char c = text[offset];
    const bool hasNext = offset + 1 < text.size();
    const char nextChar = hasNext ? text[offset + 1] : '\0';
    // Um único caractere que pode começar um token de dois (ex.: "-" de "--") no fim do bloco.
    const bool undecided = !hasNext && !finalInput;

    if (isSpace(c)) {
        std::size_t end = offset;
        while (end < text.size() && isSpace(text[end])) {
            ++end;
        }
        return r.token(SqlTokenKind::Whitespace, end);
    }
    if (c == '#' && m_syntax.hashComments) {
        return r.lineComment(offset + 1);
    }
    if (c == '-') {
        if (undecided) {
            return Status::NeedMore;
        }
        if (nextChar == '-') {
            if (!m_syntax.dashCommentNeedsSpace) {
                return r.lineComment(offset + 2);
            }
            if (offset + 2 >= text.size()) {
                return finalInput ? r.token(SqlTokenKind::LineComment, text.size()) : Status::NeedMore;
            }
            const auto after = static_cast<unsigned char>(text[offset + 2]);
            if (after <= ' ') {
                return r.lineComment(offset + 2);
            }
        }
        return r.token(SqlTokenKind::Operator, offset + 1);
    }
    if (c == '/') {
        if (undecided) {
            return Status::NeedMore;
        }
        if (nextChar == '*') {
            return r.blockComment(m_syntax.nestedBlockComments);
        }
        return r.token(SqlTokenKind::Operator, offset + 1);
    }
    if (c == '\'') {
        return r.quoted(offset + 1, '\'', m_syntax.backslashEscapes, SqlTokenKind::String);
    }
    if ((c == 'E' || c == 'e') && m_syntax.escapeStringPrefix) {
        if (undecided) {
            return Status::NeedMore;
        }
        if (nextChar == '\'') {
            return r.quoted(offset + 2, '\'', true, SqlTokenKind::String);
        }
    }
    if (c == '"') {
        return m_syntax.doubleQuotedIdentifiers
                   ? r.quoted(offset + 1, '"', false, SqlTokenKind::QuotedIdentifier)
                   : r.quoted(offset + 1, '"', m_syntax.backslashEscapes, SqlTokenKind::String);
    }
    if (c == '`' && m_syntax.backtickIdentifiers) {
        return r.quoted(offset + 1, '`', false, SqlTokenKind::QuotedIdentifier);
    }
    if (c == '[' && m_syntax.bracketIdentifiers) {
        return r.quoted(offset + 1, ']', false, SqlTokenKind::QuotedIdentifier);
    }
    if (c == '$' && m_syntax.dollarQuotes) {
        return readDollar(r, offset);
    }
    if (isDigit(c)) {
        return readNumber(r, offset);
    }
    if (c == '.') {
        if (undecided) {
            return Status::NeedMore;
        }
        if (isDigit(nextChar)) {
            return readNumber(r, offset);
        }
    }
    if (isWordStart(c) || c == '$') {
        std::size_t end = offset + 1;
        while (end < text.size() && isWordChar(text[end])) {
            ++end;
        }
        if (end >= text.size() && !finalInput) {
            return Status::NeedMore;
        }
        return r.token(SqlTokenKind::Word, end);
    }
    return r.token(SqlTokenKind::Operator, offset + 1);
}

std::vector<SqlToken> SqlTokenizer::tokenize(std::string_view text) const {
    std::vector<SqlToken> tokens;
    std::size_t offset = 0;
    SqlToken token;
    while (next(text, offset, true, token) == Status::Token) {
        tokens.push_back(token);
        offset += token.length;
    }
    return tokens;
}

} // namespace mydb::core::sql
