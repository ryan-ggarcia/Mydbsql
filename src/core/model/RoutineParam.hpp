#pragma once

#include <cstdint>
#include <string>

namespace mydb::core::model {

/// Parâmetro de procedure ou function.
struct RoutineParam {
    enum class Mode : std::uint8_t {
        In,
        Out,
        InOut,
    };

    std::string name;
    Mode mode{Mode::In};
    std::string fullType; ///< tipo como escrito na declaração, ex.: "VARCHAR(20)"

    friend bool operator==(const RoutineParam&, const RoutineParam&) = default;
};

} // namespace mydb::core::model
