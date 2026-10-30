#include <fo4ide/tools/ToolLocator.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace fo4ide::tools {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string environmentValue(const char* name) {
#if defined(_WIN32)
    char* raw = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) return {};
    std::string value(raw);
    std::free(raw);
    return value;
#else
    const char* raw = std::getenv(name);
    return raw ? std::string(raw) : std::string{};
#endif
}

#if defined(_WIN32)
std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
#endif

std::string pathKey(const std::filesystem::path& path) {
    auto value = path.lexically_normal().string();
#if defined(_WIN32)
    value = lowerAscii(std::move(value));
#endif
    return value;
}

std::filesystem::path searchPath(const std::vector<std::string>& names) {
    const auto value = environmentValue("PATH");
    if (value.empty()) return {};
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(separator, begin);
        const auto entry = trim(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!entry.empty()) {
            for (const auto& name : names) {
                const auto candidate = std::filesystem::path(entry) / name;
                std::error_code ec;
                if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

#if defined(_WIN32)
std::filesystem::path registryFallout4Root() {
    constexpr const wchar_t* keys[] = {
        L"SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Fallout4",
        L"SOFTWARE\\Bethesda Softworks\\Fallout4"
    };
    for (const auto* keyName : keys) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyName, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        wchar_t buffer[32768]{};
        DWORD bytes = sizeof(buffer);
        DWORD type = 0;
        const auto result = RegQueryValueExW(key, L"installed path", nullptr, &type,
                                             reinterpret_cast<LPBYTE>(buffer), &bytes);
        RegCloseKey(key);
        if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && buffer[0]) {
            std::filesystem::path path(buffer);
            if (std::filesystem::is_directory(path)) return path;
        }
    }
    return {};
}
#endif

}

Locator::Locator(const settings::SharedSettings& settings, std::filesystem::path executablePath)
    : settings_(settings), executablePath_(std::move(executablePath)) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(executablePath_, ec);
    if (!ec) executablePath_ = std::move(absolute);
    appDirectory_ = executablePath_.parent_path();
    if (appDirectory_.empty()) appDirectory_ = std::filesystem::current_path();
}

std::filesystem::path Locator::environmentPath(const char* name) const {
    const auto raw = environmentValue(name);
    if (raw.empty()) return {};
    return std::filesystem::path(trim(raw));
}

std::filesystem::path Locator::appPath(std::initializer_list<std::string_view> parts) const {
    auto path = appDirectory_;
    for (const auto part : parts) path /= std::string(part);
    return path;
}

std::filesystem::path Locator::firstFile(std::initializer_list<std::filesystem::path> candidates) const {
    for (const auto& path : candidates) {
        std::error_code ec;
        if (!path.empty() && std::filesystem::is_regular_file(path, ec)) return path;
    }
    return {};
}

std::filesystem::path Locator::firstDirectory(std::initializer_list<std::filesystem::path> candidates) const {
    for (const auto& path : candidates) {
        std::error_code ec;
        if (!path.empty() && std::filesystem::is_directory(path, ec)) return path;
    }
    return {};
}

std::filesystem::path Locator::onPath(std::initializer_list<std::string_view> names) const {
    return searchPath(std::vector<std::string>(names.begin(), names.end()));
}

std::filesystem::path Locator::fallout4Root() const {
    const auto current = settings_.snapshot();
    std::error_code ec;
    if (!current.fallout4Path.empty() && std::filesystem::is_directory(current.fallout4Path, ec)) {
        return current.fallout4Path;
    }
    const auto env = environmentPath("FALLOUT4_PATH");
    if (!env.empty() && std::filesystem::is_directory(env, ec)) return env;
#if defined(_WIN32)
    if (auto registry = registryFallout4Root(); !registry.empty()) return registry;
#endif
    std::vector<std::filesystem::path> bases;
#if defined(_WIN32)
    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        const auto root = std::filesystem::path(std::string(1, drive) + ":\\");
        if (std::filesystem::exists(root, ec)) bases.push_back(root);
    }
#else
    if (const char* home = std::getenv("HOME"); home && *home) bases.emplace_back(home);
    bases.emplace_back("/run/media");
    bases.emplace_back("/mnt");
#endif

    for (const auto& base : bases) {
        const std::filesystem::path probes[] = {
            base / "SteamLibrary" / "steamapps" / "common" / "Fallout 4",
            base / "Program Files (x86)" / "Steam" / "steamapps" / "common" / "Fallout 4",
            base / ".steam" / "steam" / "steamapps" / "common" / "Fallout 4",
            base / ".local" / "share" / "Steam" / "steamapps" / "common" / "Fallout 4"
        };
        for (const auto& probe : probes) {
            if (std::filesystem::is_directory(probe, ec)) return probe;
        }
    }
    return {};
}

std::filesystem::path Locator::resolve(Tool tool) const {
    const auto current = settings_.snapshot();
    const auto root = fallout4Root();
    switch (tool) {
        case Tool::Texconv:
            return firstFile({environmentPath("TEXCONV_PATH"), current.texconvPath,
                              appPath({"tools", "texconv", "Texconvx64.exe"}),
                              appPath({"tools", "texconv", "Texconv.exe"}),
                              onPath({"Texconvx64.exe", "Texconv.exe", "texconv"})});
        case Tool::PapyrusCompiler:
            return firstFile({environmentPath("PAPYRUS_COMPILER_PATH"), current.papyrusCompilerPath,
                              root.empty() ? std::filesystem::path{} : root / "Papyrus Compiler" / "PapyrusCompiler.exe",
                              onPath({"PapyrusCompiler.exe"})});
        case Tool::Archive2:
            return firstFile({environmentPath("ARCHIVE2_PATH"), current.archive2Path,
                              root.empty() ? std::filesystem::path{} : root / "Tools" / "Archive2" / "Archive2.exe",
                              onPath({"Archive2.exe"})});
        case Tool::CkWiki:
            return firstDirectory({environmentPath("CK_WIKI_PATH"), current.ckWikiPath,
                                   appPath({"tools", "ckwiki", "fallout4"})});
        case Tool::Ffmpeg:
#if defined(_WIN32)
            return firstFile({environmentPath("FFMPEG_PATH"), current.ffmpegPath,
                              appPath({"tools", "audio", "ffmpeg.exe"}), onPath({"ffmpeg.exe"})});
#else
            return firstFile({environmentPath("FFMPEG_PATH"), current.ffmpegPath,
                              appPath({"tools", "audio", "ffmpeg"}), onPath({"ffmpeg", "ffmpeg.exe"})});
#endif
        case Tool::XwmaEncode:
            return firstFile({environmentPath("XWMAENCODE_PATH"), current.xwmaEncodePath,
                              appPath({"tools", "audio", "xWMAEncode.exe"}), onPath({"xWMAEncode.exe"})});
        case Tool::BmlFuzEncode:
            return firstFile({environmentPath("BMLFUZENCODE_PATH"),
                              appPath({"tools", "audio", "BmlFuzEncode.exe"}), onPath({"BmlFuzEncode.exe"})});
        case Tool::BmlFuzDecode:
            return firstFile({environmentPath("BMLFUZDECODE_PATH"),
                              appPath({"tools", "audio", "BmlFuzDecode.exe"}), onPath({"BmlFuzDecode.exe"})});
        case Tool::ClaudeCode:
#if defined(_WIN32)
            return firstFile({environmentPath("CLAUDE_CODE_PATH"), current.claudeCodePath,
                              onPath({"claude.exe", "claude.cmd"})});
#else
            return firstFile({environmentPath("CLAUDE_CODE_PATH"), current.claudeCodePath,
                              onPath({"claude"})});
#endif
    }
    return {};
}

std::vector<std::filesystem::path> Locator::papyrusBaseImports() const {
    const auto current = settings_.snapshot();
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    auto add = [&](const std::filesystem::path& path) {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_directory(path, ec)) return;
        const auto key = pathKey(path);
        if (seen.insert(key).second) result.push_back(path);
    };

    auto addDelimited = [&](std::string value) {
        std::size_t begin = 0;
        while (begin <= value.size()) {
            const auto end = value.find(';', begin);
            add(trim(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin)));
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    };

    if (const auto raw = environmentValue("PAPYRUS_BASE_IMPORTS"); !raw.empty()) addDelimited(raw);
    if (!current.papyrusBaseImports.empty()) addDelimited(current.papyrusBaseImports.string());
    add(appPath({"tools", "papyrus", "F4SE"}));
    add(appPath({"tools", "papyrus", "Base"}));

    if (const auto root = fallout4Root(); !root.empty()) {
        add(root / "Data" / "Scripts" / "Source" / "User");
        add(root / "Data" / "Scripts" / "Source" / "Base");
    }
    return result;
}

std::string_view Locator::describe(Tool tool) const noexcept {
    switch (tool) {
        case Tool::Texconv: return "set TEXCONV_PATH or place Texconv in tools/texconv";
        case Tool::PapyrusCompiler: return "install the Creation Kit or set PAPYRUS_COMPILER_PATH";
        case Tool::Archive2: return "install the Creation Kit or set ARCHIVE2_PATH";
        case Tool::CkWiki: return "set CK_WIKI_PATH or place the mirror in tools/ckwiki/fallout4";
        case Tool::Ffmpeg: return "set FFMPEG_PATH or place ffmpeg in tools/audio";
        case Tool::XwmaEncode: return "set XWMAENCODE_PATH or place xWMAEncode.exe in tools/audio";
        case Tool::BmlFuzEncode: return "set BMLFUZENCODE_PATH or place BmlFuzEncode.exe in tools/audio";
        case Tool::BmlFuzDecode: return "set BMLFUZDECODE_PATH or place BmlFuzDecode.exe in tools/audio";
        case Tool::ClaudeCode: return "set CLAUDE_CODE_PATH or put claude on PATH";
    }
    return {};
}

std::filesystem::path resolveCommand(const std::filesystem::path& command) {
    if (!command.parent_path().empty()) return command;
    std::vector<std::string> names;
#if defined(_WIN32)
    if (command.extension().empty()) {
        auto list = environmentValue("PATHEXT");
        if (list.empty()) list = ".COM;.EXE;.BAT;.CMD";
        std::size_t begin = 0;
        while (begin <= list.size()) {
            const auto end = list.find(';', begin);
            const auto extension = trim(list.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
            if (!extension.empty()) names.push_back(command.string() + extension);
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    } else {
        names.push_back(command.string());
    }
#else
    names.push_back(command.string());
#endif
    auto found = searchPath(names);
    return found.empty() ? command : found;
}

}
