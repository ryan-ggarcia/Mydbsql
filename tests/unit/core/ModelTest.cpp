#include "core/model/Column.hpp"
#include "core/model/DataType.hpp"
#include "core/model/ForeignKey.hpp"
#include "core/model/TableDefinition.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mydb::core::model {
namespace {

Column column(std::string name, bool nullable) {
    Column c;
    c.name = std::move(name);
    c.typeName = "INT";
    c.nullable = nullable;
    return c;
}

Index index(Index::Kind kind, std::vector<std::string> names) {
    Index i;
    i.kind = kind;
    for (auto& name : names) {
        i.columns.push_back(IndexColumn{.name = std::move(name), .prefixLength = {}, .descending = false});
    }
    return i;
}

TEST(ModelTest, ColumnFullType) {
    Column c;
    c.typeName = "DECIMAL";
    EXPECT_EQ(c.fullType(), "DECIMAL");
    c.lengthSet = "10,2";
    c.isUnsigned = true;
    EXPECT_EQ(c.fullType(), "DECIMAL(10,2) UNSIGNED");
}

TEST(ModelTest, FindColumnIgnoresCase) {
    TableDefinition table;
    table.columns = {column("Id", false), column("nome", true)};
    ASSERT_NE(table.findColumn("ID"), nullptr);
    EXPECT_EQ(table.findColumn("ID")->name, "Id");
    EXPECT_EQ(table.findColumn("NOME"), &table.columns[1]);
    EXPECT_EQ(table.findColumn("outra"), nullptr);
}

TEST(ModelTest, RowIdentityPrefersPrimaryKey) {
    TableDefinition table;
    table.columns = {column("a", false), column("b", false)};
    table.indexes = {index(Index::Kind::Unique, {"b"}), index(Index::Kind::Primary, {"a"})};
    ASSERT_NE(table.primaryKey(), nullptr);
    EXPECT_EQ(table.rowIdentityColumns(), (std::vector<std::string>{"a"}));
}

TEST(ModelTest, RowIdentityFallsBackToNotNullUniqueKey) {
    TableDefinition table;
    table.columns = {column("a", true), column("b", false), column("c", false)};
    table.indexes = {index(Index::Kind::Key, {"b"}), index(Index::Kind::Unique, {"a"}),
                     index(Index::Kind::Unique, {"b", "c"})};
    EXPECT_EQ(table.primaryKey(), nullptr);
    EXPECT_EQ(table.rowIdentityColumns(), (std::vector<std::string>{"b", "c"}));
}

TEST(ModelTest, RowIdentityIsEmptyWithoutAUsableKey) {
    TableDefinition table;
    table.columns = {column("a", true), column("b", false)};
    Index prefixed = index(Index::Kind::Unique, {"b"});
    prefixed.columns[0].prefixLength = 10;
    table.indexes = {index(Index::Kind::Unique, {"a"}), prefixed, index(Index::Kind::Unique, {"inexistente"})};
    EXPECT_TRUE(table.rowIdentityColumns().empty());
}

TEST(ModelTest, EnumsToText) {
    EXPECT_EQ(toSql(ReferentialAction::NoAction), "NO ACTION");
    EXPECT_EQ(toSql(ReferentialAction::Restrict), "RESTRICT");
    EXPECT_EQ(toSql(ReferentialAction::Cascade), "CASCADE");
    EXPECT_EQ(toSql(ReferentialAction::SetNull), "SET NULL");
    EXPECT_EQ(toSql(ReferentialAction::SetDefault), "SET DEFAULT");
    EXPECT_EQ(toString(DataTypeCategory::Integer), "integer");
    EXPECT_EQ(toString(DataTypeCategory::Real), "real");
    EXPECT_EQ(toString(DataTypeCategory::Text), "text");
    EXPECT_EQ(toString(DataTypeCategory::Binary), "binary");
    EXPECT_EQ(toString(DataTypeCategory::Temporal), "temporal");
    EXPECT_EQ(toString(DataTypeCategory::Spatial), "spatial");
    EXPECT_EQ(toString(DataTypeCategory::Other), "other");
}

} // namespace
} // namespace mydb::core::model
