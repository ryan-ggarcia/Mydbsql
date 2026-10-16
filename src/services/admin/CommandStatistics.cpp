#include "services/admin/CommandStatistics.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>

namespace mydb::services::admin {

namespace {

std::optional<std::uint64_t> toNumber(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

std::string commandName(std::string_view variable) {
    std::string name(variable);
    std::ranges::transform(name, name.begin(),
                           [](unsigned char c) { return c == '_' ? ' ' : static_cast<char>(std::tolower(c)); });
    return name;
}

} // namespace

std::vector<CommandStatistic> commandStatistics(std::span<const core::ServerVariable> status) {
    constexpr std::string_view kPrefix = "Com_";
    constexpr double kSecondsPerHour = 3600.0;
    constexpr double kPercent = 100.0;

    std::uint64_t uptime = 0;
    std::vector<CommandStatistic> statistics;
    for (const core::ServerVariable& variable : status) {
        if (variable.name == "Uptime") {
            uptime = toNumber(variable.value).value_or(0);
            continue;
        }
        if (!startsWithNoCase(variable.name, kPrefix)) {
            continue;
        }
        const std::uint64_t total = toNumber(variable.value).value_or(0);
        if (total > 0) {
            statistics.push_back(CommandStatistic{
                .command = commandName(std::string_view(variable.name).substr(kPrefix.size())), .total = total});
        }
    }

    std::uint64_t sum = 0;
    for (const CommandStatistic& statistic : statistics) {
        sum += statistic.total;
    }
    for (CommandStatistic& statistic : statistics) {
        const auto total = static_cast<double>(statistic.total);
        if (uptime > 0) {
            statistic.perSecond = total / static_cast<double>(uptime);
            statistic.perHour = statistic.perSecond * kSecondsPerHour;
        }
        statistic.percent = total * kPercent / static_cast<double>(sum);
    }
    std::ranges::sort(statistics, [](const CommandStatistic& a, const CommandStatistic& b) {
        return a.total != b.total ? a.total > b.total : a.command < b.command;
    });
    return statistics;
}

} // namespace mydb::services::admin
