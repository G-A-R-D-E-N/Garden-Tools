#include <fo4ide/app/ServiceTargets.hpp>

#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fo4ide::app {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

const nlohmann::json* argument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return nullptr;
    return &args[index];
}

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argument(args, index);
    return value && value->is_string() ? value->get<std::string>() : std::string{};
}

bool boolArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argument(args, index);
    return value && value->is_boolean() && value->get<bool>();
}

void updateString(const nlohmann::json& payload, const char* key, std::string& target) {
    const auto it = payload.find(key);
    if (it != payload.end() && it->is_string()) target = it->get<std::string>();
}

void updatePath(const nlohmann::json& payload, const char* key, std::filesystem::path& target) {
    const auto it = payload.find(key);
    if (it != payload.end() && it->is_string()) target = trim(it->get<std::string>());
}

void updateBool(const nlohmann::json& payload, const char* key, bool& target) {
    const auto it = payload.find(key);
    if (it != payload.end() && it->is_boolean()) target = it->get<bool>();
}

nlohmann::json serviceText(Result<std::string> result) {
    if (!result) return "Error: " + result.error().message;
    return std::move(*result);
}

nlohmann::json pathResult(Result<std::filesystem::path> result) {
    if (!result) return std::string{};
    return result->string();
}

int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

Result<std::vector<unsigned char>> decodeBase64(std::string_view input) {
    std::vector<unsigned char> output;
    output.reserve(input.size() * 3 / 4);
    unsigned int value = 0;
    int bits = -8;
    for (const unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int digit = base64Value(c);
        if (digit < 0) return std::unexpected(Error{"InvalidBase64", "Dropped file payload is not valid base64"});
        value = (value << 6) | static_cast<unsigned int>(digit);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<unsigned char>((value >> bits) & 0xFFU));
            bits -= 8;
        }
    }
    return output;
}

std::filesystem::path uniqueStageDirectory() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("FO4RE_Nif_" + std::to_string(ticks) + "_" + std::to_string(thread));
}

}

SettingsTarget::SettingsTarget(settings::Store& store,
                               settings::Settings& current,
                               platform::Services& platform,
                               process::Runner& runner)
    : store_(store), current_(current), platform_(platform), runner_(runner) {}

Result<nlohmann::json> SettingsTarget::invoke(std::string_view method,
                                               const nlohmann::json& args) {
    if (method == "GetSettings") return getSettings();
    if (method == "SaveSettings") return saveSettings(args);
    if (method == "BrowseFolder") return browseFolder(args);
    if (method == "BrowseFile") return browseFile(args);
    if (method == "TestClaude") return testClaude(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown settings RPC method: " + std::string(method)});
}

Result<nlohmann::json> SettingsTarget::getSettings() const {
    nlohmann::json value = current_;
    return value.dump();
}

Result<nlohmann::json> SettingsTarget::saveSettings(const nlohmann::json& args) {
    const auto* raw = argument(args, 0);
    if (!raw || !raw->is_string()) return nlohmann::json("Invalid settings payload.");

    try {
        const auto payload = nlohmann::json::parse(raw->get<std::string>());
        if (!payload.is_object()) return nlohmann::json("Invalid settings payload.");

        updateString(payload, "AiProvider", current_.aiProvider);
        updateString(payload, "AnthropicApiKey", current_.anthropicApiKey);
        updateString(payload, "Model", current_.model);
        updateString(payload, "GeminiApiKey", current_.geminiApiKey);
        updateString(payload, "GeminiModel", current_.geminiModel);
        updatePath(payload, "ClaudeCodePath", current_.claudeCodePath);
        updateString(payload, "OllamaUrl", current_.ollamaUrl);
        updateString(payload, "OllamaModel", current_.ollamaModel);
        updatePath(payload, "OutputFolder", current_.outputFolder);
        updatePath(payload, "Fallout4Path", current_.fallout4Path);
        updatePath(payload, "DataFolder", current_.dataFolder);
        updatePath(payload, "Mo2InstancePath", current_.mo2InstancePath);
        updatePath(payload, "CkWikiPath", current_.ckWikiPath);
        updatePath(payload, "TexconvPath", current_.texconvPath);
        updatePath(payload, "PapyrusCompilerPath", current_.papyrusCompilerPath);
        updatePath(payload, "PapyrusBaseImports", current_.papyrusBaseImports);
        updatePath(payload, "NiftoolPath", current_.nifToolPath);
        updatePath(payload, "FfmpegPath", current_.ffmpegPath);
        updatePath(payload, "XwmaEncodePath", current_.xwmaEncodePath);
        updatePath(payload, "Archive2Path", current_.archive2Path);
        updateBool(payload, "ReadLargePluginsIntoMemory", current_.readLargePluginsIntoMemory);

        auto saved = store_.save(current_);
        if (!saved) return nlohmann::json("Save failed: " + saved.error().message);
        return nlohmann::json("Settings saved.");
    } catch (const std::exception& ex) {
        return nlohmann::json("Save failed: " + std::string(ex.what()));
    }
}

Result<nlohmann::json> SettingsTarget::browseFolder(const nlohmann::json& args) {
    const auto title = stringArgument(args, 0);
    const auto current = stringArgument(args, 1);
    platform::FileDialogOptions options;
    options.title = title.empty() ? "Choose a folder" : title;
    if (!current.empty() && std::filesystem::is_directory(current)) options.initialPath = current;
    return pathResult(platform_.pickFolder(options));
}

Result<nlohmann::json> SettingsTarget::browseFile(const nlohmann::json& args) {
    platform::FileDialogOptions options;
    options.title = stringArgument(args, 0);
    options.filter = stringArgument(args, 1);
    const auto current = stringArgument(args, 2);
    if (!current.empty() && std::filesystem::is_regular_file(current)) options.initialPath = current;
    return pathResult(platform_.pickFile(options));
}

Result<nlohmann::json> SettingsTarget::testClaude(const nlohmann::json& args) {
    auto path = trim(stringArgument(args, 0));
    if (path.empty()) path = "claude";

    process::Request request;
#if defined(_WIN32)
    request.executable = "cmd.exe";
    request.arguments = {"/d", "/s", "/c", "\"" + path + "\" --version"};
#else
    request.executable = path;
    request.arguments = {"--version"};
#endif
    request.timeout = std::chrono::seconds(30);
    auto run = runner_.run(request);
    if (!run) return nlohmann::json("Not found: " + run.error().message);
    if (run->timedOut) return nlohmann::json("claude --version timed out after 30s.");
    if (run->exitCode != 0) return nlohmann::json("claude ran but returned an error.");
    return nlohmann::json("Found: " + trim(run->stdOut));
}

NifTarget::NifTarget(assets::NifService& service, platform::Services& platform)
    : service_(service), platform_(platform) {}

Result<nlohmann::json> NifTarget::invoke(std::string_view method,
                                         const nlohmann::json& args) {
    if (method == "BrowseForFile") {
        return pathResult(platform_.pickFile({stringArgument(args, 0), stringArgument(args, 1), {}}));
    }
    if (method == "BrowseForFolder") {
        return pathResult(platform_.pickFolder({stringArgument(args, 0), {}, {}}));
    }
    if (method == "BrowseForSave") {
        auto filter = stringArgument(args, 1);
        if (filter.empty()) filter = "NIF mesh|*.nif|All files|*.*";
        return pathResult(platform_.pickSavePath({stringArgument(args, 0), filter, {}}));
    }
    if (method == "Inspect") return serviceText(service_.inspect(stringArgument(args, 0)));
    if (method == "Geo") return serviceText(service_.geometry(stringArgument(args, 0)));
    if (method == "Verify") return serviceText(service_.verify(stringArgument(args, 0)));
    if (method == "Tree") return serviceText(service_.tree(stringArgument(args, 0)));
    if (method == "Fix") {
        return serviceText(service_.fix(stringArgument(args, 0), stringArgument(args, 1)));
    }
    if (method == "Import") {
        assets::NifImportOptions options;
        options.material = stringArgument(args, 2);
        options.diffuseTexture = stringArgument(args, 3);
        options.normalTexture = stringArgument(args, 4);
        options.collisionBox = boolArgument(args, 5);
        options.fromBlender = boolArgument(args, 6);
        return serviceText(service_.importObj(stringArgument(args, 0), stringArgument(args, 1), options));
    }
    if (method == "ApplyEdits") {
        try {
            const auto editsText = stringArgument(args, 1);
            const auto edits = nlohmann::json::parse(editsText);
            return serviceText(service_.applyEdits(stringArgument(args, 0), edits, stringArgument(args, 2)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "OpenFolder") {
        auto opened = platform_.openFolder(stringArgument(args, 0));
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown NIF RPC method: " + std::string(method)});
}

Result<nlohmann::json> NifTarget::stageDroppedFile(const nlohmann::json& args) const {
    try {
        auto name = std::filesystem::path(stringArgument(args, 0)).filename();
        if (name.empty()) name = "dropped.bin";
        auto decoded = decodeBase64(stringArgument(args, 1));
        if (!decoded) return nlohmann::json("ERR:" + decoded.error().message);

        const auto directory = uniqueStageDirectory();
        std::filesystem::create_directories(directory);
        const auto path = directory / name;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return nlohmann::json("ERR:Could not create staged file");
        stream.write(reinterpret_cast<const char*>(decoded->data()), static_cast<std::streamsize>(decoded->size()));
        stream.close();
        if (!stream) return nlohmann::json("ERR:Could not write staged file");
        return nlohmann::json(path.string());
    } catch (const std::exception& ex) {
        return nlohmann::json("ERR:" + std::string(ex.what()));
    }
}

}
