#include "core/sql/SqlFormatter.hpp"

#include "core/sql/SqlTokenizer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mydb::core::sql {

namespace {

// Só palavras reservadas no MySQL/MariaDB: não podem ser identificadores sem aspas, então mudar a caixa
// nunca troca o nome de uma tabela (que no Linux diferencia maiúsculas).
constexpr auto kReserved = std::to_array<std::string_view>(
    {"add",     "all",    "alter",   "and",      "as",         "asc",        "between", "by",       "call",
     "case",    "check",  "collate", "column",   "constraint", "create",     "cross",   "database", "declare",
     "default", "delete", "desc",    "describe", "distinct",   "drop",       "else",    "elseif",   "exists",
     "explain", "false",  "for",     "foreign",  "from",       "grant",      "group",   "having",   "if",
     "in",      "index",  "inner",   "insert",   "interval",   "into",       "is",      "join",     "key",
     "left",    "like",   "limit",   "lock",     "loop",       "natural",    "not",     "null",     "on",
     "or",      "order",  "outer",   "primary",  "procedure",  "references", "rename",  "replace",  "return",
     "revoke",  "right",  "schema",  "select",   "set",        "show",       "table",   "then",     "trigger",
     "true",    "union",  "unique",  "unlock",   "update",     "use",        "using",   "values",   "when",
     "where",   "while",  "with"});

/// Começam uma linha nova (no nível zero de parênteses).
constexpr auto kClauses = std::to_array<std::string_view>({"select", "from", "where", "join", "left", "right", "inner",
                                                           "cross", "on", "group", "order", "having", "limit", "union",
                                                           "values", "set", "insert", "update", "delete"});

/// Depois destas, a palavra seguinte continua a mesma cláusula ("LEFT JOIN", "DELETE FROM").
constexpr auto kJoiners =
    std::to_array<std::string_view>({"left", "right", "inner", "cross", "outer", "natural", "delete", "full"});

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

char upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

std::string toLower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), lower);
    return result;
}

std::string toUpper(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), upper);
    return result;
}

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& list, std::string_view word) {
    for (const std::string_view item : list) {
        if (item == word) {
            return true;
        }
    }
    return false;
}

bool isReserved(std::string_view word) {
    return contains(kReserved, word);
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

enum class Clause : std::uint8_t {
    Other,
    SelectList,
    Condition, ///< WHERE, HAVING e ON: AND/OR em linhas próprias
};

class Formatter {
public:
    Formatter(std::string_view sql, const SqlSyntax& syntax, int indent)
        : m_sql(sql), m_syntax(syntax), m_indent(static_cast<std::size_t>(std::max(indent, 0)), ' ') {}

    /// false: o texto tem string ou comentário aberto, ou algo que o formatador não sabe preservar.
    bool run() {
        m_tokens = SqlTokenizer(m_syntax).tokenize(m_sql);
        for (const SqlToken& token : m_tokens) {
            if (!token.complete) {
                return false;
            }
        }
        std::size_t i = 0;
        while (i < m_tokens.size()) {
            const SqlToken& token = m_tokens[i];
            if (token.kind == SqlTokenKind::Whitespace) {
                m_hadSpace = true;
                ++i;
                continue;
            }
            if (!m_hasCode && m_syntax.delimiterCommand && token.kind == SqlTokenKind::Word &&
                toLower(text(token)) == "delimiter") {
                if (!delimiterCommand(i)) {
                    return false;
                }
                continue;
            }
            const std::size_t delimiterEnd = matchDelimiter(token);
            if (delimiterEnd != 0) {
                if (!skipTo(i, delimiterEnd)) {
                    return false;
                }
                endStatement();
                continue;
            }
            emitToken(token);
            ++i;
        }
        if (m_inStatement) {
            endStatement();
        }
        return true;
    }

    std::string take() { return std::move(m_out); }

private:
    [[nodiscard]] std::string_view text(const SqlToken& token) const {
        return m_sql.substr(token.offset, token.length);
    }

    /// "DELIMITER x" vai até o fim da linha e é copiado como está.
    bool delimiterCommand(std::size_t& i) {
        const SqlToken& word = m_tokens[i];
        std::size_t lineEnd = m_sql.find('\n', word.offset);
        if (lineEnd == std::string_view::npos) {
            lineEnd = m_sql.size();
        }
        const std::string_view argument =
            trim(m_sql.substr(word.offset + word.length, lineEnd - word.offset - word.length));
        if (argument.empty()) {
            return false;
        }
        endStatement();
        separateStatement();
        m_out += "DELIMITER ";
        m_out += argument;
        m_afterDelimiterCommand = true;
        m_delimiter = std::string(argument);
        return skipTo(i, lineEnd);
    }

    /// Avança `i` até o primeiro token que começa em `end`. false se um token atravessar `end`.
    bool skipTo(std::size_t& i, std::size_t end) {
        while (i < m_tokens.size() && m_tokens[i].offset < end) {
            if (m_tokens[i].offset + m_tokens[i].length > end) {
                return false;
            }
            ++i;
        }
        m_hadSpace = false;
        return true;
    }

    /// Fim do delimitador que começa em `token`, ou 0 se não for o delimitador.
    [[nodiscard]] std::size_t matchDelimiter(const SqlToken& token) const {
        if (token.kind != SqlTokenKind::Operator && token.kind != SqlTokenKind::Word) {
            return 0;
        }
        if (m_sql.substr(token.offset, m_delimiter.size()) != m_delimiter) {
            return 0;
        }
        return token.offset + m_delimiter.size();
    }

    void separateStatement() {
        if (!m_out.empty()) {
            m_out += m_afterDelimiterCommand ? "\n" : "\n\n";
        }
        m_afterDelimiterCommand = false;
    }

    void endStatement() {
        if (!m_inStatement) {
            return;
        }
        if (m_hasCode) {
            if (m_previous.kind == SqlTokenKind::LineComment) {
                m_out += '\n';
            }
            m_out += m_delimiter;
        }
        m_inStatement = false;
        m_hasCode = false;
        m_depth = 0;
        m_clause = Clause::Other;
        m_lineIndent = false;
        m_breakNext = false;
        m_hadSpace = false;
    }

    void newLine(bool indented) {
        m_out += '\n';
        if (indented) {
            m_out += m_indent;
        }
        m_lineIndent = indented;
    }

    void emitToken(const SqlToken& token) {
        const std::string_view raw = text(token);
        const bool afterQualifier = m_previous.kind == SqlTokenKind::Operator && !m_hadSpace &&
                                    (text(m_previous) == "." || text(m_previous) == "@");
        const std::string word = token.kind == SqlTokenKind::Word && !afterQualifier ? toLower(raw) : std::string();
        const bool depthZero = m_depth == 0;

        if (!m_inStatement) {
            separateStatement();
            m_inStatement = true;
        } else if (m_previous.kind == SqlTokenKind::LineComment) {
            newLine(m_lineIndent);
        } else if (depthZero && contains(kClauses, word) && !contains(kJoiners, m_previousWord)) {
            newLine(false);
        } else if (depthZero && m_clause == Clause::Condition && (word == "and" || word == "or")) {
            newLine(true);
        } else if (m_breakNext) {
            newLine(true);
        } else if (m_hadSpace) {
            m_out += ' ';
        }
        m_breakNext = false;
        m_hadSpace = false;

        m_out += isReserved(word) ? toUpper(raw) : std::string(raw);

        if (token.kind != SqlTokenKind::LineComment && token.kind != SqlTokenKind::BlockComment) {
            m_hasCode = true;
            m_previousWord = word;
        }
        if (token.kind == SqlTokenKind::Operator) {
            if (raw == "(") {
                ++m_depth;
            } else if (raw == ")" && m_depth > 0) {
                --m_depth;
            } else if (raw == "," && depthZero && m_clause == Clause::SelectList) {
                m_breakNext = true;
            }
        }
        if (depthZero && !word.empty()) {
            if (word == "select") {
                m_clause = Clause::SelectList;
                m_breakNext = true;
            } else if (word == "where" || word == "having" || word == "on") {
                m_clause = Clause::Condition;
            } else if (contains(kClauses, word)) {
                m_clause = Clause::Other;
            }
        }
        m_previous = token;
    }

    std::string_view m_sql;
    SqlSyntax m_syntax;
    std::string m_indent;
    std::vector<SqlToken> m_tokens;
    std::string m_out;
    std::string m_delimiter{";"};

    bool m_inStatement{false};
    bool m_hasCode{false};
    bool m_hadSpace{false};
    bool m_breakNext{false};
    bool m_lineIndent{false};
    bool m_afterDelimiterCommand{false};
    int m_depth{0};
    Clause m_clause{Clause::Other};
    SqlToken m_previous;
    std::string m_previousWord;
};

} // namespace

std::string formatSql(std::string_view sql, const SqlSyntax& syntax, int indent) {
    Formatter formatter(sql, syntax, indent);
    if (!formatter.run()) {
        return std::string(sql);
    }
    return formatter.take();
}

} // namespace mydb::core::sql
