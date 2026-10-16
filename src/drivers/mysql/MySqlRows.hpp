#pragma once

// Leitura de lotes de introspecção e administração: poucas linhas, materializadas de uma vez.

#include "core/interfaces/IResultSet.hpp"

#include <charconv>
#include <chrono>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace mydb::drivers::mysql {

class MySqlConnection;

namespace rows {

using Row = std::vector<std::optional<std::string>>;
using Rows = std::vector<Row>;

[[nodiscard]] Rows readAll(core::IResultSet& result);

/// Executa um lote e devolve as linhas de cada resultado, na ordem das sentenças.
[[nodiscard]] std::vector<Rows> runBatch(MySqlConnection& connection, const std::string& sql);

[[nodiscard]] inline std::string text(const Row& row, std::size_t column) {
    return row.at(column).value_or(std::string());
}

template <typename Number>
[[nodiscard]] std::optional<Number> number(const Row& row, std::size_t column) {
    const auto& value = row.at(column);
    if (!value) {
        return std::nullopt;
    }
    Number result{};
    const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), result);
    if (error != std::errc{} || end != value->data() + value->size()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> date(const Row& row, std::size_t column);

} // namespace rows

} // namespace mydb::drivers::mysql
