#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace mydb::core::sql {

/// Uma sentença extraída de um script, com a posição para "executar a query atual" e para apontar erros.
struct SqlStatement {
    std::string text;        ///< sem o delimitador e sem espaços nas pontas; comentários iniciais ficam
    std::uint64_t offset{0}; ///< posição, em bytes, do primeiro caractere de `text` na entrada
    std::uint32_t line{1};   ///< linha do primeiro caractere (a partir de 1)
    std::uint32_t column{1}; ///< coluna em bytes (a partir de 1)
    std::string delimiter;   ///< delimitador que a encerrou; vazio se acabou no fim da entrada

    [[nodiscard]] std::uint64_t endOffset() const noexcept { return offset + text.size(); }

    friend bool operator==(const SqlStatement&, const SqlStatement&) = default;
};

/// A sentença que contém `offset` (posição do cursor) ou, entre duas sentenças, a anterior.
/// nullptr se `statements` estiver vazio ou o cursor vier antes da primeira.
[[nodiscard]] const SqlStatement* statementAt(std::span<const SqlStatement> statements, std::uint64_t offset) noexcept;

} // namespace mydb::core::sql
