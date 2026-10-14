#include "core/model/Column.hpp"

namespace mydb::core::model {

std::string Column::fullType() const {
    std::string result = typeName;
    if (!lengthSet.empty()) {
        result += '(';
        result += lengthSet;
        result += ')';
    }
    if (isUnsigned) {
        result += " UNSIGNED";
    }
    return result;
}

} // namespace mydb::core::model
