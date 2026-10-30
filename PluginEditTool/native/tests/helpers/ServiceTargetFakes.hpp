#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fo4ide/app/ServiceTargets.hpp>

namespace fo4ide::app::test {

inline std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_service_test_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

inline void touch(const std::filesystem::path& path, std::string_view contents = {}) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
}

class FakeRunner final : public fo4ide::process::Runner {
public:
    fo4ide::process::Request last;
    fo4ide::process::Output output{true, false, 0, "native nif result", {}};
    int calls = 0;

    fo4ide::Result<fo4ide::process::Output> run(const fo4ide::process::Request& request) override {
        ++calls;
        last = request;
        return output;
    }
};

class FakePlatform final : public fo4ide::platform::Services {
public:
    std::filesystem::path fileSelection;
    std::filesystem::path folderSelection;
    std::filesystem::path saveSelection;
    std::filesystem::path openedFolder;
    std::string openedExternal;

    fo4ide::Result<std::filesystem::path> pickFile(const fo4ide::platform::FileDialogOptions&) override {
        return fileSelection;
    }

    fo4ide::Result<std::filesystem::path> pickFolder(const fo4ide::platform::FileDialogOptions&) override {
        return folderSelection;
    }

    fo4ide::Result<std::filesystem::path> pickSavePath(const fo4ide::platform::FileDialogOptions&) override {
        return saveSelection;
    }

    fo4ide::Result<void> openExternal(std::string_view value) override {
        openedExternal = value;
        return {};
    }

    fo4ide::Result<void> openFolder(const std::filesystem::path& path) override {
        openedFolder = path;
        return {};
    }
};

}
