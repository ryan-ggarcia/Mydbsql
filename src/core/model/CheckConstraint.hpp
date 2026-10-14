#pragma once

#include <string>

namespace mydb::core::model {

struct CheckConstraint {
    std::string name;
    std::string expression; ///< SQL cru, sem o CHECK e sem os parênteses externos

    friend bool operator==(const CheckConstraint&, const CheckConstraint&) = default;
};

} // namespace mydb::core::model
