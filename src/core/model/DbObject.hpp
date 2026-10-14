#pragma once

#include "core/model/ObjectRef.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace mydb::core::model {

/// Um objeto como aparece na árvore e na aba Database: identidade mais os metadados que o SGBD informar.
/// Campos opcionais ficam vazios quando o SGBD não fornece o dado (ex.: tamanho de uma view).
struct DbObject {
    using TimePoint = std::chrono::system_clock::time_point;

    ObjectRef ref;
    std::optional<std::int64_t> rows;      ///< estimativa do SGBD, não um COUNT(*)
    std::optional<std::int64_t> sizeBytes; ///< dados + índices
    std::optional<TimePoint> created;
    std::optional<TimePoint> updated;
    std::string engine;
    std::string collation;
    std::string comment;

    friend bool operator==(const DbObject&, const DbObject&) = default;
};

} // namespace mydb::core::model
