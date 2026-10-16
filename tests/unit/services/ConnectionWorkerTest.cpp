#include "services/session/ConnectionWorker.hpp"

#include "support/FakeServer.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace {

using mydb::services::session::ConnectionWorker;
using mydb::tests::FakeServer;
using mydb::tests::FakeServerConnection;
using namespace std::chrono_literals;

template <typename Predicate>
bool eventually(Predicate condition, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

TEST(ConnectionWorkerTest, RunsJobsInOrderOnItsOwnThread) {
    FakeServer server;
    std::vector<int> order;
    std::promise<std::thread::id> threadId;
    {
        ConnectionWorker worker(std::make_unique<FakeServerConnection>(server), 0ms);
        for (int i = 0; i < 5; ++i) {
            worker.post([&order, i](mydb::core::IConnection&) { order.push_back(i); });
        }
        worker.post([&threadId](mydb::core::IConnection&) { threadId.set_value(std::this_thread::get_id()); });
        auto future = threadId.get_future();
        ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
        EXPECT_NE(future.get(), std::this_thread::get_id());
    }
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
    EXPECT_TRUE(server.closed);
}

TEST(ConnectionWorkerTest, ExceptionsFromJobsDoNotStopTheWorker) {
    FakeServer server;
    std::atomic<bool> ran{false};
    ConnectionWorker worker(std::make_unique<FakeServerConnection>(server), 0ms);
    worker.post([](mydb::core::IConnection&) { throw std::runtime_error("falhou"); });
    worker.post([&ran](mydb::core::IConnection&) { ran = true; });
    EXPECT_TRUE(eventually([&] { return ran.load(); }));
}

TEST(ConnectionWorkerTest, PingsAfterIdlePeriodWhenKeepAliveIsOn) {
    FakeServer server;
    server.pingResult = false;
    std::atomic<int> observed{0};
    std::atomic<bool> lastAlive{true};
    ConnectionWorker worker(std::make_unique<FakeServerConnection>(server), 10ms, [&](bool alive) {
        lastAlive = alive;
        ++observed;
    });
    EXPECT_TRUE(eventually([&] { return observed.load() >= 2; }));
    EXPECT_FALSE(lastAlive);
    EXPECT_GE(server.pingCalls.load(), 2);
}

TEST(ConnectionWorkerTest, NoPingWhenKeepAliveIsOff) {
    FakeServer server;
    {
        ConnectionWorker worker(std::make_unique<FakeServerConnection>(server), 0ms);
        std::this_thread::sleep_for(30ms);
    }
    EXPECT_EQ(server.pingCalls.load(), 0);
}

TEST(ConnectionWorkerTest, CancelInterruptsOnlyTheRunningJob) {
    FakeServer server;
    server.blockNext = true;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> nextRan{false};
    ConnectionWorker worker(std::make_unique<FakeServerConnection>(server), 0ms);

    worker.cancelCurrent(); // nada rodando: não chega à conexão

    worker.post([&cancelled](mydb::core::IConnection& connection) {
        try {
            (void)connection.execute("SELECT SLEEP(100)");
        } catch (const mydb::core::CancelledError&) {
            cancelled = true;
        }
    });
    worker.post([&nextRan](mydb::core::IConnection&) { nextRan = true; });
    ASSERT_TRUE(server.waitBlocked(5s));
    EXPECT_EQ(worker.pending(), 1U);
    worker.cancelCurrent();
    EXPECT_TRUE(eventually([&] { return cancelled.load() && nextRan.load(); }));
    EXPECT_EQ(server.cancelCalls.load(), 1);
}

TEST(ConnectionWorkerTest, DestructorDropsPendingJobsAndCancelsTheRunningOne) {
    FakeServer server;
    server.blockNext = true;
    std::atomic<bool> pendingRan{false};
    {
        ConnectionWorker worker(std::make_unique<FakeServerConnection>(server), 0ms);
        worker.post([](mydb::core::IConnection& connection) {
            try {
                (void)connection.execute("SELECT SLEEP(100)");
            } catch (const mydb::core::CancelledError&) {
            }
        });
        worker.post([&pendingRan](mydb::core::IConnection&) { pendingRan = true; });
        ASSERT_TRUE(server.waitBlocked(5s));
    }
    EXPECT_FALSE(pendingRan);
    EXPECT_EQ(server.cancelCalls.load(), 1);
    EXPECT_TRUE(server.closed);
}

} // namespace
