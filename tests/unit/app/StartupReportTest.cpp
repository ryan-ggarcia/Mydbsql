#include "app/StartupReport.hpp"

#include <gtest/gtest.h>

namespace mydb::app {
namespace {

TEST(StartupReportTest, SerializesTheFieldsTheBudgetCheckerReads) {
    StartupReport report;
    report.platform = "linux";
    report.version = "0.1.0";
    report.startupMs = 123.456;
    report.rssKb = 40000;
    report.anonKb = 20000;
    report.libsLoaded = 57;
    report.debugBuild = false;

    EXPECT_EQ(toJson(report), "{\"platform\":\"linux\",\"version\":\"0.1.0\",\"startup_ms\":123.5,"
                              "\"rss_kb\":40000,\"anon_kb\":20000,\"libs_loaded\":57,\"debug_build\":false}");
}

TEST(StartupReportTest, EscapesStrings) {
    StartupReport report;
    report.platform = "a\"b\\c\n";
    const std::string json = toJson(report);
    EXPECT_NE(json.find(R"("platform":"a\"b\\c\n")"), std::string::npos) << json;
}

} // namespace
} // namespace mydb::app
