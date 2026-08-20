#include <fo4ide/app/ServiceTargets.hpp>

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

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

std::filesystem::path firstExisting(std::initializer_list<std::filesystem::path> candidates) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return {};
}

std::filesystem::path findOnPath(std::string_view fileName) {
    const char* rawPath = std::getenv("PATH");
    if (!rawPath || !*rawPath) return {};
    const std::string pathValue(rawPath);
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::size_t begin = 0;
    while (begin <= pathValue.size()) {
        const auto end = pathValue.find(separator, begin);
        const auto part = pathValue.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!part.empty()) {
            const auto candidate = std::filesystem::path(part) / fileName;
            if (std::filesystem::is_regular_file(candidate)) return candidate;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

}

SettingsTarget::SettingsTarget(settings::Store& store, settings::Settings& current)
    : store_(store), current_(current) {}

Result<nlohmann::json> SettingsTarget::invoke(std::string_view method,
                                               const nlohmann::json& args) {
    if (method == "GetSettings") return getSettings();
    if (method == "SaveSettings") return saveSettings(args);
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

NifTarget::NifTarget(assets::NifService& service) : service_(service) {}

Result<nlohmann::json> NifTarget::invoke(std::string_view method,
                                         const nlohmann::json& args) {
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
    return std::unexpected(Error{"UnknownMethod", "Unknown NIF RPC method: " + std::string(method)});
}

std::filesystem::path locateNifTool(const settings::Settings& settings,
                                    const std::filesystem::path& executablePath) {
    const char* env = std::getenv("NIFTOOL_PATH");
    const auto base = std::filesystem::absolute(executablePath).parent_path();
#if defined(_WIN32)
    const auto local = firstExisting({
        env && *env ? std::filesystem::path(env) : std::filesystem::path{},
        settings.nifToolPath,
        base / "tools" / "niftool" / "niftool.exe",
        base / "niftool" / "niftool.exe"
    });
    if (!local.empty()) return local;
    return findOnPath("niftool.exe");
#else
    const auto local = firstExisting({
        env && *env ? std::filesystem::path(env) : std::filesystem::path{},
        settings.nifToolPath,
        base / "tools" / "niftool" / "niftool",
        base / "niftool" / "niftool"
    });
    if (!local.empty()) return local;
    return findOnPath("niftool");
#endif
}

}
