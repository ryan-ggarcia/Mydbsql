#pragma once

namespace mydb::core::sql {

/// Regras léxicas de um dialeto: o que é comentário, string, identificador entre aspas e delimitador.
/// Cada ISqlDialect devolve a sua; os presets abaixo cobrem os SGBDs suportados.
struct SqlSyntax {
    bool hashComments{false};           ///< "# comentário" (MySQL)
    bool dashCommentNeedsSpace{false};  ///< MySQL: "--" só abre comentário se seguido de espaço ou controle
    bool nestedBlockComments{false};    ///< "/* /* */ */" (PostgreSQL)
    bool backslashEscapes{false};       ///< "\'" dentro de strings (MySQL)
    bool doubleQuotedIdentifiers{true}; ///< "x" é identificador; false: é string (MySQL sem ANSI_QUOTES)
    bool backtickIdentifiers{false};    ///< `x` (MySQL, SQLite)
    bool bracketIdentifiers{false};     ///< [x] (SQLite, SQL Server)
    bool dollarQuotes{false};           ///< $$...$$ e $tag$...$tag$ (PostgreSQL)
    bool escapeStringPrefix{false};     ///< E'...' com escapes de barra (PostgreSQL)
    bool delimiterCommand{false};       ///< comando de cliente "DELIMITER //" (MySQL)
    bool compoundBlocks{true};          ///< BEGIN…END de rotinas e triggers não terminam no ";"

    [[nodiscard]] static SqlSyntax mysql() noexcept {
        SqlSyntax s;
        s.hashComments = true;
        s.dashCommentNeedsSpace = true;
        s.backslashEscapes = true;
        s.doubleQuotedIdentifiers = false;
        s.backtickIdentifiers = true;
        s.delimiterCommand = true;
        return s;
    }

    [[nodiscard]] static SqlSyntax postgresql() noexcept {
        SqlSyntax s;
        s.nestedBlockComments = true;
        s.dollarQuotes = true;
        s.escapeStringPrefix = true;
        return s;
    }

    [[nodiscard]] static SqlSyntax sqlite() noexcept {
        SqlSyntax s;
        s.backtickIdentifiers = true;
        s.bracketIdentifiers = true;
        return s;
    }

    friend bool operator==(const SqlSyntax&, const SqlSyntax&) = default;
};

} // namespace mydb::core::sql
