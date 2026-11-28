#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace fo4ide::core {

using StageClock = std::chrono::steady_clock;

inline std::uint64_t elapsedNs(StageClock::time_point start) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(StageClock::now() - start).count());
}

// Thread-safe: adds the time spent in its scope to an atomic nanosecond counter.
class ScopedNs {
public:
    explicit ScopedNs(std::atomic<std::uint64_t>& target) noexcept
        : target_(target), start_(StageClock::now()) {}
    ScopedNs(const ScopedNs&) = delete;
    ScopedNs& operator=(const ScopedNs&) = delete;
    ~ScopedNs() { target_.fetch_add(elapsedNs(start_), std::memory_order_relaxed); }

private:
    std::atomic<std::uint64_t>& target_;
    StageClock::time_point start_;
};

}  // namespace fo4ide::core
