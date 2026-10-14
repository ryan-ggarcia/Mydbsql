#include "core/errors/DbError.hpp"

#include <gtest/gtest.h>

namespace mydb::core {
namespace {

TEST(DbErrorTest, KeepsAllFieldsFromInfo) {
    const QueryError error(
        DbErrorInfo{.message = "Table 'x' doesn't exist", .code = 1146, .sqlState = "42S02", .hint = "Confira o nome"});

    EXPECT_STREQ(error.what(), "Table 'x' doesn't exist");
    EXPECT_EQ(error.code(), 1146);
    EXPECT_EQ(error.sqlState(), "42S02");
    EXPECT_EQ(error.hint(), "Confira o nome");
}

TEST(DbErrorTest, MessageOnlyConstructorLeavesOtherFieldsEmpty) {
    const ConnectionError error("Connection refused");

    EXPECT_STREQ(error.what(), "Connection refused");
    EXPECT_EQ(error.code(), 0);
    EXPECT_TRUE(error.sqlState().empty());
    EXPECT_TRUE(error.hint().empty());
}

TEST(DbErrorTest, HierarchyCanBeCaughtByBaseClass) {
    EXPECT_THROW(throw ConnectionError("x"), DbError);
    EXPECT_THROW(throw QueryError("x"), DbError);
    EXPECT_THROW(throw CancelledError(), DbError);
    EXPECT_THROW(throw CancelledError(), std::runtime_error);
}

TEST(DbErrorTest, CancelledErrorIsDistinguishableFromQueryError) {
    try {
        throw CancelledError();
    } catch (const QueryError&) {
        FAIL() << "CancelledError não deve ser um QueryError";
    } catch (const CancelledError& error) {
        EXPECT_FALSE(std::string(error.what()).empty());
    }
}

} // namespace
} // namespace mydb::core
