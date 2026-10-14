#include "core/sql/SqlSplitter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::core::sql {
namespace {

std::vector<std::string> texts(const std::vector<SqlStatement>& statements) {
    std::vector<std::string> result;
    result.reserve(statements.size());
    for (const auto& statement : statements) {
        result.push_back(statement.text);
    }
    return result;
}

std::vector<std::string> splitTexts(std::string_view sql, const SqlSyntax& syntax = SqlSyntax::mysql()) {
    return texts(SqlSplitter::split(sql, syntax));
}

std::vector<SqlStatement> splitInChunks(std::string_view sql, const SqlSyntax& syntax, std::size_t chunkSize) {
    std::vector<SqlStatement> statements;
    SqlSplitter splitter(syntax);
    const SqlSplitter::Sink sink = [&statements](SqlStatement&& s) { statements.push_back(std::move(s)); };
    for (std::size_t offset = 0; offset < sql.size(); offset += chunkSize) {
        splitter.feed(sql.substr(offset, chunkSize), sink);
    }
    splitter.finish(sink);
    return statements;
}

using Texts = std::vector<std::string>;

// --- Básico -------------------------------------------------------------------------------------------

TEST(SqlSplitterTest, SplitsOnSemicolonAndTrimsWhitespace) {
    EXPECT_EQ(splitTexts("SELECT 1;  SELECT 2 ;\n"), (Texts{"SELECT 1", "SELECT 2"}));
}

TEST(SqlSplitterTest, LastStatementWithoutDelimiterIsKept) {
    const auto statements = SqlSplitter::split("SELECT 1; SELECT 2", SqlSyntax::mysql());
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statements[0].delimiter, ";");
    EXPECT_EQ(statements[1].text, "SELECT 2");
    EXPECT_TRUE(statements[1].delimiter.empty());
}

TEST(SqlSplitterTest, EmptyInputAndEmptyStatementsProduceNothing) {
    EXPECT_TRUE(splitTexts("").empty());
    EXPECT_TRUE(splitTexts(" \n\t ").empty());
    EXPECT_EQ(splitTexts(" ;; \n ; SELECT 1 ;;"), (Texts{"SELECT 1"}));
}

// --- Strings e identificadores ------------------------------------------------------------------------

TEST(SqlSplitterTest, SemicolonsInsideQuotesDoNotSplit) {
    EXPECT_EQ(splitTexts("SELECT 'a;b', \"c;d\", `e;f`, 'it''s;'; SELECT 2"),
              (Texts{"SELECT 'a;b', \"c;d\", `e;f`, 'it''s;'", "SELECT 2"}));
}

TEST(SqlSplitterTest, MySqlBackslashEscapesInStrings) {
    EXPECT_EQ(splitTexts(R"(SELECT 'x\';y', "a\";b"; SELECT 2)"), (Texts{R"(SELECT 'x\';y', "a\";b")", "SELECT 2"}));
}

TEST(SqlSplitterTest, Utf8TextIsPreserved) {
    EXPECT_EQ(splitTexts("SELECT 'ação;' AS `coluna;ç`; SELECT 'ü'"),
              (Texts{"SELECT 'ação;' AS `coluna;ç`", "SELECT 'ü'"}));
}

TEST(SqlSplitterTest, UnterminatedStringRunsToEndOfInput) {
    EXPECT_EQ(splitTexts("SELECT 1; SELECT 'abc; SELECT 2"), (Texts{"SELECT 1", "SELECT 'abc; SELECT 2"}));
}

// --- Comentários --------------------------------------------------------------------------------------

TEST(SqlSplitterTest, CommentsDoNotSplitAndStayWithTheNextStatement) {
    EXPECT_EQ(splitTexts("-- a; b\nSELECT 1; # c;\nSELECT 2; /* ; */ SELECT 3;"),
              (Texts{"-- a; b\nSELECT 1", "# c;\nSELECT 2", "/* ; */ SELECT 3"}));
}

TEST(SqlSplitterTest, CommentOnlyStatementsAreDropped) {
    EXPECT_EQ(splitTexts("SELECT 1; -- só comentário\n/* outro */"), (Texts{"SELECT 1"}));
}

TEST(SqlSplitterTest, MySqlDoubleDashNeedsSpaceToBeAComment) {
    EXPECT_EQ(splitTexts("SELECT 1--1; SELECT 2"), (Texts{"SELECT 1--1", "SELECT 2"}));
    EXPECT_EQ(splitTexts("SELECT 1 -- x;\n; SELECT 2"), (Texts{"SELECT 1 -- x;", "SELECT 2"}));
}

TEST(SqlSplitterTest, HashIsNotACommentOutsideMySql) {
    EXPECT_EQ(splitTexts("SELECT 1 # 2; SELECT 3", SqlSyntax::postgresql()), (Texts{"SELECT 1 # 2", "SELECT 3"}));
}

// --- DELIMITER (cliente MySQL) ------------------------------------------------------------------------

TEST(SqlSplitterTest, DelimiterCommandChangesTheDelimiter) {
    const auto statements = SqlSplitter::split("DELIMITER //\n"
                                               "CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END//\n"
                                               "DELIMITER ;\n"
                                               "SELECT 3;",
                                               SqlSyntax::mysql());
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statements[0].text, "CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END");
    EXPECT_EQ(statements[0].delimiter, "//");
    EXPECT_EQ(statements[1].text, "SELECT 3");
    EXPECT_EQ(statements[1].delimiter, ";");
}

TEST(SqlSplitterTest, CustomDelimiterGluedToAWordStillSplits) {
    EXPECT_EQ(splitTexts("DELIMITER $$\nCREATE FUNCTION f() RETURNS INT BEGIN RETURN 1; END$$\nDELIMITER ;\n"),
              (Texts{"CREATE FUNCTION f() RETURNS INT BEGIN RETURN 1; END"}));
}

TEST(SqlSplitterTest, DelimiterIsCaseInsensitiveAndOnlyAtStatementStart) {
    EXPECT_EQ(splitTexts("delimiter ;;\nSELECT 1;; SELECT 2;;"), (Texts{"SELECT 1", "SELECT 2"}));
    EXPECT_EQ(splitTexts("SELECT 1 AS delimiter; SELECT 2"), (Texts{"SELECT 1 AS delimiter", "SELECT 2"}));
}

TEST(SqlSplitterTest, DelimiterCommandAtEndOfInputWithoutNewline) {
    SqlSplitter splitter(SqlSyntax::mysql());
    std::vector<SqlStatement> statements;
    const SqlSplitter::Sink sink = [&statements](SqlStatement&& s) { statements.push_back(std::move(s)); };
    splitter.feed("SELECT 1;\nDELIMITER //", sink);
    EXPECT_EQ(splitter.delimiter(), ";"); // ainda pode vir mais texto na linha
    splitter.finish(sink);
    EXPECT_EQ(texts(statements), (Texts{"SELECT 1"}));
}

TEST(SqlSplitterTest, DelimiterCommandIsNotRecognizedOutsideMySql) {
    EXPECT_EQ(splitTexts("DELIMITER //\nSELECT 1//", SqlSyntax::postgresql()), (Texts{"DELIMITER //\nSELECT 1//"}));
}

// --- Blocos BEGIN…END ---------------------------------------------------------------------------------

TEST(SqlSplitterTest, ProcedureBodyWithoutDelimiterCommand) {
    const std::string procedure = "CREATE PROCEDURE p()\n"
                                  "BEGIN\n"
                                  "  DECLARE x INT;\n"
                                  "  IF x THEN SELECT 1; END IF;\n"
                                  "  lbl: LOOP LEAVE lbl; END LOOP lbl;\n"
                                  "  WHILE x DO SET x = 0; END WHILE;\n"
                                  "  REPEAT SET x = 1; UNTIL x END REPEAT;\n"
                                  "  CASE x WHEN 1 THEN SELECT 1; ELSE SELECT 2; END CASE;\n"
                                  "  SET x = CASE WHEN x THEN 1 ELSE 0 END;\n"
                                  "END";
    EXPECT_EQ(splitTexts(procedure + ";\nSELECT 9;"), (Texts{procedure, "SELECT 9"}));
}

TEST(SqlSplitterTest, NestedBlocks) {
    EXPECT_EQ(splitTexts("CREATE PROCEDURE p() BEGIN BEGIN SELECT 1; END; SELECT 2; END; SELECT 3"),
              (Texts{"CREATE PROCEDURE p() BEGIN BEGIN SELECT 1; END; SELECT 2; END", "SELECT 3"}));
}

TEST(SqlSplitterTest, RoutineVariants) {
    EXPECT_EQ(splitTexts("CREATE DEFINER=`root`@`%` TRIGGER t BEFORE INSERT ON x FOR EACH ROW "
                         "BEGIN SET NEW.a = 1; END; SELECT 1;"),
              (Texts{"CREATE DEFINER=`root`@`%` TRIGGER t BEFORE INSERT ON x FOR EACH ROW BEGIN SET NEW.a = 1; END",
                     "SELECT 1"}));
    EXPECT_EQ(splitTexts("CREATE OR REPLACE FUNCTION f() RETURNS INT RETURN 1; SELECT 2"),
              (Texts{"CREATE OR REPLACE FUNCTION f() RETURNS INT RETURN 1", "SELECT 2"}));
    EXPECT_EQ(splitTexts("CREATE EVENT e ON SCHEDULE EVERY 1 DAY DO BEGIN DELETE FROM t; END; SELECT 1"),
              (Texts{"CREATE EVENT e ON SCHEDULE EVERY 1 DAY DO BEGIN DELETE FROM t; END", "SELECT 1"}));
}

TEST(SqlSplitterTest, TriggerWithCaseExpressionAndNoBegin) {
    EXPECT_EQ(splitTexts("CREATE TRIGGER t BEFORE INSERT ON x FOR EACH ROW SET NEW.a = CASE WHEN 1 THEN 2 END; "
                         "SELECT 3"),
              (Texts{"CREATE TRIGGER t BEFORE INSERT ON x FOR EACH ROW SET NEW.a = CASE WHEN 1 THEN 2 END",
                     "SELECT 3"}));
}

TEST(SqlSplitterTest, TransactionBeginIsNotABlock) {
    EXPECT_EQ(splitTexts("BEGIN; INSERT INTO t VALUES (1); COMMIT;"),
              (Texts{"BEGIN", "INSERT INTO t VALUES (1)", "COMMIT"}));
    EXPECT_EQ(splitTexts("BEGIN WORK; SELECT 1"), (Texts{"BEGIN WORK", "SELECT 1"}));
}

TEST(SqlSplitterTest, MariaDbAnonymousBlock) {
    EXPECT_EQ(splitTexts("BEGIN NOT ATOMIC SELECT 1; SELECT 2; END; SELECT 3;"),
              (Texts{"BEGIN NOT ATOMIC SELECT 1; SELECT 2; END", "SELECT 3"}));
}

TEST(SqlSplitterTest, KeywordsOutsideRoutinesDoNotOpenBlocks) {
    EXPECT_EQ(splitTexts("CREATE VIEW v AS SELECT CASE WHEN a THEN 1 END AS x FROM t; SELECT 2;"),
              (Texts{"CREATE VIEW v AS SELECT CASE WHEN a THEN 1 END AS x FROM t", "SELECT 2"}));
    EXPECT_EQ(splitTexts("CREATE TABLE t (`begin` INT, `end` INT); SELECT CASE 1 WHEN 1 THEN 2 END; SELECT 3"),
              (Texts{"CREATE TABLE t (`begin` INT, `end` INT)", "SELECT CASE 1 WHEN 1 THEN 2 END", "SELECT 3"}));
}

TEST(SqlSplitterTest, SqliteTrigger) {
    EXPECT_EQ(splitTexts("CREATE TRIGGER t AFTER INSERT ON a BEGIN UPDATE b SET n = n + 1; END; SELECT 1",
                         SqlSyntax::sqlite()),
              (Texts{"CREATE TRIGGER t AFTER INSERT ON a BEGIN UPDATE b SET n = n + 1; END", "SELECT 1"}));
}

TEST(SqlSplitterTest, CompoundBlocksCanBeDisabled) {
    SqlSyntax syntax = SqlSyntax::mysql();
    syntax.compoundBlocks = false;
    EXPECT_EQ(splitTexts("CREATE PROCEDURE p() BEGIN SELECT 1; END;", syntax),
              (Texts{"CREATE PROCEDURE p() BEGIN SELECT 1", "END"}));
}

// --- PostgreSQL ---------------------------------------------------------------------------------------

TEST(SqlSplitterTest, PostgresDollarQuotes) {
    const SqlSyntax pg = SqlSyntax::postgresql();
    EXPECT_EQ(splitTexts("CREATE FUNCTION f() RETURNS int AS $$ BEGIN RETURN 1; END; $$ LANGUAGE plpgsql; SELECT 1;",
                         pg),
              (Texts{"CREATE FUNCTION f() RETURNS int AS $$ BEGIN RETURN 1; END; $$ LANGUAGE plpgsql", "SELECT 1"}));
    EXPECT_EQ(splitTexts("DO $body$ BEGIN PERFORM 1; $x$; END $body$; SELECT 2", pg),
              (Texts{"DO $body$ BEGIN PERFORM 1; $x$; END $body$", "SELECT 2"}));
    EXPECT_EQ(splitTexts("SELECT $1, a$b FROM t; SELECT 2", pg), (Texts{"SELECT $1, a$b FROM t", "SELECT 2"}));
}

TEST(SqlSplitterTest, PostgresNestedCommentsAndStrings) {
    const SqlSyntax pg = SqlSyntax::postgresql();
    EXPECT_EQ(splitTexts("/* a /* b; */ c; */ SELECT 1; SELECT 2", pg),
              (Texts{"/* a /* b; */ c; */ SELECT 1", "SELECT 2"}));
    EXPECT_EQ(splitTexts(R"(SELECT E'a\';b'; SELECT 2)", pg), (Texts{R"(SELECT E'a\';b')", "SELECT 2"}));
    EXPECT_EQ(splitTexts(R"(SELECT 'a\'; SELECT 2)", pg), (Texts{R"(SELECT 'a\')", "SELECT 2"}));
    EXPECT_EQ(splitTexts("SELECT \"a;b\" FROM t --x; y\n; SELECT 2", pg),
              (Texts{"SELECT \"a;b\" FROM t --x; y", "SELECT 2"}));
    EXPECT_EQ(splitTexts("CREATE FUNCTION f() RETURNS int LANGUAGE sql BEGIN ATOMIC SELECT 1; END; SELECT 2", pg),
              (Texts{"CREATE FUNCTION f() RETURNS int LANGUAGE sql BEGIN ATOMIC SELECT 1; END", "SELECT 2"}));
}

// --- SQLite -------------------------------------------------------------------------------------------

TEST(SqlSplitterTest, SqliteBracketIdentifiers) {
    EXPECT_EQ(splitTexts("SELECT [a;b], [c]]d;] FROM t; SELECT 2", SqlSyntax::sqlite()),
              (Texts{"SELECT [a;b], [c]]d;] FROM t", "SELECT 2"}));
}

// --- Posições -----------------------------------------------------------------------------------------

TEST(SqlSplitterTest, ReportsOffsetLineAndColumn) {
    const auto statements = SqlSplitter::split("SELECT 1;\n  SELECT 2;\r\n\tSELECT\n3", SqlSyntax::mysql());
    ASSERT_EQ(statements.size(), 3U);
    EXPECT_EQ(statements[0].offset, 0U);
    EXPECT_EQ(statements[0].line, 1U);
    EXPECT_EQ(statements[0].column, 1U);
    EXPECT_EQ(statements[1].offset, 12U);
    EXPECT_EQ(statements[1].line, 2U);
    EXPECT_EQ(statements[1].column, 3U);
    EXPECT_EQ(statements[2].offset, 24U);
    EXPECT_EQ(statements[2].line, 3U);
    EXPECT_EQ(statements[2].column, 2U);
    EXPECT_EQ(statements[2].text, "SELECT\n3");
    EXPECT_EQ(statements[2].endOffset(), 32U);
}

TEST(SqlSplitterTest, StatementAtFindsTheStatementUnderTheCursor) {
    const auto statements = SqlSplitter::split("SELECT 1;\n\nSELECT 2;", SqlSyntax::mysql());
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statementAt(statements, 0), &statements[0]);
    EXPECT_EQ(statementAt(statements, 5), &statements[0]);
    EXPECT_EQ(statementAt(statements, 10), &statements[0]); // linha em branco entre as duas: a anterior
    EXPECT_EQ(statementAt(statements, 11), &statements[1]);
    EXPECT_EQ(statementAt(statements, 100), &statements[1]);
    EXPECT_EQ(statementAt({}, 0), nullptr);

    const auto indented = SqlSplitter::split("   SELECT 1", SqlSyntax::mysql());
    EXPECT_EQ(statementAt(indented, 1), nullptr);
}

// --- Streaming ----------------------------------------------------------------------------------------

TEST(SqlSplitterTest, AnyChunkSizeGivesTheSameResult) {
    struct Case {
        std::string_view sql;
        SqlSyntax syntax;
    };
    const std::array cases{
        Case{"SELECT 'a;b', `c;d` -- e;\n; # f;\nSELECT 2; /* g; */ SELECT \"h;\\\"i\"", SqlSyntax::mysql()},
        Case{"DELIMITER //\nCREATE PROCEDURE p() BEGIN SELECT 1; END//\nDELIMITER $$\nSELECT 2 $$\ndelimiter ;\n"
             "SELECT 3; CREATE TRIGGER t BEFORE INSERT ON x FOR EACH ROW BEGIN IF 1 THEN SET @a = 1; END IF; END;"
             " SELECT 4",
             SqlSyntax::mysql()},
        Case{"SELECT $$a;b$$; SELECT $t$ $$; $t$; /* x /* y; */ */ SELECT E'\\';'; SELECT 'ação'",
             SqlSyntax::postgresql()},
        Case{"SELECT [a;b]; CREATE TRIGGER t AFTER INSERT ON a BEGIN SELECT 1; END;\r\nSELECT 2",
             SqlSyntax::sqlite()},
    };
    for (const Case& c : cases) {
        const auto whole = SqlSplitter::split(c.sql, c.syntax);
        ASSERT_FALSE(whole.empty()) << c.sql;
        for (const std::size_t chunk : {1U, 2U, 3U, 5U, 7U, 13U, 64U}) {
            EXPECT_EQ(splitInChunks(c.sql, c.syntax, chunk), whole) << "bloco de " << chunk << " bytes: " << c.sql;
        }
    }
}

TEST(SqlSplitterTest, MemoryStaysConstantOnLargeInput) {
    constexpr std::string_view kLine = "INSERT INTO t (a, b) VALUES (1, 'texto; com ponto e vírgula');\n";
    constexpr std::size_t kTotal = std::size_t{16} * 1024 * 1024;
    constexpr std::size_t kChunk = std::size_t{64} * 1024;

    std::string chunk;
    while (chunk.size() + kLine.size() <= kChunk) {
        chunk += kLine;
    }
    const std::size_t linesPerChunk = chunk.size() / kLine.size();

    SqlSplitter splitter(SqlSyntax::mysql());
    std::size_t count = 0;
    std::size_t maxBuffered = 0;
    const SqlSplitter::Sink sink = [&count](SqlStatement&&) { ++count; };
    std::size_t fed = 0;
    while (fed < kTotal) {
        splitter.feed(chunk, sink);
        fed += chunk.size();
        maxBuffered = std::max(maxBuffered, splitter.bufferedBytes());
    }
    splitter.finish(sink);

    EXPECT_EQ(count, (fed / chunk.size()) * linesPerChunk);
    EXPECT_LT(maxBuffered, kLine.size()); // só o "\n" final de cada bloco fica retido
}

TEST(SqlSplitterTest, HugeTokenAcrossManySmallChunks) {
    const std::string value(std::size_t{4} * 1024 * 1024, 'x');
    const std::string sql = "INSERT INTO t VALUES ('" + value + "'); SELECT 1";
    const auto statements = splitInChunks(sql, SqlSyntax::mysql(), 1024);
    ASSERT_EQ(statements.size(), 2U);
    EXPECT_EQ(statements[0].text.size(), sql.find(';'));
    EXPECT_EQ(statements[1].text, "SELECT 1");
}

TEST(SqlSplitterTest, FinishResetsForReuse) {
    SqlSplitter splitter(SqlSyntax::mysql());
    std::vector<SqlStatement> statements;
    const SqlSplitter::Sink sink = [&statements](SqlStatement&& s) { statements.push_back(std::move(s)); };
    splitter.feed("DELIMITER //\nSELECT 1//\nSELECT 2", sink);
    splitter.finish(sink);
    EXPECT_EQ(splitter.delimiter(), ";");
    splitter.feed("SELECT 3; SELECT 4", sink);
    splitter.finish(sink);

    ASSERT_EQ(statements.size(), 4U);
    EXPECT_EQ(statements[2].text, "SELECT 3");
    EXPECT_EQ(statements[2].offset, 0U);
    EXPECT_EQ(statements[2].line, 1U);
}

} // namespace
} // namespace mydb::core::sql
