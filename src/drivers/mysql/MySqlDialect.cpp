#include "drivers/mysql/MySqlDialect.hpp"

#include <array>
#include <string>
#include <utility>

namespace mydb::drivers::mysql {

namespace {

using core::model::DataType;
using core::model::DataTypeCategory;

DataType type(std::string name, DataTypeCategory category, std::string defaultLength = {},
              bool lengthRequired = false) {
    DataType t;
    t.name = std::move(name);
    t.category = category;
    t.defaultLength = std::move(defaultLength);
    t.lengthRequired = lengthRequired;
    t.lengthAllowed = lengthRequired || !t.defaultLength.empty();
    return t;
}

std::vector<DataType> buildDataTypes(const MySqlServerVersion& version) {
    using C = DataTypeCategory;
    std::vector<DataType> types;
    for (const char* name : {"TINYINT", "SMALLINT", "MEDIUMINT", "INT", "BIGINT"}) {
        DataType t = type(name, C::Integer);
        t.lengthAllowed = true;
        t.unsignedAllowed = true;
        types.push_back(std::move(t));
    }
    types.push_back(type("BIT", C::Integer, "1"));
    for (const char* name : {"FLOAT", "DOUBLE"}) {
        DataType t = type(name, C::Real);
        t.unsignedAllowed = true;
        types.push_back(std::move(t));
    }
    DataType decimal = type("DECIMAL", C::Real, "10,0");
    decimal.unsignedAllowed = true;
    types.push_back(std::move(decimal));

    for (const char* name : {"DATE", "TIME", "DATETIME", "TIMESTAMP", "YEAR"}) {
        types.push_back(type(name, C::Temporal));
    }

    const auto text = [](std::string name, std::string length = {}, bool required = false) {
        DataType t = type(std::move(name), DataTypeCategory::Text, std::move(length), required);
        t.charsetAllowed = true;
        return t;
    };
    types.push_back(text("CHAR", "1"));
    types.push_back(text("VARCHAR", "255", true));
    for (const char* name : {"TINYTEXT", "TEXT", "MEDIUMTEXT", "LONGTEXT"}) {
        types.push_back(text(name));
    }
    types.push_back(text("ENUM", "'a','b'", true));
    types.push_back(text("SET", "'a','b'", true));

    types.push_back(type("BINARY", C::Binary, "1"));
    types.push_back(type("VARBINARY", C::Binary, "255", true));
    for (const char* name : {"TINYBLOB", "BLOB", "MEDIUMBLOB", "LONGBLOB"}) {
        types.push_back(type(name, C::Binary));
    }

    for (const char* name : {"GEOMETRY", "POINT", "LINESTRING", "POLYGON", "MULTIPOINT", "MULTILINESTRING",
                             "MULTIPOLYGON", "GEOMETRYCOLLECTION"}) {
        types.push_back(type(name, C::Spatial));
    }

    types.push_back(type("JSON", version.mariaDb ? C::Text : C::Other));
    if (version.mariaDb && version.atLeast(10, 5)) {
        types.push_back(type("INET6", C::Other));
    }
    if (version.mariaDb && version.atLeast(10, 7)) {
        types.push_back(type("UUID", C::Other));
    }
    if (version.mariaDb && version.atLeast(10, 10)) {
        types.push_back(type("INET4", C::Other));
    }
    return types;
}

} // namespace

MySqlDialect::MySqlDialect(MySqlServerVersion version, SqlMode mode)
    : m_version(std::move(version)), m_mode(mode), m_dataTypes(buildDataTypes(m_version)) {}

std::string MySqlDialect::quoteIdentifier(std::string_view identifier) const {
    const char quote = m_mode.ansiQuotes ? '"' : '`';
    std::string result;
    result.reserve(identifier.size() + 2);
    result += quote;
    for (const char c : identifier) {
        if (c == quote) {
            result += quote;
        }
        result += c;
    }
    result += quote;
    return result;
}

std::string MySqlDialect::quoteString(std::string_view value) const {
    std::string result;
    result.reserve(value.size() + 2);
    result += '\'';
    for (const char c : value) {
        if (m_mode.noBackslashEscapes) {
            if (c == '\'') {
                result += '\'';
            }
            result += c;
            continue;
        }
        // Mesmo conjunto de mysql_real_escape_string para utf8mb4 (sem I/O: o dialeto não tem conexão).
        switch (c) {
        case '\0':
            result += "\\0";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\'':
            result += "\\'";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\x1a':
            result += "\\Z";
            break;
        default:
            result += c;
        }
    }
    result += '\'';
    return result;
}

std::string MySqlDialect::quoteBinary(std::span<const std::byte> value) const {
    if (value.empty()) {
        return "''";
    }
    constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string result = "0x";
    result.reserve(2 + (value.size() * 2));
    for (const std::byte b : value) {
        const auto v = std::to_integer<unsigned>(b);
        result += kHex.at(v >> 4U);
        result += kHex.at(v & 0x0FU);
    }
    return result;
}

std::string MySqlDialect::applyLimit(std::string_view select, std::int64_t limit, std::int64_t offset) const {
    std::string result(select);
    if (limit < 0 && offset <= 0) {
        return result;
    }
    // Sem limite mas com deslocamento: o MySQL exige um LIMIT; o maior valor sem sinal é o idioma documentado.
    const std::string count = limit < 0 ? "18446744073709551615" : std::to_string(limit);
    result += " LIMIT ";
    if (offset > 0) {
        result += std::to_string(offset);
        result += ", ";
    }
    result += count;
    return result;
}

bool MySqlDialect::supports(core::Feature feature) const {
    using core::Feature;
    const bool maria = m_version.mariaDb;
    switch (feature) {
    case Feature::Schemas:
        return false; // no MySQL, "schema" é sinônimo de banco
    case Feature::Sequences:
        return maria && m_version.atLeast(10, 3);
    case Feature::Events:
    case Feature::Triggers:
    case Feature::StoredRoutines:
    case Feature::FulltextIndexes:
    case Feature::SpatialIndexes:
    case Feature::MultipleResultSets:
    case Feature::Transactions:
        return true;
    case Feature::CheckConstraints:
        return maria ? m_version.atLeast(10, 2, 1) : m_version.atLeast(8, 0, 16);
    case Feature::GeneratedColumns:
        return maria || m_version.atLeast(5, 7, 6);
    case Feature::InvisibleColumns:
        return maria ? m_version.atLeast(10, 3, 3) : m_version.atLeast(8, 0, 23);
    case Feature::DescendingIndexes:
        return maria ? m_version.atLeast(10, 8) : m_version.atLeast(8, 0);
    }
    return false;
}

core::sql::SqlSyntax MySqlDialect::syntax() const {
    core::sql::SqlSyntax syntax = core::sql::SqlSyntax::mysql();
    syntax.doubleQuotedIdentifiers = m_mode.ansiQuotes;
    syntax.backslashEscapes = !m_mode.noBackslashEscapes;
    return syntax;
}

MySqlDialect::SqlMode MySqlDialect::parseSqlMode(std::string_view sqlMode) {
    SqlMode mode;
    std::size_t start = 0;
    while (start <= sqlMode.size()) {
        const std::size_t comma = sqlMode.find(',', start);
        const std::string_view item =
            sqlMode.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        // ANSI é um modo combinado que inclui ANSI_QUOTES.
        if (item == "ANSI_QUOTES" || item == "ANSI") {
            mode.ansiQuotes = true;
        } else if (item == "NO_BACKSLASH_ESCAPES") {
            mode.noBackslashEscapes = true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return mode;
}

} // namespace mydb::drivers::mysql
