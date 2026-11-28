#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/events/EventBus.hpp>

using namespace std::chrono_literals;

TEST_CASE("EventBus broadcasts independently to each subscriber") {
    fo4ide::events::EventBus bus;
    auto first = bus.subscribe();
    auto second = bus.subscribe();

    bus.publish({"SetStatus", {{"text", "Loading"}}});

    const auto a = first.waitPop(50ms);
    const auto b = second.waitPop(50ms);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->type == "SetStatus");
    CHECK(a->payload.at("text") == "Loading");
    CHECK(b->payload == a->payload);
}

TEST_CASE("EventBus removes subscriptions when they are destroyed") {
    fo4ide::events::EventBus bus;
    {
        auto subscription = bus.subscribe();
        CHECK(bus.subscriberCount() == 1);
    }
    CHECK(bus.subscriberCount() == 0);
}

TEST_CASE("EventBus preserves publication order") {
    fo4ide::events::EventBus bus;
    auto subscription = bus.subscribe();

    for (int i = 0; i < 20; ++i) bus.publish({"Tick", {{"value", i}}});

    for (int i = 0; i < 20; ++i) {
        const auto event = subscription.waitPop(50ms);
        REQUIRE(event.has_value());
        CHECK(event->payload.at("value") == i);
    }
}

TEST_CASE("EventBus shutdown wakes waiting subscribers") {
    fo4ide::events::EventBus bus;
    auto subscription = bus.subscribe();
    bus.shutdown();
    CHECK_FALSE(subscription.waitPop(10ms).has_value());
}
