#pragma once

#include "core/model/DbObject.hpp"
#include "core/model/TableDefinition.hpp"
#include "core/sql/SqlSyntax.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::services::query {

enum class CompletionKind : std::uint8_t {
    Keyword,
    Function,
    Database,
    Table,
    View,
    Routine,
    Column,
    Alias,
    Snippet,
};

struct Completion {
    std::string text;   ///< o que entra no editor
    std::string detail; ///< assinatura da função, tipo da coluna, tabela de origem…
    CompletionKind kind{CompletionKind::Keyword};

    friend bool operator==(const Completion&, const Completion&) = default;
};

struct SqlFunction {
    std::string name;
    std::string signature;   ///< ex.: "CONCAT(str1, str2, ...)"
    std::string description; ///< uma linha
};

/// Índice de autocomplete em memória (CLAUDE.md §2.3, regra 5): vetores ordenados com busca por prefixo,
/// sem diferenciar maiúsculas de minúsculas, que respondem em ≤ 10 ms. É montado fora da thread da UI e
/// depois só lido (consultas const são thread-safe).
///
/// Contexto da consulta: `complete(text, cursor)` olha o texto antes do cursor:
/// - "banco." → objetos daquele banco; "tabela." ou "alias." → colunas daquela tabela (os aliases vêm de
///   FROM/JOIN/UPDATE/INTO do mesmo texto, pelo SqlTokenizer);
/// - fora disso → palavras-chave, funções, bancos, objetos do banco atual e aliases do texto.
/// Strings e comentários não completam nada.
class CompletionIndex {
public:
    explicit CompletionIndex(core::sql::SqlSyntax syntax);

    void setKeywords(std::vector<std::string> keywords);
    void setFunctions(std::vector<SqlFunction> functions);
    void setDatabases(std::vector<std::string> databases, std::string currentDatabase);
    void setObjects(std::string_view database, const std::vector<core::model::DbObject>& objects);
    void setColumns(const core::model::TableDefinition& table);
    void setSnippets(std::vector<std::string> names);

    /// No máximo `limit` itens, os de prefixo exato primeiro, em ordem alfabética.
    [[nodiscard]] std::vector<Completion> complete(std::string_view text, std::size_t cursor,
                                                   std::size_t limit = 200) const;

    /// Tabelas citadas antes de `cursor` cujas colunas ainda não estão no índice (quem chama busca com
    /// IMetadataProvider::table e passa a setColumns).
    [[nodiscard]] std::vector<core::model::ObjectRef> missingTables(std::string_view text, std::size_t cursor) const;

private:
    struct Entry {
        std::string key; ///< em minúsculas, para a busca
        Completion completion;
    };
    struct Table {
        std::string database;
        std::string name;
        std::vector<Entry> columns;
    };

    core::sql::SqlSyntax m_syntax;
    std::vector<Entry> m_global; ///< palavras-chave, funções, bancos, snippets; ordenado por key
    std::vector<std::pair<std::string, std::vector<Entry>>> m_objects; ///< por banco
    std::vector<Table> m_tables;
    std::string m_currentDatabase;
};

/// Catálogo de funções lido do JSON de resources/functions/<dialeto>.json:
/// {"functions": [{"name": "CONCAT", "signature": "CONCAT(str1, str2, ...)", "description": "…"}]}.
/// Lança std::runtime_error se o JSON for inválido.
[[nodiscard]] std::vector<SqlFunction> parseFunctionCatalog(std::string_view json);

/// Palavras-chave de SQL do dialeto (uma lista própria, não copiada do HeidiSQL).
[[nodiscard]] std::vector<std::string> sqlKeywords(std::string_view driverId);

} // namespace mydb::services::query
