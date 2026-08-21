#include <fo4ide/events/EventBus.hpp>

#include <utility>
#include <vector>

namespace fo4ide::events {

Subscription::Subscription(std::shared_ptr<Queue> queue, std::function<void()> unsubscribe)
    : queue_(std::move(queue)), unsubscribe_(std::move(unsubscribe)) {}

Subscription::Subscription(Subscription&& other) noexcept
    : queue_(std::move(other.queue_)), unsubscribe_(std::move(other.unsubscribe_)) {}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        queue_ = std::move(other.queue_);
        unsubscribe_ = std::move(other.unsubscribe_);
    }
    return *this;
}

Subscription::~Subscription() {
    reset();
}

std::optional<Event> Subscription::tryPop() {
    if (!queue_) return std::nullopt;
    std::scoped_lock lock(queue_->mutex);
    if (queue_->events.empty()) return std::nullopt;
    auto event = std::move(queue_->events.front());
    queue_->events.pop_front();
    return event;
}

std::optional<Event> Subscription::waitPop(std::chrono::milliseconds timeout) {
    if (!queue_) return std::nullopt;
    std::unique_lock lock(queue_->mutex);
    queue_->cv.wait_for(lock, timeout, [this] {
        return queue_->closed || !queue_->events.empty();
    });
    if (queue_->events.empty()) return std::nullopt;
    auto event = std::move(queue_->events.front());
    queue_->events.pop_front();
    return event;
}

Subscription::operator bool() const noexcept {
    return static_cast<bool>(queue_);
}

void Subscription::reset() {
    if (unsubscribe_) unsubscribe_();
    unsubscribe_ = {};
    queue_.reset();
}

EventBus::EventBus() : state_(std::make_shared<State>()) {}

EventBus::~EventBus() {
    shutdown();
}

Subscription EventBus::subscribe() {
    auto queue = std::make_shared<Subscription::Queue>();
    std::size_t id = 0;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->closed) {
            queue->closed = true;
            return Subscription(std::move(queue), {});
        }
        id = state_->nextId++;
        state_->subscribers.emplace(id, queue);
    }

    std::weak_ptr<State> weakState = state_;
    return Subscription(std::move(queue), [weakState, id] {
        if (auto state = weakState.lock()) {
            std::scoped_lock lock(state->mutex);
            state->subscribers.erase(id);
        }
    });
}

void EventBus::publish(Event event) {
    std::vector<std::shared_ptr<Subscription::Queue>> queues;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->closed) return;
        for (auto it = state_->subscribers.begin(); it != state_->subscribers.end();) {
            if (auto queue = it->second.lock()) {
                queues.push_back(std::move(queue));
                ++it;
            } else {
                it = state_->subscribers.erase(it);
            }
        }
    }

    for (auto& queue : queues) {
        {
            std::scoped_lock lock(queue->mutex);
            if (!queue->closed) queue->events.push_back(event);
        }
        queue->cv.notify_one();
    }
}

std::size_t EventBus::subscriberCount() const {
    std::scoped_lock lock(state_->mutex);
    return state_->subscribers.size();
}

void EventBus::shutdown() {
    std::vector<std::shared_ptr<Subscription::Queue>> queues;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->closed) return;
        state_->closed = true;
        for (auto& [id, weakQueue] : state_->subscribers) {
            static_cast<void>(id);
            if (auto queue = weakQueue.lock()) queues.push_back(std::move(queue));
        }
        state_->subscribers.clear();
    }

    for (auto& queue : queues) {
        {
            std::scoped_lock lock(queue->mutex);
            queue->closed = true;
        }
        queue->cv.notify_all();
    }
}

}
