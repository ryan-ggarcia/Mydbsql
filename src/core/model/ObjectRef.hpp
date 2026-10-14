#pragma once

#include <cstdint>
#include <string>

namespace mydb::core::model {

enum class ObjectType : std::uint8_t {
    Table,
    View,
    Procedure,
    Function,
    Trigger,
    Event,
    Sequence,
    Other,
};

/// Identifica um objeto do banco. `schema` fica vazio nos SGBDs sem esse nível (MySQL, SQLite).
struct ObjectRef {
    std::string database;
    std::string schema;
    std::string name;
    ObjectType type{ObjectType::Table};

    friend bool operator==(const ObjectRef&, const ObjectRef&) = default;
};

} // namespace mydb::core::model
