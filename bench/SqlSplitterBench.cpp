// Divisão de scripts em streaming (CLAUDE.md §5, Fase 1): 100 MB em blocos de 64 KB, com a memória
// retida pelo splitter publicada como contador para provar que não cresce com a entrada.

#include "core/sql/SqlSplitter.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using mydb::core::sql::SqlSplitter;
using mydb::core::sql::SqlStatement;
using mydb::core::sql::SqlSyntax;

constexpr std::size_t kChunkSize = std::size_t{64} * 1024;

/// Um bloco de 64 KB com uma mistura realista: INSERTs, comentários, strings com ";" e uma procedure.
std::string makeChunk() {
    constexpr std::string_view kStatements[] = {
        "INSERT INTO cliente (id, nome, obs) VALUES (1, 'Maria; da Silva', 'it''s ok');\n",
        "-- comentário com ; dentro\nUPDATE cliente SET nome = \"x\" WHERE id = 1;\n",
        "/* bloco\n   ; */ SELECT `a;b`, COUNT(*) FROM t GROUP BY 1;\n",
        "CREATE PROCEDURE p() BEGIN DECLARE x INT; IF x THEN SELECT 1; END IF; END;\n",
    };
    std::string chunk;
    chunk.reserve(kChunkSize);
    for (std::size_t i = 0;; ++i) {
        const std::string_view next = kStatements[i % std::size(kStatements)];
        if (chunk.size() + next.size() > kChunkSize) {
            break;
        }
        chunk += next;
    }
    return chunk;
}

void BM_SplitStreaming(benchmark::State& state) {
    const std::string chunk = makeChunk();
    const auto totalBytes = static_cast<std::size_t>(state.range(0)) * 1024 * 1024;
    std::size_t statements = 0;
    std::size_t maxBuffered = 0;
    const SqlSplitter::Sink sink = [&statements](SqlStatement&& statement) {
        benchmark::DoNotOptimize(statement.text.data());
        ++statements;
    };

    for (auto _ : state) {
        SqlSplitter splitter(SqlSyntax::mysql());
        for (std::size_t fed = 0; fed < totalBytes; fed += chunk.size()) {
            splitter.feed(chunk, sink);
            maxBuffered = std::max(maxBuffered, splitter.bufferedBytes());
        }
        splitter.finish(sink);
    }

    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * totalBytes));
    state.counters["statements"] = static_cast<double>(statements) / static_cast<double>(state.iterations());
    state.counters["max_buffered_kb"] = static_cast<double>(maxBuffered) / 1024.0;
}
BENCHMARK(BM_SplitStreaming)->Arg(100)->Unit(benchmark::kMillisecond)->Iterations(1);

void BM_SplitEditorText(benchmark::State& state) {
    // O caso do editor: texto inteiro na memória, dividido a cada "executar query atual".
    const std::string text = makeChunk();
    for (auto _ : state) {
        auto statements = SqlSplitter::split(text, SqlSyntax::mysql());
        benchmark::DoNotOptimize(statements.data());
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations() * text.size()));
}
BENCHMARK(BM_SplitEditorText);

} // namespace
