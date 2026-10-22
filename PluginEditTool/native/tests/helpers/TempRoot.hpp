#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4ide_test {

class TempRoot {
public:
    explicit TempRoot(std::string_view parent)
        : parent_(std::filesystem::temp_directory_path() / std::string(parent)),
          path_(parent_ / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) {
        std::filesystem::create_directories(path_);
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
