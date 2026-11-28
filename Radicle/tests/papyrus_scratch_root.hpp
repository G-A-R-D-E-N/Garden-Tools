#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4::papyrus::test {

class ScratchRoot {
public:
  explicit ScratchRoot(std::string_view tag) {
    static std::atomic<unsigned> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("fo4re-" + std::string(tag) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
             std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(path_);
  }

  ~ScratchRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  ScratchRoot(const ScratchRoot&) = delete;
  ScratchRoot& operator=(const ScratchRoot&) = delete;

  const std::filesystem::path& path() const { return path_; }

  std::filesystem::path write(const std::string& relative, const std::string& content = {}) const {
    const auto file = path_ / relative;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
    return file;
  }

private:
  std::filesystem::path path_;
};

}
