#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/process/ProcessRunner.hpp>

namespace fo4ide::platform {

struct FileDialogOptions {
    std::string title;
    std::string filter;
    std::filesystem::path initialPath;
};

class Services {
public:
    virtual ~Services() = default;
    virtual Result<std::filesystem::path> pickFile(const FileDialogOptions& options) = 0;
    virtual Result<std::filesystem::path> pickFolder(const FileDialogOptions& options) = 0;
    virtual Result<std::filesystem::path> pickSavePath(const FileDialogOptions& options) = 0;
    virtual Result<void> openExternal(std::string_view value) = 0;
    virtual Result<void> openFolder(const std::filesystem::path& path) = 0;
};

class NativeServices final : public Services {
public:
    explicit NativeServices(process::Runner& runner);

    Result<std::filesystem::path> pickFile(const FileDialogOptions& options) override;
    Result<std::filesystem::path> pickFolder(const FileDialogOptions& options) override;
    Result<std::filesystem::path> pickSavePath(const FileDialogOptions& options) override;
    Result<void> openExternal(std::string_view value) override;
    Result<void> openFolder(const std::filesystem::path& path) override;

private:
#if !defined(_WIN32)
    process::Runner& runner_;
#endif
};

}
