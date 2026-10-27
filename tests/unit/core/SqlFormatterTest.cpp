#include "core/sql/SqlFormatter.hpp"

#include <gtest/gtest.h>

#include <string>

namespace mydb::core::sql {
namespace {

std::string format(std::string_view sql) {
    return formatSql(sql, SqlSyntax::mysql());
}

TEST(SqlFormatterTest, UppercasesKeywordsAndBreaksClauses) {
    EXPECT_EQ(format("select a, b from t where x = 1 and y = 2 order by a"),
              "SELECT\n    a,\n    b\nFROM t\nWHERE x = 1\n    AND y = 2\nORDER BY a;");
}

TEST(SqlFormatterTest, JoinsAndMultiWordClausesStayTogether) {
    EXPECT_EQ(format("select * from a left join b on a.id=b.id group by a.x"),
              "SELECT\n    *\nFROM a\nLEFT JOIN b\nON a.id=b.id\nGROUP BY a.x;");
    EXPECT_EQ(format("delete from t where id=1"), "DELETE FROM t\nWHERE id=1;");
    EXPECT_EQ(format("insert into t (a, b) values (1, 2)"), "INSERT INTO t (a, b)\nVALUES (1, 2);");
    EXPECT_EQ(format("update t set a = 1"), "UPDATE t\nSET a = 1;");
}

TEST(SqlFormatterTest, KeepsStringsCommentsAndQuotedIdentifiers) {
    EXPECT_EQ(format("select 'from x where', `select` from t -- from where\nwhere a=1"),
              "SELECT\n    'from x where',\n    `select`\nFROM t -- from where\nWHERE a=1;");
    EXPECT_EQ(format("select /* from */ a"), "SELECT\n    /* from */ a;");
}

TEST(SqlFormatterTest, LeavesNonReservedWordsAndQualifiedNamesAlone) {
    // "user" e "status" não são reservadas: podem ser nomes de tabela, que no Linux diferenciam caixa.
    EXPECT_EQ(format("select t.select, status from user"), "SELECT\n    t.select,\n    status\nFROM user;");
}

TEST(SqlFormatterTest, SubqueriesAndFunctionArgumentsAreNotBroken) {
    EXPECT_EQ(format("select count(a, b) from (select a from t) x"),
              "SELECT\n    count(a, b)\nFROM (SELECT a FROM t) x;");
}

TEST(SqlFormatterTest, KeepsAdjacentOperators) {
    EXPECT_EQ(format("select a>=1, b<>2, @v:=3, 1--1"), "SELECT\n    a>=1,\n    b<>2,\n    @v:=3,\n    1--1;");
}

TEST(SqlFormatterTest, SeparatesStatementsWithBlankLine) {
    EXPECT_EQ(format("select 1; select 2;\nselect 3"), "SELECT\n    1;\n\nSELECT\n    2;\n\nSELECT\n    3;");
}

TEST(SqlFormatterTest, LineCommentBeforeDelimiterGetsNewLine) {
    EXPECT_EQ(format("select 1 -- fim"), "SELECT\n    1 -- fim\n;");
    EXPECT_EQ(format("select 1; -- fim"), "SELECT\n    1;\n\n-- fim");
}

TEST(SqlFormatterTest, KeepsDelimiterCommands) {
    EXPECT_EQ(format("delimiter //\ncreate procedure p() begin select 1; end//\ndelimiter ;\nselect 2"),
              "DELIMITER //\nCREATE PROCEDURE p() begin\nSELECT\n    1; end//\n\nDELIMITER ;\nSELECT\n    2;");
}

TEST(SqlFormatterTest, UnterminatedTextComesBackUnchanged) {
    EXPECT_EQ(format("select 'abc from t"), "select 'abc from t");
    EXPECT_EQ(format("select a /* from"), "select a /* from");
}

TEST(SqlFormatterTest, EmptyAndCommentOnlyInput) {
    EXPECT_EQ(format(""), "");
    EXPECT_EQ(format("   "), "");
    EXPECT_EQ(format("-- só isto"), "-- só isto");
}

TEST(SqlFormatterTest, PostgresDollarQuotesAreUntouched) {
    EXPECT_EQ(formatSql("select $$ from where $$::text", SqlSyntax::postgresql()),
              "SELECT\n    $$ from where $$::text;");
}

} // namespace
} // namespace mydb::core::sql
