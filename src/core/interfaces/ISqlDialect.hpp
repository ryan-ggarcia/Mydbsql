#pragma once

#include "core/model/DataType.hpp"
#include "core/sql/SqlSyntax.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mydb::core {

/// Recursos que variam entre SGBDs e versões. Substitui o TFeatureOrRequirement do HeidiSQL.
enum class Feature : std::uint8_t {
    Schemas,
    Sequences,
    Events,
    Triggers,
    StoredRoutines,
    CheckConstraints,
    GeneratedColumns,
    InvisibleColumns,
    DescendingIndexes,
    FulltextIndexes,
    SpatialIndexes,
    MultipleResultSets,
    Transactions,
};

/// Regras textuais de um SGBD. Sem I/O: pode ser usado em qualquer thread.
class ISqlDialect {
public:
    virtual ~ISqlDialect() = default;

    [[nodiscard]] virtual std::string quoteIdentifier(std::string_view identifier) const = 0;
    [[nodiscard]] virtual std::string quoteString(std::string_view value) const = 0;
    [[nodiscard]] virtual std::string quoteBinary(std::span<const std::byte> value) const = 0;
    [[nodiscard]] virtual std::string applyLimit(std::string_view select, std::int64_t limit,
                                                 std::int64_t offset) const = 0;
    [[nodiscard]] virtual std::span<const model::DataType> dataTypes() const = 0;
    [[nodiscard]] virtual bool supports(Feature feature) const = 0;

    /// Regras léxicas usadas pelo SqlSplitter e pelo realce de sintaxe.
    [[nodiscard]] virtual sql::SqlSyntax syntax() const = 0;

protected:
    ISqlDialect() = default;
    ISqlDialect(const ISqlDialect&) = default;
    ISqlDialect(ISqlDialect&&) = default;
    ISqlDialect& operator=(const ISqlDialect&) = default;
    ISqlDialect& operator=(ISqlDialect&&) = default;
};

} // namespace mydb::core
