#pragma once

#include "core/sql/SqlSyntax.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mydb::core::sql {

enum class SqlTokenKind : std::uint8_t {
    Whitespace,
    LineComment,
    BlockComment,
    Word,             ///< palavra-chave ou identificador sem aspas
    QuotedIdentifier, ///< "x", `x` ou [x], conforme o dialeto
    String,           ///< 'x', E'x', $$x$$ e, no MySQL, "x"
    Number,
    Operator, ///< um caractere de pontuação ou operador, inclusive ";"
};

struct SqlToken {
    SqlTokenKind kind{SqlTokenKind::Whitespace};
    std::size_t offset{0};
    std::size_t length{0};
    bool complete{true}; ///< false: string ou comentário sem fechamento no fim da entrada

    friend bool operator==(const SqlToken&, const SqlToken&) = default;
};

/// Analisador léxico sem estado próprio: lê um token a partir de uma posição. Serve ao SqlSplitter
/// (entrada em blocos) e, depois, ao realce de sintaxe e ao autocomplete.
class SqlTokenizer {
public:
    enum class Status : std::uint8_t {
        Token,    ///< `out` contém o próximo token
        NeedMore, ///< o token pode continuar além do fim de `text`; chame de novo com mais dados
        End,      ///< `offset` está no fim de `text`
    };

    explicit SqlTokenizer(SqlSyntax syntax) noexcept : m_syntax(syntax) {}

    /// Com finalInput=false, nunca corta um token no fim de `text`: devolve NeedMore.
    /// Com finalInput=true, o que sobrar no fim vira um token (incompleto, se for o caso).
    [[nodiscard]] Status next(std::string_view text, std::size_t offset, bool finalInput, SqlToken& out) const;

    /// Todos os tokens de um texto completo.
    [[nodiscard]] std::vector<SqlToken> tokenize(std::string_view text) const;

    [[nodiscard]] const SqlSyntax& syntax() const noexcept { return m_syntax; }

private:
    SqlSyntax m_syntax;
};

} // namespace mydb::core::sql
