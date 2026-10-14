#include "sdk/EventBus.hpp"
#include "sdk/IModule.hpp"
#include "sdk/Registry.hpp"
#include "sdk/Version.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mydb::sdk {
namespace {

// --- Version ------------------------------------------------------------------------------------------

TEST(VersionTest, CompatibilityRules) {
    const Version host{.major = 1, .minor = 3};
    EXPECT_TRUE(isCompatible(host, {.major = 1, .minor = 0}));
    EXPECT_TRUE(isCompatible(host, {.major = 1, .minor = 3}));
    EXPECT_FALSE(isCompatible(host, {.major = 1, .minor = 4})); // usa algo que o host não tem
    EXPECT_FALSE(isCompatible(host, {.major = 0, .minor = 9}));
    EXPECT_FALSE(isCompatible(host, {.major = 2, .minor = 0}));
    EXPECT_EQ(host.toString(), "1.3");
    EXPECT_LT((Version{.major = 1, .minor = 2}), host);
}

TEST(VersionTest, ModuleFileStem) {
    EXPECT_EQ(moduleFileStem("example.hello"), "mydb_example_hello");
    EXPECT_EQ(moduleFileStem("driver.mysql"), "mydb_driver_mysql");
}

// --- Registry -----------------------------------------------------------------------------------------

struct IGreeter {
    virtual ~IGreeter() = default;
    [[nodiscard]] virtual std::string greet() const = 0;
};

struct Greeter final : IGreeter {
    explicit Greeter(std::string text) : text(std::move(text)) {}
    [[nodiscard]] std::string greet() const override { return text; }
    std::string text;
};

TEST(RegistryTest, RegistersFactoriesAndCreatesLazily) {
    Registry<IGreeter> registry;
    int created = 0;
    registry.add("pt", "Português", [&created] {
        ++created;
        return std::make_unique<Greeter>("olá");
    });
    registry.add("en", "English", [] { return std::make_unique<Greeter>("hello"); });

    EXPECT_EQ(created, 0); // registrar não cria nada (§2.2)
    EXPECT_EQ(registry.size(), 2U);
    EXPECT_EQ(registry.entries()[0].id, "pt");
    ASSERT_NE(registry.find("en"), nullptr);
    EXPECT_EQ(registry.find("en")->displayName, "English");
    EXPECT_EQ(registry.find("fr"), nullptr);

    EXPECT_EQ(registry.create("pt")->greet(), "olá");
    EXPECT_EQ(created, 1);
}

TEST(RegistryTest, RejectsInvalidRegistrations) {
    Registry<IGreeter> registry;
    const auto factory = [] { return std::make_unique<Greeter>("x"); };
    registry.add("a", "A", factory);
    EXPECT_THROW(registry.add("a", "A de novo", factory), RegistryError);
    EXPECT_THROW(registry.add("", "Sem id", factory), RegistryError);
    EXPECT_THROW(registry.add("b", "Sem fábrica", nullptr), RegistryError);
    EXPECT_THROW((void)registry.create("inexistente"), RegistryError);

    registry.add("nulo", "Fábrica nula", [] { return std::unique_ptr<IGreeter>(); });
    EXPECT_THROW((void)registry.create("nulo"), RegistryError);
}

// --- EventBus -----------------------------------------------------------------------------------------

struct Connected {
    static constexpr std::string_view kEventId = "test.connected";
    std::string session;
};

struct Disconnected {
    static constexpr std::string_view kEventId = "test.disconnected";
};

TEST(EventBusTest, DeliversOnlyMatchingEvents) {
    EventBus bus;
    std::vector<std::string> received;
    int disconnects = 0;
    auto a = bus.subscribe<Connected>([&](const Connected& e) { received.push_back("a:" + e.session); });
    auto b = bus.subscribe<Connected>([&](const Connected& e) { received.push_back("b:" + e.session); });
    auto c = bus.subscribe<Disconnected>([&](const Disconnected&) { ++disconnects; });

    bus.publish(Connected{.session = "s1"});
    bus.publish(Disconnected{});

    EXPECT_EQ(received, (std::vector<std::string>{"a:s1", "b:s1"}));
    EXPECT_EQ(disconnects, 1);
    EXPECT_EQ(bus.subscriberCount(), 3U);
}

TEST(EventBusTest, SubscriptionIsRaii) {
    EventBus bus;
    int calls = 0;
    {
        const auto subscription = bus.subscribe<Disconnected>([&](const Disconnected&) { ++calls; });
        bus.publish(Disconnected{});
    }
    bus.publish(Disconnected{});
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(bus.subscriberCount(), 0U);
}

TEST(EventBusTest, SubscriptionCanBeMovedAndReset) {
    EventBus bus;
    int calls = 0;
    EventBus::Subscription outer;
    EXPECT_FALSE(outer.active());
    {
        auto inner = bus.subscribe<Disconnected>([&](const Disconnected&) { ++calls; });
        outer = std::move(inner);
        EXPECT_FALSE(inner.active()); // NOLINT(bugprone-use-after-move): o estado após o move é o contrato
    }
    bus.publish(Disconnected{});
    EXPECT_EQ(calls, 1);
    outer.reset();
    bus.publish(Disconnected{});
    EXPECT_EQ(calls, 1);
}

TEST(EventBusTest, HandlerMayUnsubscribeItselfWhilePublishing) {
    EventBus bus;
    int calls = 0;
    EventBus::Subscription self;
    self = bus.subscribe<Disconnected>([&](const Disconnected&) {
        ++calls;
        self.reset();
    });
    bus.publish(Disconnected{});
    bus.publish(Disconnected{});
    EXPECT_EQ(calls, 1);
}

} // namespace
} // namespace mydb::sdk
