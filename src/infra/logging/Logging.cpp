#include "infra/logging/Logging.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace mydb::infra::logging {

namespace {

constexpr std::size_t kQueueSize = 8192;
constexpr std::size_t kMaxFileBytes = std::size_t{5} * 1024 * 1024;
constexpr std::size_t kMaxFiles = 3;

} // namespace

void initialize(const LogOptions& options) noexcept {
    try {
        std::vector<spdlog::sink_ptr> sinks;
        std::string fileError;
        if (options.directory) {
            try {
                std::filesystem::create_directories(*options.directory);
                // spdlog recebe std::string: no Windows, um caminho fora da página de código ANSI lança
                // aqui e o log segue só no console.
                sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    (*options.directory / "mydbsql.log").string(), kMaxFileBytes, kMaxFiles));
            } catch (const std::exception& error) {
                fileError = error.what();
            }
        }
        if (options.console || sinks.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
        }

        spdlog::init_thread_pool(kQueueSize, 1);
        auto logger = std::make_shared<spdlog::async_logger>("mydb", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                             spdlog::async_overflow_policy::overrun_oldest);
        logger->set_level(options.verbose ? spdlog::level::debug : spdlog::level::info);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(std::move(logger));

        if (!fileError.empty()) {
            spdlog::warn("Log em arquivo desativado: {}", fileError);
        }
    } catch (...) {
        // O app continua com o logger padrão do spdlog; o stderr é o único lugar que sobra para avisar.
        std::fputs("mydbsql: falha ao configurar o log\n", stderr);
    }
}

void shutdown() noexcept {
    try {
        spdlog::shutdown();
    } catch (...) {
        std::fputs("mydbsql: falha ao encerrar o log\n", stderr);
    }
}

} // namespace mydb::infra::logging
