// Autocomplete (CLAUDE.md §2.3, regra 5): responder em ≤ 10 ms com 10 mil tabelas no banco atual.

#include "services/query/CompletionIndex.hpp"

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

using mydb::core::model::Column;
using mydb::core::model::DbObject;
using mydb::core::model::ObjectRef;
using mydb::core::model::ObjectType;
using mydb::core::model::TableDefinition;
using mydb::services::query::CompletionIndex;
using mydb::services::query::SqlFunction;

constexpr int kTables = 10'000;
constexpr int kTablesWithColumns = 500;
constexpr int kColumns = 30;
constexpr int kFunctions = 250;

std::string tableName(int i) {
    return "table_" + std::to_string(i);
}

const CompletionIndex& bigIndex() {
    static const CompletionIndex index = [] {
        CompletionIndex built(mydb::core::sql::SqlSyntax::mysql());
        built.setKeywords(mydb::services::query::sqlKeywords("mysql"));
        std::vector<SqlFunction> functions;
        for (int i = 0; i < kFunctions; ++i) {
            functions.push_back(
                SqlFunction{.name = "FUNC_" + std::to_string(i), .signature = "F(x)", .description = "f"});
        }
        built.setFunctions(std::move(functions));
        built.setDatabases({"big", "other"}, "big");
        std::vector<DbObject> objects;
        objects.reserve(kTables);
        for (int i = 0; i < kTables; ++i) {
            DbObject object;
            object.ref = ObjectRef{.database = "big", .schema = {}, .name = tableName(i), .type = ObjectType::Table};
            objects.push_back(std::move(object));
        }
        built.setObjects("big", objects);
        for (int i = 0; i < kTablesWithColumns; ++i) {
            TableDefinition table;
            table.ref = ObjectRef{.database = "big", .schema = {}, .name = tableName(i), .type = ObjectType::Table};
            for (int c = 0; c < kColumns; ++c) {
                Column column;
                column.name = "column_" + std::to_string(c);
                column.typeName = "INT";
                table.columns.push_back(std::move(column));
            }
            built.setColumns(table);
        }
        return built;
    }();
    return index;
}

constexpr std::string_view kQuery =
    "SELECT a.column_1, b.column_2, \nFROM table_1 a\nJOIN table_2 b ON b.column_0 = a.column_0\n"
    "LEFT JOIN big.table_3 AS c ON c.column_0 = a.column_0\nWHERE a.column_5 > 10 AND ";

void complete(benchmark::State& state, const std::string& sql, std::size_t cursor) {
    const CompletionIndex& index = bigIndex();
    for (auto _ : state) {
        benchmark::DoNotOptimize(index.complete(sql, cursor));
    }
}

/// Pior caso: nada digitado, todos os candidatos (10 mil tabelas, palavras-chave, funções e colunas citadas).
void BM_CompletionEverything10k(benchmark::State& state) {
    const std::string sql(kQuery);
    complete(state, sql, sql.size());
}

void BM_CompletionPrefix10k(benchmark::State& state) {
    const std::string sql = std::string(kQuery) + "table_9";
    complete(state, sql, sql.size());
}

void BM_CompletionAliasColumns10k(benchmark::State& state) {
    const std::string sql = std::string(kQuery) + "c.col";
    complete(state, sql, sql.size());
}

} // namespace

BENCHMARK(BM_CompletionEverything10k)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CompletionPrefix10k)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CompletionAliasColumns10k)->Unit(benchmark::kMillisecond);
