// Partes do driver MySQL sem I/O: versão, dialeto e leitura do information_schema.

#include "drivers/mysql/MySqlColumnParser.hpp"
#include "drivers/mysql/MySqlDialect.hpp"
#include "drivers/mysql/MySqlServerVersion.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace mydb::drivers::mysql {
namespace {

using core::Feature;
using core::model::ColumnDefault;

TEST(MySqlServerVersionTest, ParsesMySqlAndMariaDbVersions) {
    const auto mysql = MySqlServerVersion::parse("8.0.45");
    EXPECT_FALSE(mysql.mariaDb);
    EXPECT_EQ(mysql.number, 80045U);
    EXPECT_TRUE(mysql.atLeast(8, 0, 16));
    EXPECT_FALSE(mysql.atLeast(8, 1));

    const auto maria = MySqlServerVersion::parse("11.4.12-MariaDB-log");
    EXPECT_TRUE(maria.mariaDb);
    EXPECT_EQ(maria.number, 110412U);

    const auto compat = MySqlServerVersion::parse("5.5.5-10.6.18-MariaDB");
    EXPECT_TRUE(compat.mariaDb);
    EXPECT_EQ(compat.number, 100618U);

    EXPECT_EQ(MySqlServerVersion::parse("").number, 0U);
    EXPECT_EQ(MySqlServerVersion::parse("9").number, 90000U);
}

TEST(MySqlDialectTest, QuotesIdentifiersStringsAndBinary) {
    const MySqlDialect dialect(MySqlServerVersion::parse("8.0.45"));
    EXPECT_EQ(dialect.quoteIdentifier("a`b"), "`a``b`");
    EXPECT_EQ(dialect.quoteString("it's \"x\"\n\\\r\x1a"), R"('it\'s \"x\"\n\\\r\Z')");
    EXPECT_EQ(dialect.quoteString(std::string("a\0b", 3)), R"('a\0b')");
    const std::array bytes{std::byte{0x00}, std::byte{0xAB}, std::byte{0x10}};
    EXPECT_EQ(dialect.quoteBinary(bytes), "0x00AB10");
    EXPECT_EQ(dialect.quoteBinary({}), "''");
}

TEST(MySqlDialectTest, SqlModeChangesQuoting) {
    const auto mode = MySqlDialect::parseSqlMode("STRICT_TRANS_TABLES,ANSI_QUOTES,NO_BACKSLASH_ESCAPES");
    EXPECT_TRUE(mode.ansiQuotes);
    EXPECT_TRUE(mode.noBackslashEscapes);
    EXPECT_TRUE(MySqlDialect::parseSqlMode("ANSI").ansiQuotes);
    EXPECT_FALSE(MySqlDialect::parseSqlMode("").ansiQuotes);

    const MySqlDialect dialect(MySqlServerVersion::parse("8.0.45"), mode);
    EXPECT_EQ(dialect.quoteIdentifier("a\"b"), "\"a\"\"b\"");
    EXPECT_EQ(dialect.quoteString("it's \\"), "'it''s \\'");
    const auto syntax = dialect.syntax();
    EXPECT_TRUE(syntax.doubleQuotedIdentifiers);
    EXPECT_FALSE(syntax.backslashEscapes);
    EXPECT_TRUE(syntax.delimiterCommand);
}

TEST(MySqlDialectTest, ApplyLimit) {
    const MySqlDialect dialect(MySqlServerVersion::parse("8.0.45"));
    EXPECT_EQ(dialect.applyLimit("SELECT 1", 10, 0), "SELECT 1 LIMIT 10");
    EXPECT_EQ(dialect.applyLimit("SELECT 1", 10, 20), "SELECT 1 LIMIT 20, 10");
    EXPECT_EQ(dialect.applyLimit("SELECT 1", -1, 0), "SELECT 1");
    EXPECT_EQ(dialect.applyLimit("SELECT 1", -1, 5), "SELECT 1 LIMIT 5, 18446744073709551615");
}

TEST(MySqlDialectTest, FeaturesDependOnFlavorAndVersion) {
    const MySqlDialect mysql57(MySqlServerVersion::parse("5.7.44"));
    const MySqlDialect mysql80(MySqlServerVersion::parse("8.0.45"));
    const MySqlDialect maria(MySqlServerVersion::parse("11.4.12-MariaDB"));
    EXPECT_FALSE(mysql57.supports(Feature::CheckConstraints));
    EXPECT_TRUE(mysql80.supports(Feature::CheckConstraints));
    EXPECT_TRUE(mysql80.supports(Feature::InvisibleColumns));
    EXPECT_FALSE(mysql80.supports(Feature::Sequences));
    EXPECT_TRUE(maria.supports(Feature::Sequences));
    EXPECT_TRUE(maria.supports(Feature::DescendingIndexes));
    EXPECT_FALSE(maria.supports(Feature::Schemas));
    EXPECT_TRUE(maria.supports(Feature::MultipleResultSets));

    const auto has = [](const MySqlDialect& d, std::string_view name) {
        return std::ranges::any_of(d.dataTypes(), [name](const auto& t) { return t.name == name; });
    };
    EXPECT_TRUE(has(mysql80, "VARCHAR"));
    EXPECT_FALSE(has(mysql80, "UUID"));
    EXPECT_TRUE(has(maria, "UUID"));
    EXPECT_TRUE(has(maria, "INET6"));
}

InformationSchemaColumn column(std::string type, std::string columnType, bool nullable = true) {
    InformationSchemaColumn c;
    c.name = "c";
    c.dataType = std::move(type);
    c.columnType = std::move(columnType);
    c.nullable = nullable;
    return c;
}

TEST(MySqlColumnParserTest, TypeLengthAndUnsigned) {
    auto row = column("int", "int(10) unsigned", false);
    auto parsed = parseColumn(row, false);
    EXPECT_EQ(parsed.typeName, "INT");
    EXPECT_EQ(parsed.lengthSet, "10");
    EXPECT_TRUE(parsed.isUnsigned);
    EXPECT_FALSE(parsed.nullable);
    EXPECT_EQ(parsed.fullType(), "INT(10) UNSIGNED");

    parsed = parseColumn(column("enum", "enum('a','b(c)')"), false);
    EXPECT_EQ(parsed.lengthSet, "'a','b(c)'");
    EXPECT_FALSE(parsed.isUnsigned);
}

TEST(MySqlColumnParserTest, MySqlDefaults) {
    auto row = column("varchar", "varchar(10)");
    EXPECT_EQ(parseColumn(row, false).defaultValue.kind, ColumnDefault::Kind::Null);
    row.nullable = false;
    EXPECT_EQ(parseColumn(row, false).defaultValue.kind, ColumnDefault::Kind::None);
    row.defaultValue = "abc";
    EXPECT_EQ(parseColumn(row, false).defaultValue,
              (ColumnDefault{.kind = ColumnDefault::Kind::Literal, .text = "abc"}));

    auto ts = column("timestamp", "timestamp", false);
    ts.defaultValue = "CURRENT_TIMESTAMP";
    ts.extra = "DEFAULT_GENERATED on update CURRENT_TIMESTAMP";
    const auto parsed = parseColumn(ts, false);
    EXPECT_EQ(parsed.defaultValue.kind, ColumnDefault::Kind::Expression);
    EXPECT_EQ(parsed.onUpdate, "CURRENT_TIMESTAMP");

    auto id = column("int", "int", false);
    id.extra = "auto_increment";
    EXPECT_EQ(parseColumn(id, false).defaultValue.kind, ColumnDefault::Kind::AutoIncrement);
}

TEST(MySqlColumnParserTest, MariaDbDefaults) {
    auto row = column("varchar", "varchar(10)");
    EXPECT_EQ(parseColumn(row, true).defaultValue.kind, ColumnDefault::Kind::None);
    row.defaultValue = "NULL";
    EXPECT_EQ(parseColumn(row, true).defaultValue.kind, ColumnDefault::Kind::Null);
    row.defaultValue = "'it''s'";
    EXPECT_EQ(parseColumn(row, true).defaultValue,
              (ColumnDefault{.kind = ColumnDefault::Kind::Literal, .text = "it's"}));
    row.defaultValue = "-1.5e3";
    EXPECT_EQ(parseColumn(row, true).defaultValue.kind, ColumnDefault::Kind::Literal);
    row.defaultValue = "current_timestamp()";
    EXPECT_EQ(parseColumn(row, true).defaultValue.kind, ColumnDefault::Kind::Expression);
}

TEST(MySqlColumnParserTest, GeneratedAndInvisibleColumns) {
    auto row = column("int", "int");
    row.generationExpression = "`a` + 1";
    row.extra = "STORED GENERATED";
    auto parsed = parseColumn(row, false);
    ASSERT_TRUE(parsed.generated.has_value());
    EXPECT_TRUE(parsed.generated->stored);
    EXPECT_EQ(parsed.defaultValue.kind, ColumnDefault::Kind::None);

    row.extra = "VIRTUAL GENERATED INVISIBLE";
    parsed = parseColumn(row, false);
    EXPECT_FALSE(parsed.generated->stored);
    EXPECT_TRUE(parsed.invisible);

    row.extra = "PERSISTENT GENERATED";
    EXPECT_TRUE(parseColumn(row, true).generated->stored);
}

TEST(MySqlColumnParserTest, ReferentialActionsAndDates) {
    using core::model::ReferentialAction;
    EXPECT_EQ(parseReferentialAction("CASCADE"), ReferentialAction::Cascade);
    EXPECT_EQ(parseReferentialAction("SET NULL"), ReferentialAction::SetNull);
    EXPECT_EQ(parseReferentialAction("SET DEFAULT"), ReferentialAction::SetDefault);
    EXPECT_EQ(parseReferentialAction("RESTRICT"), ReferentialAction::Restrict);
    EXPECT_EQ(parseReferentialAction("NO ACTION"), ReferentialAction::NoAction);

    const auto when = parseDateTime("2026-10-15 13:45:07");
    ASSERT_TRUE(when.has_value());
    const auto days = std::chrono::floor<std::chrono::days>(*when);
    EXPECT_EQ(std::chrono::year_month_day{days},
              (std::chrono::year_month_day{std::chrono::year{2026}, std::chrono::October, std::chrono::day{15}}));
    EXPECT_EQ(*when - days, std::chrono::hours{13} + std::chrono::minutes{45} + std::chrono::seconds{7});
    EXPECT_FALSE(parseDateTime("2026-13-01 00:00:00").has_value());
    EXPECT_FALSE(parseDateTime("ontem").has_value());
}

} // namespace
} // namespace mydb::drivers::mysql
