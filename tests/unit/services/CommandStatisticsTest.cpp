#include "services/admin/CommandStatistics.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using mydb::core::ServerVariable;
using mydb::services::admin::commandStatistics;

TEST(CommandStatisticsTest, ComputesRatesAndShares) {
    const std::vector<ServerVariable> status{
        {.name = "Uptime", .value = "7200", .global = true},
        {.name = "Com_select", .value = "300", .global = true},
        {.name = "Com_alter_table", .value = "100", .global = true},
        {.name = "Com_insert", .value = "0", .global = true},
        {.name = "Questions", .value = "999", .global = true},
    };
    const auto statistics = commandStatistics(status);
    ASSERT_EQ(statistics.size(), 2U);
    EXPECT_EQ(statistics[0].command, "select");
    EXPECT_EQ(statistics[0].total, 300U);
    EXPECT_DOUBLE_EQ(statistics[0].perHour, 150.0);
    EXPECT_DOUBLE_EQ(statistics[0].perSecond, 300.0 / 7200.0);
    EXPECT_DOUBLE_EQ(statistics[0].percent, 75.0);
    EXPECT_EQ(statistics[1].command, "alter table");
    EXPECT_DOUBLE_EQ(statistics[1].percent, 25.0);
}

TEST(CommandStatisticsTest, WithoutUptimeRatesStayZero) {
    const std::vector<ServerVariable> status{{.name = "Com_select", .value = "5", .global = true}};
    const auto statistics = commandStatistics(status);
    ASSERT_EQ(statistics.size(), 1U);
    EXPECT_DOUBLE_EQ(statistics[0].perHour, 0.0);
    EXPECT_DOUBLE_EQ(statistics[0].percent, 100.0);
}

} // namespace
