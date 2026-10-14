#include "app/CommandLine.hpp"

#include <charconv>
#include <string_view>

namespace mydb::app {

namespace {

constexpr std::string_view kMeasureStartup = "--measure-startup";
constexpr std::string_view kSettleMs = "--settle-ms=";
constexpr int kMaxSettleMs = 60'000;

} // namespace

CommandLineOptions parseCommandLine(std::span<const char* const> args) {
    CommandLineOptions options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == nullptr) {
            continue;
        }
        const std::string_view arg(args[i]);
        if (arg == kMeasureStartup) {
            options.measureStartup = true;
        } else if (arg.starts_with(kMeasureStartup) && arg.size() > kMeasureStartup.size() &&
                   arg[kMeasureStartup.size()] == '=') {
            options.measureStartup = true;
            const std::string_view file = arg.substr(kMeasureStartup.size() + 1);
            if (file.empty()) {
                options.error = "--measure-startup= precisa de um caminho de arquivo";
            } else {
                options.measureOutput = std::filesystem::path(std::u8string(file.begin(), file.end()));
            }
        } else if (arg.starts_with(kSettleMs)) {
            const std::string_view value = arg.substr(kSettleMs.size());
            int parsed = -1;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size() || parsed < 0 || parsed > kMaxSettleMs) {
                options.error = "--settle-ms precisa de um inteiro entre 0 e 60000";
            } else {
                options.settleMs = parsed;
            }
        }
    }
    return options;
}

} // namespace mydb::app
