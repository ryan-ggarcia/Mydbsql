#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mydb::core::model {

struct IndexColumn {
    std::string name;
    std::optional<std::uint32_t> prefixLength; ///< ex.: KEY (nome(10)) no MySQL
    bool descending{false};

    friend bool operator==(const IndexColumn&, const IndexColumn&) = default;
};

struct Index {
    enum class Kind : std::uint8_t {
        Primary,
        Unique,
        Key,
        Fulltext,
        Spatial,
    };

    std::string name; ///< vazio para PRIMARY KEY nos SGBDs que não dão nome a ela
    Kind kind{Kind::Key};
    std::vector<IndexColumn> columns;
    std::string algorithm; ///< ex.: BTREE, HASH; vazio = padrão do SGBD
    std::string comment;

    friend bool operator==(const Index&, const Index&) = default;
};

} // namespace mydb::core::model
