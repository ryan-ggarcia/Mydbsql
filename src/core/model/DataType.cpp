#include "core/model/DataType.hpp"

namespace mydb::core::model {

std::string_view toString(DataTypeCategory category) noexcept {
    switch (category) {
    case DataTypeCategory::Integer:
        return "integer";
    case DataTypeCategory::Real:
        return "real";
    case DataTypeCategory::Text:
        return "text";
    case DataTypeCategory::Binary:
        return "binary";
    case DataTypeCategory::Temporal:
        return "temporal";
    case DataTypeCategory::Spatial:
        return "spatial";
    case DataTypeCategory::Other:
        break;
    }
    return "other";
}

} // namespace mydb::core::model
