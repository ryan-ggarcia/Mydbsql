#include "core/model/TableDefinition.hpp"

#include <algorithm>
#include <cctype>

namespace mydb::core::model {

namespace {

bool equalsNoCase(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

std::vector<std::string> columnNames(const Index& index) {
    std::vector<std::string> result;
    result.reserve(index.columns.size());
    for (const IndexColumn& column : index.columns) {
        result.push_back(column.name);
    }
    return result;
}

} // namespace

const Column* TableDefinition::findColumn(std::string_view name) const noexcept {
    const auto found = std::ranges::find_if(columns, [name](const Column& c) { return equalsNoCase(c.name, name); });
    return found == columns.end() ? nullptr : &*found;
}

const Index* TableDefinition::primaryKey() const noexcept {
    const auto found = std::ranges::find_if(indexes, [](const Index& i) { return i.kind == Index::Kind::Primary; });
    return found == indexes.end() ? nullptr : &*found;
}

std::vector<std::string> TableDefinition::rowIdentityColumns() const {
    if (const Index* primary = primaryKey(); primary != nullptr && !primary->columns.empty()) {
        return columnNames(*primary);
    }
    for (const Index& index : indexes) {
        if (index.kind != Index::Kind::Unique || index.columns.empty()) {
            continue;
        }
        // Um UNIQUE com coluna anulável ou com prefixo admite linhas repetidas: não identifica a linha.
        const bool usable = std::ranges::all_of(index.columns, [this](const IndexColumn& indexColumn) {
            const Column* column = findColumn(indexColumn.name);
            return column != nullptr && !column->nullable && !indexColumn.prefixLength;
        });
        if (usable) {
            return columnNames(index);
        }
    }
    return {};
}

} // namespace mydb::core::model
