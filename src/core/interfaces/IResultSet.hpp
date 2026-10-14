#pragma once

#include "core/model/DataType.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mydb::core {

struct ColumnInfo {
    std::string name;
    std::string typeName;
    model::DataTypeCategory category{model::DataTypeCategory::Other};
    std::string originDatabase; ///< vazio para colunas calculadas
    std::string originTable;
    bool nullable{true};
    std::optional<std::size_t> maxLength;

    friend bool operator==(const ColumnInfo&, const ColumnInfo&) = default;
};

/// Resultado lido linha a linha (streaming, CLAUDE.md §2.2, regra 4). Os valores devolvidos por text()
/// e bytes() só valem até a próxima chamada de next().
class IResultSet {
public:
    virtual ~IResultSet() = default;

    [[nodiscard]] virtual std::span<const ColumnInfo> columns() const = 0;

    /// Avança para a próxima linha; false quando acabou.
    [[nodiscard]] virtual bool next() = 0;

    [[nodiscard]] virtual bool isNull(std::size_t column) const = 0;
    [[nodiscard]] virtual std::string_view text(std::size_t column) const = 0;
    [[nodiscard]] virtual std::span<const std::byte> bytes(std::size_t column) const = 0;

protected:
    IResultSet() = default;
    IResultSet(const IResultSet&) = default;
    IResultSet(IResultSet&&) = default;
    IResultSet& operator=(const IResultSet&) = default;
    IResultSet& operator=(IResultSet&&) = default;
};

} // namespace mydb::core
