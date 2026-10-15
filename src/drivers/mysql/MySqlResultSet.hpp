#pragma once

#include "core/interfaces/IResultSet.hpp"
#include "drivers/mysql/MySqlHandles.hpp"

#include <chrono>
#include <vector>

namespace mydb::drivers::mysql {

class MySqlConnection;

/// Linhas lidas sob demanda com mysql_use_result: o servidor só manda a próxima linha quando next() pede.
class MySqlResultSet final : public core::IResultSet {
public:
    MySqlResultSet(MySqlConnection& connection, ResultHandle result, std::chrono::microseconds& networkTime);

    [[nodiscard]] std::span<const core::ColumnInfo> columns() const override { return m_columns; }
    [[nodiscard]] bool next() override;
    [[nodiscard]] bool isNull(std::size_t column) const override;
    [[nodiscard]] std::string_view text(std::size_t column) const override;
    [[nodiscard]] std::span<const std::byte> bytes(std::size_t column) const override;

private:
    void checkColumn(std::size_t column) const;

    MySqlConnection* m_connection;
    ResultHandle m_result;
    std::chrono::microseconds* m_networkTime;
    std::vector<core::ColumnInfo> m_columns;
    MYSQL_ROW m_row{nullptr};
    unsigned long* m_lengths{nullptr}; // NOLINT(google-runtime-int): tipo da API C
};

} // namespace mydb::drivers::mysql
