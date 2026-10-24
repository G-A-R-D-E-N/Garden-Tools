#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fo4::test {

inline long process_id() {
#if defined(_WIN32)
  return _getpid();
#else
  return getpid();
#endif
}

class TempDataDir {
public:
  TempDataDir()
      : path_{std::filesystem::temp_directory_path() /
              ("radicle_load_order_" + std::to_string(process_id()) + "_" + std::to_string(next_id()))},
        base_{std::chrono::floor<std::chrono::seconds>(std::filesystem::file_time_type::clock::now()) -
              std::chrono::hours(1)} {
    std::filesystem::create_directories(path_);
  }
  ~TempDataDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDataDir(const TempDataDir&) = delete;
  TempDataDir& operator=(const TempDataDir&) = delete;

  const std::filesystem::path& path() const { return path_; }

  void write(const std::string& name, const std::vector<std::byte>& bytes, int rank) {
    const auto file = path_ / name;
    std::filesystem::create_directories(file.parent_path());
    {
      std::ofstream out{file, std::ios::binary};
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    std::filesystem::last_write_time(file, base_ + std::chrono::seconds(rank));
  }

private:
  static std::uint64_t next_id() {
    static std::atomic<std::uint64_t> id{0};
    return ++id;
  }

  std::filesystem::path path_;
  std::filesystem::file_time_type base_;
};

}  // namespace fo4::test
