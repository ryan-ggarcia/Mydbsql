// Custo das leituras que o --measure-startup e o teste de "nenhum driver carregado" fazem.

#include "infra/platform/IPlatform.hpp"

#include <benchmark/benchmark.h>

namespace {

using mydb::infra::platform::createPlatform;

void BM_ProcessMemory(benchmark::State& state) {
    const auto platform = createPlatform();
    for (auto _ : state) {
        benchmark::DoNotOptimize(platform->processMemory());
    }
}
BENCHMARK(BM_ProcessMemory);

void BM_LoadedLibraries(benchmark::State& state) {
    const auto platform = createPlatform();
    for (auto _ : state) {
        auto libraries = platform->loadedLibraries();
        benchmark::DoNotOptimize(libraries.data());
    }
}
BENCHMARK(BM_LoadedLibraries);

} // namespace
