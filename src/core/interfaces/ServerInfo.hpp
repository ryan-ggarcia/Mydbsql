#pragma once

#include <cstdint>
#include <string>

namespace mydb::core {

struct ServerInfo {
    std::string productName;   ///< ex.: "MariaDB", "MySQL", "PostgreSQL", "SQLite"
    std::string versionString; ///< como o servidor informa, ex.: "11.4.2-MariaDB-log"
    std::uint32_t version{0};  ///< major*10000 + minor*100 + patch, ex.: 110402
    std::uint64_t connectionId{0};

    friend bool operator==(const ServerInfo&, const ServerInfo&) = default;
};

} // namespace mydb::core
