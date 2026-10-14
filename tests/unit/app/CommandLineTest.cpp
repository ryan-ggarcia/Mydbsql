#include "app/CommandLine.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace mydb::app {
namespace {

CommandLineOptions parse(std::vector<const char*> args) {
    args.insert(args.begin(), "mydbsql");
    return parseCommandLine(args);
}

TEST(CommandLineTest, NoArgumentsMeansNormalStartup) {
    const auto options = parse({});
    EXPECT_FALSE(options.measureStartup);
    EXPECT_FALSE(options.measureOutput.has_value());
    EXPECT_EQ(options.settleMs, 500);
    EXPECT_TRUE(options.error.empty());
}

TEST(CommandLineTest, MeasureStartupToStdout) {
    const auto options = parse({"--measure-startup"});
    EXPECT_TRUE(options.measureStartup);
    EXPECT_FALSE(options.measureOutput.has_value());
}

TEST(CommandLineTest, MeasureStartupToFile) {
    const auto options = parse({"--measure-startup=out/run1.json"});
    EXPECT_TRUE(options.measureStartup);
    ASSERT_TRUE(options.measureOutput.has_value());
    EXPECT_EQ(options.measureOutput->generic_string(), "out/run1.json");
}

TEST(CommandLineTest, EmptyMeasureFileIsAnError) {
    EXPECT_FALSE(parse({"--measure-startup="}).error.empty());
}

TEST(CommandLineTest, SettleMsAcceptsRangeAndRejectsGarbage) {
    EXPECT_EQ(parse({"--settle-ms=0"}).settleMs, 0);
    EXPECT_EQ(parse({"--settle-ms=1500"}).settleMs, 1500);
    EXPECT_FALSE(parse({"--settle-ms=-1"}).error.empty());
    EXPECT_FALSE(parse({"--settle-ms=12abc"}).error.empty());
    EXPECT_FALSE(parse({"--settle-ms=999999"}).error.empty());
}

TEST(CommandLineTest, QtAndUnknownArgumentsAreIgnored) {
    const auto options = parse({"-platform", "offscreen", "--style=fusion", "--measure-startupx"});
    EXPECT_FALSE(options.measureStartup);
    EXPECT_TRUE(options.error.empty());
}

} // namespace
} // namespace mydb::app
