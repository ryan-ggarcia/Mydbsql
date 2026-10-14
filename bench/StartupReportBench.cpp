#include "app/StartupReport.hpp"

#include <benchmark/benchmark.h>

namespace {

void BM_StartupReportToJson(benchmark::State& state) {
    mydb::app::StartupReport report;
    report.platform = "linux";
    report.version = "0.1.0";
    report.startupMs = 123.4;
    report.rssKb = 40000;
    report.anonKb = 20000;
    report.libsLoaded = 57;
    for (auto _ : state) {
        benchmark::DoNotOptimize(mydb::app::toJson(report));
    }
}
BENCHMARK(BM_StartupReportToJson);

} // namespace
