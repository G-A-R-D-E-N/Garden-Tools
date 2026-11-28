#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace fo4::lod {

// Invariant: items are independent and own their output slot; the first exception is rethrown on the caller's thread.
template <typename Body>
void parallel_for(std::size_t count, Body&& body) {
  const auto reported = static_cast<std::size_t>(std::thread::hardware_concurrency());
  const auto workers = std::min<std::size_t>(count, std::max<std::size_t>(1, reported));
  if (workers <= 1) {
    for (std::size_t index = 0; index < count; ++index) body(index);
    return;
  }
  std::atomic<std::size_t> next{0};
  std::mutex failure_mutex;
  std::exception_ptr failure;
  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (std::size_t worker = 0; worker < workers; ++worker) {
    threads.emplace_back([&]() {
      for (;;) {
        const auto index = next.fetch_add(1);
        if (index >= count) return;
        try {
          body(index);
        } catch (...) {
          std::lock_guard<std::mutex> guard(failure_mutex);
          if (!failure) failure = std::current_exception();
          next.store(count);
          return;
        }
      }
    });
  }
  for (auto& thread : threads) thread.join();
  if (failure) std::rethrow_exception(failure);
}

}  // namespace fo4::lod
