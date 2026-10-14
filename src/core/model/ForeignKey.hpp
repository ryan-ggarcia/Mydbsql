#pragma once

#include "core/model/ObjectRef.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::core::model {

enum class ReferentialAction : std::uint8_t {
    NoAction,
    Restrict,
    Cascade,
    SetNull,
    SetDefault,
};

[[nodiscard]] std::string_view toSql(ReferentialAction action) noexcept;

struct ForeignKey {
    std::string name;
    std::vector<std::string> columns;
    ObjectRef referencedTable;
    std::vector<std::string> referencedColumns;
    ReferentialAction onUpdate{ReferentialAction::NoAction};
    ReferentialAction onDelete{ReferentialAction::NoAction};

    friend bool operator==(const ForeignKey&, const ForeignKey&) = default;
};

} // namespace mydb::core::model
