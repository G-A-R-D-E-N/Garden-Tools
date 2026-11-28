#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::platform {

struct WindowOptions {
    std::string title{"FO4IDE"};
    std::string url;
    std::filesystem::path userDataDirectory;
    int width{1440};
    int height{900};
    std::function<void(std::string_view route, bool open)> onPopoutChanged;
};

class NativeWindow {
public:
    static bool available() noexcept;
    Result<int> run(const WindowOptions& options);
};

}
