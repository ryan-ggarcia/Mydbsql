#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace mydb::core::model {

/// Um banco na aba Host: tamanho e quantos objetos de cada tipo ele tem.
struct DatabaseSummary {
    using TimePoint = std::chrono::system_clock::time_point;

    std::string name;
    std::optional<std::int64_t> sizeBytes; ///< dados + índices das tabelas
    std::uint32_t tables{0};
    std::uint32_t views{0};
    std::uint32_t procedures{0};
    std::uint32_t functions{0};
    std::uint32_t triggers{0};
    std::uint32_t events{0};
    std::optional<TimePoint> lastModified;
    std::string collation; ///< collation padrão do banco

    friend bool operator==(const DatabaseSummary&, const DatabaseSummary&) = default;
};

} // namespace mydb::core::model
