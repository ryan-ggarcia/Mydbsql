#include "drivers/mysql/MySqlColumnParser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace mydb::drivers::mysql {

namespace {

using core::model::ColumnDefault;

std::string toUpper(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return result;
}

std::string toLower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
}

/// Literal numérico como o servidor escreve: -12, 3.5, 1e-3. Feito à mão porque o from_chars de double
/// ainda falta na libc++ de alguns Xcode suportados.
bool isNumber(std::string_view text) {
    std::size_t i = 0;
    const auto digits = [&]() {
        const std::size_t start = i;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        return i > start;
    };
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        ++i;
    }
    bool any = digits();
    if (i < text.size() && text[i] == '.') {
        ++i;
        any = digits() || any;
    }
    if (!any) {
        return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
            ++i;
        }
        if (!digits()) {
            return false;
        }
    }
    return i == text.size();
}

/// 'it''s' → it's
std::string unquoteSqlString(std::string_view quoted) {
    std::string result;
    const std::string_view body = quoted.substr(1, quoted.size() - 2);
    result.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        result += body[i];
        if (body[i] == '\'' && i + 1 < body.size() && body[i + 1] == '\'') {
            ++i;
        }
    }
    return result;
}

ColumnDefault parseDefault(const InformationSchemaColumn& row, const std::string& extraLower, bool mariaDb) {
    if (extraLower.find("auto_increment") != std::string::npos) {
        return {.kind = ColumnDefault::Kind::AutoIncrement, .text = {}};
    }
    if (!row.generationExpression.empty()) {
        return {};
    }
    if (!row.defaultValue) {
        // MySQL não distingue "sem DEFAULT" de "DEFAULT NULL" numa coluna anulável; NULL é o efeito prático.
        if (!mariaDb && row.nullable) {
            return {.kind = ColumnDefault::Kind::Null, .text = {}};
        }
        return {};
    }
    const std::string& value = *row.defaultValue;
    if (mariaDb) {
        if (value == "NULL") {
            return {.kind = ColumnDefault::Kind::Null, .text = {}};
        }
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
            return {.kind = ColumnDefault::Kind::Literal, .text = unquoteSqlString(value)};
        }
        if (isNumber(value)) {
            return {.kind = ColumnDefault::Kind::Literal, .text = value};
        }
        return {.kind = ColumnDefault::Kind::Expression, .text = value};
    }
    if (extraLower.find("default_generated") != std::string::npos) {
        return {.kind = ColumnDefault::Kind::Expression, .text = value};
    }
    return {.kind = ColumnDefault::Kind::Literal, .text = value};
}

} // namespace

core::model::Column parseColumn(const InformationSchemaColumn& row, bool mariaDb) {
    core::model::Column column;
    column.name = row.name;
    column.typeName = toUpper(row.dataType);
    const std::size_t open = row.columnType.find('(');
    const std::size_t close = row.columnType.rfind(')');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        column.lengthSet = row.columnType.substr(open + 1, close - open - 1);
    }
    const std::string typeLower = toLower(row.columnType);
    column.isUnsigned = typeLower.find(" unsigned") != std::string::npos;
    column.nullable = row.nullable;

    const std::string extraLower = toLower(row.extra);
    column.defaultValue = parseDefault(row, extraLower, mariaDb);
    constexpr std::string_view kOnUpdate = "on update ";
    if (const std::size_t at = extraLower.find(kOnUpdate); at != std::string::npos) {
        std::string_view rest = std::string_view(row.extra).substr(at + kOnUpdate.size());
        rest = rest.substr(0, rest.find(' '));
        column.onUpdate = toUpper(rest);
    }
    if (!row.generationExpression.empty()) {
        const bool stored =
            extraLower.find("stored") != std::string::npos || extraLower.find("persistent") != std::string::npos;
        column.generated = core::model::GeneratedColumn{.expression = row.generationExpression, .stored = stored};
    }
    column.invisible = extraLower.find("invisible") != std::string::npos;
    column.charset = row.charset;
    column.collation = row.collation;
    column.comment = row.comment;
    return column;
}

core::model::ReferentialAction parseReferentialAction(std::string_view rule) noexcept {
    using core::model::ReferentialAction;
    if (rule == "CASCADE") {
        return ReferentialAction::Cascade;
    }
    if (rule == "SET NULL") {
        return ReferentialAction::SetNull;
    }
    if (rule == "SET DEFAULT") {
        return ReferentialAction::SetDefault;
    }
    if (rule == "RESTRICT") {
        return ReferentialAction::Restrict;
    }
    return ReferentialAction::NoAction;
}

std::optional<std::chrono::system_clock::time_point> parseDateTime(std::string_view text) {
    // AAAA-MM-DD HH:MM:SS
    constexpr std::size_t kLength = 19;
    if (text.size() < kLength || text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':' ||
        text[16] != ':') {
        return std::nullopt;
    }
    const auto number = [text](std::size_t from, std::size_t length) -> std::optional<int> {
        int value = 0;
        const auto [end, error] = std::from_chars(text.data() + from, text.data() + from + length, value);
        if (error != std::errc{} || end != text.data() + from + length) {
            return std::nullopt;
        }
        return value;
    };
    const auto year = number(0, 4);
    const auto month = number(5, 2);
    const auto day = number(8, 2);
    const auto hour = number(11, 2);
    const auto minute = number(14, 2);
    const auto second = number(17, 2);
    if (!year || !month || !day || !hour || !minute || !second) {
        return std::nullopt;
    }
    const std::chrono::year_month_day date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) {
        return std::nullopt;
    }
    return std::chrono::sys_days{date} + std::chrono::hours{*hour} + std::chrono::minutes{*minute} +
           std::chrono::seconds{*second};
}

} // namespace mydb::drivers::mysql
