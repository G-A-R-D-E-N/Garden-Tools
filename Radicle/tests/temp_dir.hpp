#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4::test {

struct TempDir {
  std::filesystem::path path;

  explicit TempDir(std::string_view tag = "radicle_test") {
    static std::atomic<unsigned> counter{0};
    path = std::filesystem::temp_directory_path() /
           (std::string(tag) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(path);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

}  // namespace fo4::test
