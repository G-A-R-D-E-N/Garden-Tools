#include <fo4ide/settings/Settings.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace fo4ide::settings {
namespace {

std::filesystem::path readPath(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) return {};
    return std::filesystem::path(it->get<std::string>());
}

std::string readString(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : std::string{};
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

void applyEnv(std::filesystem::path& value, const char* name) {
    const auto env = environmentValue(name);
    if (!env.empty()) value = env;
}

std::string migrateModel(std::string value) {
    static const std::unordered_map<std::string, std::string> retired{
        {"claude-3-7-sonnet-20250219", "claude-sonnet-4-6"},
        {"claude-3-5-sonnet-20241022", "claude-sonnet-4-6"},
        {"claude-3-5-sonnet-20240620", "claude-sonnet-4-6"},
        {"claude-3-sonnet-20240229", "claude-sonnet-4-6"},
        {"claude-3-5-haiku-20241022", "claude-haiku-4-5"},
        {"claude-3-opus-20240229", "claude-opus-4-8"},
        {"claude-2.1", "claude-sonnet-4-6"},
        {"claude-2.0", "claude-sonnet-4-6"},
    };
    if (const auto it = retired.find(value); it != retired.end()) return it->second;
    return value;
}

}

void from_json(const nlohmann::json& json, Settings& settings) {
    settings.extra = json.is_object() ? json : nlohmann::json::object();
    settings.aiProvider = readString(json, "AiProvider");
    settings.anthropicApiKey = readString(json, "AnthropicApiKey");
    settings.model = migrateModel(readString(json, "Model"));
    settings.geminiApiKey = readString(json, "GeminiApiKey");
    settings.geminiModel = readString(json, "GeminiModel");
    settings.claudeCodePath = readPath(json, "ClaudeCodePath");
    settings.ollamaUrl = readString(json, "OllamaUrl");
    settings.ollamaModel = readString(json, "OllamaModel");
    settings.outputFolder = readPath(json, "OutputFolder");
    settings.fallout4Path = readPath(json, "Fallout4Path");
    settings.dataFolder = readPath(json, "DataFolder");
    settings.mo2InstancePath = readPath(json, "Mo2InstancePath");
    settings.ckWikiPath = readPath(json, "CkWikiPath");
    settings.texconvPath = readPath(json, "TexconvPath");
    settings.papyrusCompilerPath = readPath(json, "PapyrusCompilerPath");
    settings.papyrusBaseImports = readPath(json, "PapyrusBaseImports");
    settings.ffmpegPath = readPath(json, "FfmpegPath");
    settings.xwmaEncodePath = readPath(json, "XwmaEncodePath");
    settings.archive2Path = readPath(json, "Archive2Path");
    settings.readLargePluginsIntoMemory = json.value("ReadLargePluginsIntoMemory", false);

    applyEnv(settings.fallout4Path, "FALLOUT4_PATH");
    applyEnv(settings.dataFolder, "FO4_DATA_PATH");
    applyEnv(settings.ckWikiPath, "CK_WIKI_PATH");
    applyEnv(settings.texconvPath, "TEXCONV_PATH");
    applyEnv(settings.papyrusCompilerPath, "PAPYRUS_COMPILER_PATH");
    applyEnv(settings.papyrusBaseImports, "PAPYRUS_BASE_IMPORTS");
    applyEnv(settings.ffmpegPath, "FFMPEG_PATH");
    applyEnv(settings.xwmaEncodePath, "XWMAENCODE_PATH");
    applyEnv(settings.archive2Path, "ARCHIVE2_PATH");
}

void to_json(nlohmann::json& json, const Settings& settings) {
    json = settings.extra.is_object() ? settings.extra : nlohmann::json::object();
    json["AiProvider"] = settings.aiProvider;
    json["AnthropicApiKey"] = settings.anthropicApiKey;
    json["Model"] = settings.model;
    json["GeminiApiKey"] = settings.geminiApiKey;
    json["GeminiModel"] = settings.geminiModel;
    json["ClaudeCodePath"] = settings.claudeCodePath.string();
    json["OllamaUrl"] = settings.ollamaUrl;
    json["OllamaModel"] = settings.ollamaModel;
    json["OutputFolder"] = settings.outputFolder.string();
    json["Fallout4Path"] = settings.fallout4Path.string();
    json["DataFolder"] = settings.dataFolder.string();
    json["Mo2InstancePath"] = settings.mo2InstancePath.string();
    json["CkWikiPath"] = settings.ckWikiPath.string();
    json["TexconvPath"] = settings.texconvPath.string();
    json["PapyrusCompilerPath"] = settings.papyrusCompilerPath.string();
    json["PapyrusBaseImports"] = settings.papyrusBaseImports.string();
    json["FfmpegPath"] = settings.ffmpegPath.string();
    json["XwmaEncodePath"] = settings.xwmaEncodePath.string();
    json["Archive2Path"] = settings.archive2Path.string();
    json["ReadLargePluginsIntoMemory"] = settings.readLargePluginsIntoMemory;
}

Store::Store(std::filesystem::path path) : path_(std::move(path)) {}

Result<Settings> Store::load() const {
    try {
        if (!std::filesystem::exists(path_)) return Settings{};
        std::ifstream stream(path_, std::ios::binary);
        if (!stream) return std::unexpected(Error{"SettingsReadFailed", "Could not open settings file"});
        nlohmann::json json;
        stream >> json;
        return json.get<Settings>();
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"SettingsReadFailed", ex.what()});
    }
}

Result<void> Store::save(const Settings& settings) const {
    try {
        if (const auto parent = path_.parent_path(); !parent.empty()) std::filesystem::create_directories(parent);
        const auto temp = path_.string() + ".tmp";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            if (!stream) return std::unexpected(Error{"SettingsWriteFailed", "Could not create temporary settings file"});
            nlohmann::json json = settings;
            stream << json.dump(2) << '\n';
            stream.flush();
            if (!stream) return std::unexpected(Error{"SettingsWriteFailed", "Could not flush temporary settings file"});
        }
        std::error_code ec;
        std::filesystem::rename(temp, path_, ec);
        if (!ec) return {};
        std::error_code cleanupEc;
        std::filesystem::remove(temp, cleanupEc);
        return std::unexpected(Error{"SettingsWriteFailed", ec.message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"SettingsWriteFailed", ex.what()});
    }
}

const std::filesystem::path& Store::path() const noexcept {
    return path_;
}

}
