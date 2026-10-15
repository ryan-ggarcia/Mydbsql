#include "drivers/mysql/MySqlResultSet.hpp"

#include "drivers/mysql/MySqlConnection.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace mydb::drivers::mysql {

namespace {

using core::model::DataTypeCategory;

constexpr unsigned kBinaryCharset = 63; // "binary": a coluna guarda bytes, não texto

struct TypeDescription {
    const char* name;
    DataTypeCategory category;
};

TypeDescription describe(const MYSQL_FIELD& field) {
    using C = DataTypeCategory;
    const bool binary = field.charsetnr == kBinaryCharset;
    if ((field.flags & ENUM_FLAG) != 0U) {
        return {.name = "ENUM", .category = C::Text};
    }
    if ((field.flags & SET_FLAG) != 0U) {
        return {.name = "SET", .category = C::Text};
    }
    switch (field.type) {
    case MYSQL_TYPE_TINY:
        return {.name = "TINYINT", .category = C::Integer};
    case MYSQL_TYPE_SHORT:
        return {.name = "SMALLINT", .category = C::Integer};
    case MYSQL_TYPE_INT24:
        return {.name = "MEDIUMINT", .category = C::Integer};
    case MYSQL_TYPE_LONG:
        return {.name = "INT", .category = C::Integer};
    case MYSQL_TYPE_LONGLONG:
        return {.name = "BIGINT", .category = C::Integer};
    case MYSQL_TYPE_BIT:
        return {.name = "BIT", .category = C::Integer};
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
        return {.name = "DECIMAL", .category = C::Real};
    case MYSQL_TYPE_FLOAT:
        return {.name = "FLOAT", .category = C::Real};
    case MYSQL_TYPE_DOUBLE:
        return {.name = "DOUBLE", .category = C::Real};
    case MYSQL_TYPE_YEAR:
        return {.name = "YEAR", .category = C::Temporal};
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_NEWDATE:
        return {.name = "DATE", .category = C::Temporal};
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_TIME2:
        return {.name = "TIME", .category = C::Temporal};
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_DATETIME2:
        return {.name = "DATETIME", .category = C::Temporal};
    case MYSQL_TYPE_TIMESTAMP:
    case MYSQL_TYPE_TIMESTAMP2:
        return {.name = "TIMESTAMP", .category = C::Temporal};
    case MYSQL_TYPE_STRING:
        return binary ? TypeDescription{.name = "BINARY", .category = C::Binary}
                      : TypeDescription{.name = "CHAR", .category = C::Text};
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
        return binary ? TypeDescription{.name = "VARBINARY", .category = C::Binary}
                      : TypeDescription{.name = "VARCHAR", .category = C::Text};
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
        return binary ? TypeDescription{.name = "BLOB", .category = C::Binary}
                      : TypeDescription{.name = "TEXT", .category = C::Text};
    case MYSQL_TYPE_GEOMETRY:
        return {.name = "GEOMETRY", .category = C::Spatial};
    case MYSQL_TYPE_JSON:
        return {.name = "JSON", .category = C::Other};
    default:
        return {.name = "UNKNOWN", .category = C::Other};
    }
}

std::string toString(const char* text, unsigned length) {
    return text == nullptr ? std::string() : std::string(text, length);
}

} // namespace

MySqlResultSet::MySqlResultSet(MySqlConnection& connection, ResultHandle result, std::chrono::microseconds& networkTime)
    : m_connection(&connection), m_result(std::move(result)), m_networkTime(&networkTime) {
    const unsigned count = mysql_num_fields(m_result.get());
    const MYSQL_FIELD* fields = mysql_fetch_fields(m_result.get());
    m_columns.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const MYSQL_FIELD& field = fields[i]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): API C
        const TypeDescription type = describe(field);
        m_columns.push_back(core::ColumnInfo{
            .name = toString(field.name, field.name_length),
            .typeName = type.name,
            .category = type.category,
            .originDatabase = toString(field.db, field.db_length),
            .originTable = toString(field.org_table, field.org_table_length),
            .nullable = (field.flags & NOT_NULL_FLAG) == 0U,
            .maxLength = static_cast<std::size_t>(field.length),
        });
    }
}

bool MySqlResultSet::next() {
    const auto started = std::chrono::steady_clock::now();
    m_row = mysql_fetch_row(m_result.get());
    *m_networkTime += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);
    if (m_row == nullptr) {
        m_lengths = nullptr;
        MYSQL* handle = m_connection->handle();
        if (mysql_errno(handle) != 0) {
            m_connection->raise(handle);
        }
        return false;
    }
    m_lengths = mysql_fetch_lengths(m_result.get());
    return true;
}

void MySqlResultSet::checkColumn(std::size_t column) const {
    if (m_row == nullptr) {
        throw std::logic_error("IResultSet: não há linha atual (chame next())");
    }
    if (column >= m_columns.size()) {
        throw std::out_of_range("IResultSet: coluna " + std::to_string(column) + " não existe");
    }
}

bool MySqlResultSet::isNull(std::size_t column) const {
    checkColumn(column);
    return m_row[column] == nullptr; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): API C
}

std::string_view MySqlResultSet::text(std::size_t column) const {
    checkColumn(column);
    const char* value = m_row[column]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): API C
    return value == nullptr ? std::string_view() : std::string_view(value, m_lengths[column]); // NOLINT
}

std::span<const std::byte> MySqlResultSet::bytes(std::size_t column) const {
    const std::string_view value = text(column);
    return std::as_bytes(std::span(value.data(), value.size()));
}

} // namespace mydb::drivers::mysql
