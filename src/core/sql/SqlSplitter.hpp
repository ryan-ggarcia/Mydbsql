#pragma once

#include "core/sql/SqlStatement.hpp"
#include "core/sql/SqlSyntax.hpp"
#include "core/sql/SqlTokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::core::sql {

/// Divide scripts SQL em sentenças, em streaming: recebe a entrada em blocos de qualquer tamanho e
/// guarda só a sentença em andamento, então um arquivo de 1 GB usa memória constante (CLAUDE.md §2.1).
///
/// Respeita strings, identificadores entre aspas, comentários, $$ do PostgreSQL, o comando DELIMITER do
/// cliente MySQL e blocos BEGIN…END de CREATE PROCEDURE/FUNCTION/TRIGGER/EVENT (e BEGIN NOT ATOMIC),
/// cujos ";" internos não encerram a sentença. Sentenças só com comentários são descartadas.
class SqlSplitter {
public:
    using Sink = std::function<void(SqlStatement&&)>;

    explicit SqlSplitter(SqlSyntax syntax);

    /// Processa mais um bloco. Sentenças completas são entregues ao `sink` na ordem em que aparecem.
    void feed(std::string_view chunk, const Sink& sink);

    /// Fim da entrada: entrega a última sentença, mesmo sem delimitador. O splitter volta ao estado inicial.
    void finish(const Sink& sink);

    /// Atalho para um texto já inteiro na memória (ex.: o conteúdo do editor).
    [[nodiscard]] static std::vector<SqlStatement> split(std::string_view text, const SqlSyntax& syntax);

    /// Delimitador em vigor (muda com "DELIMITER x").
    [[nodiscard]] const std::string& delimiter() const noexcept { return m_delimiter; }

    /// Bytes retidos entre chamadas de feed(); usado para provar que a memória não cresce com a entrada.
    [[nodiscard]] std::size_t bufferedBytes() const noexcept { return m_buffer.size(); }

private:
    enum class Routine : std::uint8_t {
        Unknown,
        Yes,
        No,
    };

    /// Avança enquanto houver tokens completos. Devolve quando faltarem dados.
    void process(bool finalInput, const Sink& sink);
    [[nodiscard]] bool tryDelimiterCommand(bool finalInput, bool& needMore);
    void onCodeToken(std::string_view text, SqlTokenKind kind);
    void onWord(std::string_view word);
    void emit(std::size_t end, std::string_view usedDelimiter, const Sink& sink);
    void resetStatement() noexcept;
    void advanceLineTracking(std::size_t to) noexcept;
    void compact();
    [[nodiscard]] int effectiveDepth() const noexcept { return m_depth - (m_pendingEnd ? 1 : 0); }

    SqlTokenizer m_tokenizer;
    std::string m_buffer;
    std::uint64_t m_bufferOffset{0}; ///< posição absoluta de m_buffer[0] na entrada
    std::size_t m_scan{0};           ///< próximo byte a analisar em m_buffer
    std::size_t m_statementStart{0}; ///< início da sentença em andamento em m_buffer
    std::size_t m_retryWhenPending{0}; ///< só reanalisa quando houver ao menos estes bytes pendentes

    std::size_t m_trackPos{0};       ///< até onde linhas e colunas já foram contadas em m_buffer
    std::uint32_t m_trackLine{1};
    std::uint32_t m_trackColumn{1};

    std::string m_delimiter{";"};
    bool m_hasCode{false};
    Routine m_routine{Routine::Unknown};
    std::vector<std::string> m_leadWords;
    int m_depth{0};
    bool m_pendingEnd{false};
};

} // namespace mydb::core::sql
