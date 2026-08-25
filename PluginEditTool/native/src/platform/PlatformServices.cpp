#include <fo4ide/platform/PlatformServices.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace fo4ide::platform {
namespace {

#if !defined(_WIN32)
std::filesystem::path findProgram(std::string_view name) {
    const char* raw = std::getenv("PATH");
    if (!raw || !*raw) return {};
    const std::string value(raw);
    constexpr char separator = ':';
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(separator, begin);
        auto part = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!part.empty()) {
            auto candidate = std::filesystem::path(part) / name;
            if (std::filesystem::is_regular_file(candidate)) return candidate;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

std::string trimOutput(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
    return value;
}

std::pair<std::string, std::string> firstFilter(std::string_view filter) {
    const auto first = filter.find('|');
    if (first == std::string_view::npos) return {"Files", std::string(filter)};
    const auto second = filter.find('|', first + 1);
    const auto name = std::string(filter.substr(0, first));
    const auto pattern = std::string(filter.substr(first + 1, second == std::string_view::npos ? std::string_view::npos : second - first - 1));
    return {name, pattern};
}
#endif

#if defined(_WIN32)

std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

class ComScope {
public:
    ComScope() {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        owns_ = result_ == S_OK || result_ == S_FALSE;
    }

    ~ComScope() {
        if (owns_) CoUninitialize();
    }

    bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_{E_FAIL};
    bool owns_{false};
};

std::vector<std::pair<std::wstring, std::wstring>> parseFilters(std::string_view filter) {
    std::vector<std::pair<std::wstring, std::wstring>> result;
    std::size_t begin = 0;
    while (begin < filter.size()) {
        const auto split = filter.find('|', begin);
        if (split == std::string_view::npos) break;
        const auto next = filter.find('|', split + 1);
        result.emplace_back(
            widen(filter.substr(begin, split - begin)),
            widen(filter.substr(split + 1, next == std::string_view::npos ? std::string_view::npos : next - split - 1)));
        if (next == std::string_view::npos) break;
        begin = next + 1;
    }
    return result;
}

void setInitialFolder(IFileDialog* dialog, const std::filesystem::path& initialPath) {
    if (!dialog || initialPath.empty()) return;
    auto folder = initialPath;
    if (std::filesystem::is_regular_file(folder)) folder = folder.parent_path();
    if (!std::filesystem::is_directory(folder)) return;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        dialog->SetFolder(item);
        item->Release();
    }
}

Result<std::filesystem::path> showDialog(const FileDialogOptions& options, bool folder, bool save) {
    ComScope com;
    if (!com.usable()) return std::unexpected(Error{"DialogInitFailed", "COM initialization failed"});

    IFileDialog* dialog = nullptr;
    HRESULT hr = save
        ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
        : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return std::unexpected(Error{"DialogInitFailed", "Could not create Windows file dialog"});

    DWORD flags = 0;
    dialog->GetOptions(&flags);
    flags |= FOS_FORCEFILESYSTEM;
    if (folder) flags |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
    dialog->SetOptions(flags);

    const auto title = widen(options.title);
    if (!title.empty()) dialog->SetTitle(title.c_str());
    setInitialFolder(dialog, options.initialPath);

    auto filterPairs = parseFilters(options.filter);
    std::vector<COMDLG_FILTERSPEC> specs;
    specs.reserve(filterPairs.size());
    for (auto& pair : filterPairs) specs.push_back({pair.first.c_str(), pair.second.c_str()});
    if (!folder && !specs.empty()) dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());

    hr = dialog->Show(nullptr);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return std::filesystem::path{};
    }
    if (FAILED(hr)) {
        dialog->Release();
        return std::unexpected(Error{"DialogFailed", "Windows file dialog failed"});
    }

    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    dialog->Release();
    if (FAILED(hr) || !item) return std::unexpected(Error{"DialogFailed", "Windows file dialog returned no selection"});

    PWSTR rawPath = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    item->Release();
    if (FAILED(hr) || !rawPath) return std::unexpected(Error{"DialogFailed", "Could not resolve selected path"});

    std::filesystem::path result(rawPath);
    CoTaskMemFree(rawPath);
    return result;
}

#else

Result<std::filesystem::path> runPortableDialog(process::Runner& runner,
                                                const FileDialogOptions& options,
                                                bool folder,
                                                bool save) {
#if defined(__APPLE__)
    static_cast<void>(runner);
    static_cast<void>(options);
    static_cast<void>(folder);
    static_cast<void>(save);
    return std::unexpected(Error{"DialogUnavailable", "Native macOS dialogs are not implemented"});
#else
    if (const auto zenity = findProgram("zenity"); !zenity.empty()) {
        process::Request request;
        request.executable = zenity;
        request.arguments = {"--file-selection"};
        if (!options.title.empty()) request.arguments.push_back("--title=" + options.title);
        if (folder) request.arguments.push_back("--directory");
        if (save) {
            request.arguments.push_back("--save");
            request.arguments.push_back("--confirm-overwrite");
        }
        if (!options.initialPath.empty()) request.arguments.push_back("--filename=" + options.initialPath.string());
        if (!folder && !options.filter.empty()) {
            auto [name, pattern] = firstFilter(options.filter);
            std::replace(pattern.begin(), pattern.end(), ';', ' ');
            request.arguments.push_back("--file-filter=" + name + " | " + pattern);
        }
        request.timeout = std::chrono::minutes(10);
        auto result = runner.run(request);
        if (!result) return std::unexpected(result.error());
        if (result->timedOut) return std::unexpected(Error{"DialogTimeout", "File dialog timed out"});
        if (result->exitCode != 0) return std::filesystem::path{};
        return std::filesystem::path(trimOutput(result->stdOut));
    }

    if (const auto kdialog = findProgram("kdialog"); !kdialog.empty()) {
        process::Request request;
        request.executable = kdialog;
        if (!options.title.empty()) {
            request.arguments.push_back("--title");
            request.arguments.push_back(options.title);
        }
        if (folder) {
            request.arguments.push_back("--getexistingdirectory");
            request.arguments.push_back(options.initialPath.empty() ? std::string(".") : options.initialPath.string());
        } else {
            request.arguments.push_back(save ? "--getsavefilename" : "--getopenfilename");
            request.arguments.push_back(options.initialPath.empty() ? std::string(".") : options.initialPath.string());
            if (!options.filter.empty()) {
                auto [name, pattern] = firstFilter(options.filter);
                std::replace(pattern.begin(), pattern.end(), ';', ' ');
                request.arguments.push_back(pattern + "|" + name);
            }
        }
        request.timeout = std::chrono::minutes(10);
        auto result = runner.run(request);
        if (!result) return std::unexpected(result.error());
        if (result->timedOut) return std::unexpected(Error{"DialogTimeout", "File dialog timed out"});
        if (result->exitCode != 0) return std::filesystem::path{};
        return std::filesystem::path(trimOutput(result->stdOut));
    }

    return std::unexpected(Error{"DialogUnavailable", "Install zenity or kdialog to use native file dialogs"});
#endif
}

#endif

}

NativeServices::NativeServices(process::Runner& runner)
#if defined(_WIN32)
{
    static_cast<void>(runner);
}
#else
    : runner_(runner) {}
#endif

Result<std::filesystem::path> NativeServices::pickFile(const FileDialogOptions& options) {
#if defined(_WIN32)
    return showDialog(options, false, false);
#else
    return runPortableDialog(runner_, options, false, false);
#endif
}

Result<std::filesystem::path> NativeServices::pickFolder(const FileDialogOptions& options) {
#if defined(_WIN32)
    return showDialog(options, true, false);
#else
    return runPortableDialog(runner_, options, true, false);
#endif
}

Result<std::filesystem::path> NativeServices::pickSavePath(const FileDialogOptions& options) {
#if defined(_WIN32)
    return showDialog(options, false, true);
#else
    return runPortableDialog(runner_, options, false, true);
#endif
}

Result<void> NativeServices::openExternal(std::string_view value) {
    if (value.empty()) return std::unexpected(Error{"InvalidPath", "External target is empty"});
#if defined(_WIN32)
    const auto wide = widen(value);
    const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) return std::unexpected(Error{"OpenFailed", "ShellExecuteW failed"});
    return {};
#else
#if defined(__APPLE__)
    auto executable = findProgram("open");
    std::vector<std::string> arguments{std::string(value)};
#else
    auto executable = findProgram("xdg-open");
    std::vector<std::string> arguments{std::string(value)};
    if (executable.empty()) {
        executable = findProgram("gio");
        arguments.insert(arguments.begin(), "open");
    }
#endif
    if (executable.empty()) return std::unexpected(Error{"OpenFailed", "No desktop opener was found"});
    process::Request request;
    request.executable = executable;
    request.arguments = std::move(arguments);
    request.timeout = std::chrono::seconds(15);
    auto result = runner_.run(request);
    if (!result) return std::unexpected(result.error());
    if (result->exitCode != 0) return std::unexpected(Error{"OpenFailed", result->combined()});
    return {};
#endif
}

Result<void> NativeServices::openFolder(const std::filesystem::path& path) {
    auto target = path;
    if (std::filesystem::is_regular_file(target)) target = target.parent_path();
    if (!std::filesystem::is_directory(target)) {
        return std::unexpected(Error{"FolderNotFound", "Folder not found: " + target.string()});
    }
    return openExternal(target.string());
}

}
