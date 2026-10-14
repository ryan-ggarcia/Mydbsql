#include "core/sql/SqlTokenizer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mydb::core::sql {
namespace {

using Kind = SqlTokenKind;
using Status = SqlTokenizer::Status;

std::vector<std::pair<Kind, std::string>> significant(std::string_view text, const SqlSyntax& syntax) {
    std::vector<std::pair<Kind, std::string>> result;
    for (const SqlToken& token : SqlTokenizer(syntax).tokenize(text)) {
        if (token.kind != Kind::Whitespace) {
            result.emplace_back(token.kind, std::string(text.substr(token.offset, token.length)));
        }
    }
    return result;
}

TEST(SqlTokenizerTest, TokensCoverTheWholeInputWithoutGaps) {
    constexpr std::string_view kSql = "SELECT `a`, \"b\", 'c' -- x\n/* y */ 1.5e+3, 0x1F FROM t;";
    const auto tokens = SqlTokenizer(SqlSyntax::mysql()).tokenize(kSql);
    std::size_t expected = 0;
    for (const SqlToken& token : tokens) {
        EXPECT_EQ(token.offset, expected);
        EXPECT_GT(token.length, 0U);
        expected += token.length;
    }
    EXPECT_EQ(expected, kSql.size());
}

TEST(SqlTokenizerTest, ClassifiesMySqlTokens) {
    const auto tokens = significant("SELECT `a`, \"b\", 'c' -- x\n/* y */ 1.5e+3 # z", SqlSyntax::mysql());
    const std::vector<std::pair<Kind, std::string>> expected{
        {Kind::Word, "SELECT"},          {Kind::QuotedIdentifier, "`a`"}, {Kind::Operator, ","},
        {Kind::String, "\"b\""},         {Kind::Operator, ","},           {Kind::String, "'c'"},
        {Kind::LineComment, "-- x"},     {Kind::BlockComment, "/* y */"}, {Kind::Number, "1.5e+3"},
        {Kind::LineComment, "# z"},
    };
    EXPECT_EQ(tokens, expected);
}

TEST(SqlTokenizerTest, ClassifiesPostgresTokens) {
    const auto tokens = significant("SELECT \"a\", $$b$$, $q$c$q$, E'd\\'', $1, .5", SqlSyntax::postgresql());
    const std::vector<std::pair<Kind, std::string>> expected{
        {Kind::Word, "SELECT"},   {Kind::QuotedIdentifier, "\"a\""}, {Kind::Operator, ","},
        {Kind::String, "$$b$$"},  {Kind::Operator, ","},             {Kind::String, "$q$c$q$"},
        {Kind::Operator, ","},    {Kind::String, "E'd\\''"},          {Kind::Operator, ","},
        {Kind::Operator, "$"},    {Kind::Number, "1"},                {Kind::Operator, ","},
        {Kind::Number, ".5"},
    };
    EXPECT_EQ(tokens, expected);
}

TEST(SqlTokenizerTest, SqliteBracketsAndUnicodeWords) {
    const auto tokens = significant("SELECT [a]]b], ação FROM t", SqlSyntax::sqlite());
    ASSERT_GE(tokens.size(), 4U);
    EXPECT_EQ(tokens[1], (std::pair<Kind, std::string>{Kind::QuotedIdentifier, "[a]]b]"}));
    EXPECT_EQ(tokens[3], (std::pair<Kind, std::string>{Kind::Word, "ação"}));
}

TEST(SqlTokenizerTest, NeedMoreWhenATokenMayContinue) {
    const SqlTokenizer mysql(SqlSyntax::mysql());
    const SqlTokenizer pg(SqlSyntax::postgresql());
    SqlToken token;
    EXPECT_EQ(mysql.next("SELEC", 0, false, token), Status::NeedMore);
    EXPECT_EQ(mysql.next("'abc", 0, false, token), Status::NeedMore);
    EXPECT_EQ(mysql.next("'abc'", 0, false, token), Status::NeedMore); // pode ser 'abc''…
    EXPECT_EQ(mysql.next("-", 0, false, token), Status::NeedMore);
    EXPECT_EQ(mysql.next("--", 0, false, token), Status::NeedMore);
    EXPECT_EQ(mysql.next("/* x *", 0, false, token), Status::NeedMore);
    EXPECT_EQ(mysql.next("# x", 0, false, token), Status::NeedMore);
    EXPECT_EQ(mysql.next("12", 0, false, token), Status::NeedMore);
    EXPECT_EQ(pg.next("$ta", 0, false, token), Status::NeedMore);
    EXPECT_EQ(pg.next("$$ a ", 0, false, token), Status::NeedMore);
    EXPECT_EQ(pg.next("E", 0, false, token), Status::NeedMore);

    ASSERT_EQ(mysql.next("'abc' ", 0, false, token), Status::Token);
    EXPECT_EQ(token.length, 5U);
    EXPECT_EQ(mysql.next("x", 1, false, token), Status::End);
}

TEST(SqlTokenizerTest, UnterminatedTokensAtFinalInputAreIncomplete) {
    const SqlTokenizer mysql(SqlSyntax::mysql());
    SqlToken token;
    ASSERT_EQ(mysql.next("'abc", 0, true, token), Status::Token);
    EXPECT_EQ(token.kind, Kind::String);
    EXPECT_FALSE(token.complete);
    EXPECT_EQ(token.length, 4U);

    ASSERT_EQ(mysql.next("/* abc", 0, true, token), Status::Token);
    EXPECT_EQ(token.kind, Kind::BlockComment);
    EXPECT_FALSE(token.complete);

    ASSERT_EQ(mysql.next("-- fim", 0, true, token), Status::Token);
    EXPECT_EQ(token.kind, Kind::LineComment);
    EXPECT_TRUE(token.complete);
}

TEST(SqlTokenizerTest, NestedBlockCommentsOnlyWhenEnabled) {
    constexpr std::string_view kSql = "/* a /* b */ c */";
    EXPECT_EQ(SqlTokenizer(SqlSyntax::postgresql()).tokenize(kSql).size(), 1U);
    EXPECT_GT(SqlTokenizer(SqlSyntax::mysql()).tokenize(kSql).size(), 1U);
}

} // namespace
} // namespace mydb::core::sql
