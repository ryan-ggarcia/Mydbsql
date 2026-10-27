#pragma once

#include "core/sql/SqlSyntax.hpp"

#include <string>
#include <string_view>

namespace mydb::core::sql {

/// Reformata SQL localmente (sem rede), a partir dos tokens do SqlTokenizer:
/// - palavras-chave em maiúsculas; identificadores, strings, números e comentários intactos;
/// - quebra de linha antes de SELECT, FROM, WHERE, [LEFT|RIGHT|INNER|CROSS] JOIN, ON, GROUP BY, ORDER BY,
///   HAVING, LIMIT, UNION, VALUES, SET, INSERT INTO, UPDATE, DELETE FROM;
/// - itens de listas do SELECT e AND/OR do WHERE em linhas próprias, com `indent` espaços;
/// - sentenças separadas por uma linha em branco, cada uma terminada pelo delimitador.
/// Nunca muda o significado: só espaços e caixa das palavras-chave mudam. Texto com string ou comentário
/// sem fechamento volta sem alteração.
[[nodiscard]] std::string formatSql(std::string_view sql, const SqlSyntax& syntax, int indent = 4);

} // namespace mydb::core::sql
