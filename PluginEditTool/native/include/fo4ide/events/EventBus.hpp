#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace fo4ide::events {

struct Event {
    std::string type;
    nlohmann::json payload;
};

class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    ~Subscription();

    std::optional<Event> tryPop();
    std::optional<Event> waitPop(std::chrono::milliseconds timeout);
    explicit operator bool() const noexcept;

private:
    struct Queue {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Event> events;
        bool closed{false};
    };

    friend class EventBus;
    Subscription(std::shared_ptr<Queue> queue, std::function<void()> unsubscribe);
    void reset();

    std::shared_ptr<Queue> queue_;
    std::function<void()> unsubscribe_;
};

class EventBus {
public:
    EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    ~EventBus();

    Subscription subscribe();
    void publish(Event event);
    std::size_t subscriberCount() const;
    void shutdown();

private:
    struct State {
        mutable std::mutex mutex;
        std::unordered_map<std::size_t, std::weak_ptr<Subscription::Queue>> subscribers;
        std::size_t nextId{1};
        bool closed{false};
    };

    std::shared_ptr<State> state_;
};

}
