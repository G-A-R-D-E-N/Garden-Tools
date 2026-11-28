#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fo4ide_test {

// Requires: a name unique per process and call, because ctest runs cases as parallel processes.
inline std::string uniqueTempName() {
    static std::atomic<unsigned long long> calls{0};
#ifdef _WIN32
    const auto pid = static_cast<unsigned long>(::_getpid());
#else
    const auto pid = static_cast<unsigned long>(::getpid());
#endif
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return std::to_string(pid) + "-" + std::to_string(stamp) + "-" +
           std::to_string(calls.fetch_add(1, std::memory_order_relaxed));
}

class TempRoot {
public:
    explicit TempRoot(std::string_view parent)
        : parent_(std::filesystem::temp_directory_path() / std::string(parent)),
          path_(parent_ / uniqueTempName()) {
        // A finishing test can remove the shared empty parent during this call, so retry.
        std::error_code error;
        for (int attempt = 0; attempt < 5; ++attempt) {
            error.clear();
            if (std::filesystem::create_directories(path_, error); !error) return;
        }
        throw std::filesystem::filesystem_error("cannot create test folder", path_, error);
    }
    ~TempRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::remove(parent_, ignored);
    }
    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::string string() const { return path_.string(); }

private:
    std::filesystem::path parent_;
    std::filesystem::path path_;
};

}  // namespace fo4ide_test
