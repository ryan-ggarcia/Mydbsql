#include "core/model/ForeignKey.hpp"

namespace mydb::core::model {

std::string_view toSql(ReferentialAction action) noexcept {
    switch (action) {
    case ReferentialAction::Restrict:
        return "RESTRICT";
    case ReferentialAction::Cascade:
        return "CASCADE";
    case ReferentialAction::SetNull:
        return "SET NULL";
    case ReferentialAction::SetDefault:
        return "SET DEFAULT";
    case ReferentialAction::NoAction:
        break;
    }
    return "NO ACTION";
}

} // namespace mydb::core::model
