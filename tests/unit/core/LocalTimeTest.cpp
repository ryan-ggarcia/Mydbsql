#include "core/model/LocalTime.hpp"

#include <gtest/gtest.h>

namespace {

using mydb::core::model::formatUtcOffset;
using mydb::core::model::localUtcOffset;
using namespace std::chrono_literals;

TEST(LocalTimeTest, FormatsOffsetsForSql) {
    EXPECT_EQ(formatUtcOffset(0min), "+00:00");
    EXPECT_EQ(formatUtcOffset(-180min), "-03:00");
    EXPECT_EQ(formatUtcOffset(330min), "+05:30");
    EXPECT_EQ(formatUtcOffset(-45min), "-00:45");
    EXPECT_EQ(formatUtcOffset(840min), "+14:00");
}

TEST(LocalTimeTest, LocalOffsetIsAPlausibleTimeZone) {
    const auto offset = localUtcOffset(std::chrono::system_clock::now());
    EXPECT_GE(offset, -12h);
    EXPECT_LE(offset, 14h);
    EXPECT_EQ(offset.count() % 15, 0);
}

} // namespace
