#pragma once

#include "core/model/Column.hpp"
#include "core/model/CheckConstraint.hpp"
#include "core/model/ForeignKey.hpp"
#include "core/model/Index.hpp"
#include "core/model/ObjectRef.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::core::model {

struct TableOptions {
    std::string engine;
    std::string collation;
    std::string comment;
    std::optional<std::uint64_t> autoIncrement;

    friend bool operator==(const TableOptions&, const TableOptions&) = default;
};

/// Estrutura completa de uma tabela: base do designer (Fase 7), do AlterTableBuilder e da modelagem (Fase 13).
struct TableDefinition {
    ObjectRef ref;
    std::vector<Column> columns;
    std::vector<Index> indexes;
    std::vector<ForeignKey> foreignKeys;
    std::vector<CheckConstraint> checks;
    TableOptions options;

    /// Comparação de nomes sem diferenciar maiúsculas (ASCII), como a maioria dos SGBDs faz com colunas.
    [[nodiscard]] const Column* findColumn(std::string_view name) const noexcept;
    [[nodiscard]] const Index* primaryKey() const noexcept;

    /// Colunas que identificam uma linha para UPDATE/DELETE (CLAUDE.md §4.4, Unit of Work):
    /// a chave primária ou, sem ela, o primeiro índice UNIQUE cujas colunas são todas NOT NULL.
    /// Vazio quando a tabela não tem chave utilizável (a grade fica somente leitura).
    [[nodiscard]] std::vector<std::string> rowIdentityColumns() const;

    friend bool operator==(const TableDefinition&, const TableDefinition&) = default;
};

} // namespace mydb::core::model
