#include "app/Application.hpp"
#include "app/CommandLine.hpp"
#include "app/StartupProbe.hpp"
#include "infra/logging/Logging.hpp"
#include "infra/platform/IPlatform.hpp"
#include "ui/main/MainWindow.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

namespace {

constexpr bool kDebugBuild = MYDB_DEBUG_BUILD != 0;

} // namespace

namespace {

int run(int argc, char** argv, std::chrono::steady_clock::time_point startedAt) {
    namespace platform = mydb::infra::platform;
    namespace logging = mydb::infra::logging;

    const std::vector<const char*> args(argv, argv + argc); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto options = mydb::app::parseCommandLine(args);
    if (!options.error.empty()) {
        std::fprintf(stderr, "mydbsql: %s\n", options.error.c_str());
        return 2;
    }

    const std::unique_ptr<platform::IPlatform> os = platform::createPlatform();
    logging::initialize({.directory = os->standardDirectory(platform::StandardDirectory::Cache) / "logs",
                         .console = kDebugBuild,
                         .verbose = kDebugBuild});

    int exitCode = 0;
    {
        mydb::app::Application app(argc, argv, /*enableWatchdog=*/kDebugBuild);
        mydb::ui::MainWindow window;

        std::unique_ptr<mydb::app::StartupProbe> probe;
        if (options.measureStartup) {
            probe = std::make_unique<mydb::app::StartupProbe>(*os, startedAt, options);
        }

        window.show();
        if (probe) {
            probe->attach(window);
        }
        spdlog::info("Mydbsql {} iniciado ({})", MYDB_VERSION, os->name());
        exitCode = mydb::app::Application::exec();
    }

    logging::shutdown();
    return exitCode;
}

} // namespace

int main(int argc, char* argv[]) {
    // Marco zero do --measure-startup (§2.1): o mais cedo possível.
    const auto startedAt = std::chrono::steady_clock::now();
    try {
        return run(argc, argv, startedAt);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "mydbsql: erro fatal: %s\n", error.what());
    } catch (...) {
        std::fprintf(stderr, "mydbsql: erro fatal desconhecido\n");
    }
    mydb::infra::logging::shutdown();
    return 1;
}
