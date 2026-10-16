#include "drivers/mysql/MySqlRows.hpp"

#include "drivers/mysql/MySqlColumnParser.hpp"
#include "drivers/mysql/MySqlConnection.hpp"

namespace mydb::drivers::mysql::rows {

Rows readAll(core::IResultSet& result) {
    Rows all;
    const std::size_t count = result.columns().size();
    while (result.next()) {
        Row row;
        row.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            row.push_back(result.isNull(i) ? std::nullopt : std::optional(std::string(result.text(i))));
        }
        all.push_back(std::move(row));
    }
    return all;
}

std::vector<Rows> runBatch(MySqlConnection& connection, const std::string& sql) {
    std::vector<Rows> results;
    const auto execution = connection.execute(sql);
    while (execution->nextResult()) {
        core::IResultSet* result = execution->resultSet();
        results.push_back(result != nullptr ? readAll(*result) : Rows{});
    }
    return results;
}

std::optional<std::chrono::system_clock::time_point> date(const Row& row, std::size_t column) {
    const auto& value = row.at(column);
    return value ? parseDateTime(*value) : std::nullopt;
}

} // namespace mydb::drivers::mysql::rows
