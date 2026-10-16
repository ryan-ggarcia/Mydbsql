#include "services/log/LogBuffer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace {

using mydb::core::LogCategory;
using mydb::services::log::LogBuffer;
using mydb::services::log::SessionLogger;

TEST(LogBufferTest, KeepsOnlyTheNewestEntries) {
    LogBuffer buffer(3);
    for (int i = 1; i <= 5; ++i) {
        buffer.append(LogCategory::Info, {}, std::to_string(i));
    }
    const auto entries = buffer.since(0);
    ASSERT_EQ(entries.size(), 3U);
    EXPECT_EQ(entries.front().text, "3");
    EXPECT_EQ(entries.front().sequence, 3U);
    EXPECT_EQ(entries.back().text, "5");
    EXPECT_EQ(buffer.since(4).size(), 1U);
    EXPECT_TRUE(buffer.since(5).empty());
}

TEST(LogBufferTest, ListenerIsCalledOncePerBurst) {
    LogBuffer buffer;
    int calls = 0;
    buffer.setListener([&calls] { ++calls; });
    buffer.append(LogCategory::Sql, "s1", "SELECT 1");
    buffer.append(LogCategory::Sql, "s1", "SELECT 2");
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(buffer.since(0).size(), 2U);
    buffer.append(LogCategory::Sql, "s1", "SELECT 3");
    EXPECT_EQ(calls, 2);
}

TEST(LogBufferTest, LongTextIsCutWithoutBreakingUtf8) {
    LogBuffer buffer;
    std::string text(LogBuffer::kMaxTextLength - 1, 'a');
    text += "ção e mais texto";
    buffer.append(LogCategory::UserSql, {}, text);
    const std::string stored = buffer.since(0).at(0).text;
    EXPECT_EQ(stored, std::string(LogBuffer::kMaxTextLength - 1, 'a') + "…");
}

TEST(LogBufferTest, SessionLoggerTagsTheSession) {
    LogBuffer buffer;
    SessionLogger logger(buffer, "s9");
    mydb::core::IQueryLogger& asInterface = logger;
    asInterface.log(LogCategory::Sql, "SHOW DATABASES");
    const auto entries = buffer.since(0);
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].sessionId, "s9");
    EXPECT_EQ(entries[0].category, LogCategory::Sql);
}

TEST(LogBufferTest, ConcurrentWritersKeepSequencesUnique) {
    LogBuffer buffer(10000);
    std::vector<std::jthread> writers;
    for (int t = 0; t < 4; ++t) {
        writers.emplace_back([&buffer] {
            for (int i = 0; i < 500; ++i) {
                buffer.append(LogCategory::Debug, {}, "x");
            }
        });
    }
    writers.clear();
    const auto entries = buffer.since(0);
    ASSERT_EQ(entries.size(), 2000U);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].sequence, i + 1);
    }
}

} // namespace
