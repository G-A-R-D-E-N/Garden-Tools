#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/app/WikiService.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fo4ide/graph/GraphCompiler.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphScriptLoader.hpp>
#include <fo4ide/graph/GraphToolService.hpp>
#include <fo4ide/graph/NodeDefinition.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

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

bool isWhitespace(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
}

std::string stripQuotes(std::string value) {
    value = trim(std::move(value));
    while (!value.empty() && value.front() == '"') value.erase(value.begin());
    while (!value.empty() && value.back() == '"') value.pop_back();
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           toLower(std::string(value.substr(value.size() - suffix.size()))) == toLower(std::string(suffix));
}

// Mirrors PapyrusService.ExtractScriptName: the first non-comment line, if it
// starts with ScriptName, contributes its second token.
std::string extractScriptName(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        auto l = trim(line);
        if (l.empty() || l.front() == ';') continue;
        if (toLower(l).rfind("scriptname ", 0) == 0) {
            std::istringstream tokens(l);
            std::string token;
            std::vector<std::string> parts;
            while (tokens >> token) parts.push_back(token);
            if (parts.size() >= 2) return parts[1];
        }
        break;
    }
    return "";
}

std::string readFileText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
}

void writeFileText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not open '" + path.string() + "' for writing.");
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) throw std::runtime_error("Could not write '" + path.string() + "'.");
}

std::vector<std::filesystem::path> enumeratePexFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> stack{root};
    std::error_code ec;
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code statusEc;
            const auto& entry = *it;
            if (entry.is_symlink(statusEc)) continue;
            if (entry.is_directory(statusEc)) {
                stack.push_back(entry.path());
            } else if (entry.is_regular_file(statusEc) &&
                       endsWithIgnoreCase(entry.path().extension().string(), ".pex")) {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

std::vector<std::filesystem::path> enumeratePscFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> stack{root};
    std::error_code ec;
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code statusEc;
            const auto& entry = *it;
            if (entry.is_symlink(statusEc)) continue;
            if (entry.is_directory(statusEc)) {
                stack.push_back(entry.path());
            } else if (entry.is_regular_file(statusEc) &&
                       endsWithIgnoreCase(entry.path().extension().string(), ".psc")) {
                files.push_back(entry.path());
            }
        }
    }
    return files;
}

// Mirrors PapyrusService.NamespacedOutPath: namespaced scripts (Foo:Bar) are
// written into namespace subfolders so they recompile as-is.
std::string namespacedOutPath(const std::string& outDir, const std::string& text,
                              const std::string& fallbackBaseName, const std::string& ext,
                              bool assembly) {
    std::filesystem::path rel = fallbackBaseName + ext;
    if (!assembly) {
        const auto obj = extractScriptName(text);
        if (!obj.empty()) {
            rel = std::filesystem::path{};
            std::istringstream stream(obj);
            std::string part;
            while (std::getline(stream, part, ':')) {
                if (!part.empty()) rel /= part;
            }
            rel += ext;
        }
    }
    const auto full = std::filesystem::path(outDir) / rel;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    return full.string();
}

std::optional<std::filesystem::path> stageNamespacedFolder(const std::filesystem::path& folder) {
    const auto files = enumeratePscFiles(folder);
    if (files.empty()) return std::nullopt;

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging = std::filesystem::temp_directory_path() /
        ("FO4RE_PapyrusStage_" + std::to_string(ticks));
    try {
        for (const auto& file : files) {
            std::string obj;
            try {
                obj = extractScriptName(readFileText(file));
            } catch (...) {
            }
            std::filesystem::path rel;
            if (obj.find(':') != std::string::npos) {
                std::istringstream stream(obj);
                std::string part;
                while (std::getline(stream, part, ':')) {
                    if (!part.empty()) rel /= part;
                }
                rel += ".psc";
            } else {
                rel = file.filename();
            }
            const auto dest = staging / rel;
            std::filesystem::create_directories(dest.parent_path());
            std::filesystem::copy_file(file, dest, std::filesystem::copy_options::overwrite_existing);
        }
        return staging;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        return std::nullopt;
    }
}

struct CiLess {
    bool operator()(const std::string& left, const std::string& right) const {
        return toLower(left) < toLower(right);
    }
};

// Mirrors PapyrusService.DiagnoseDependencies: mines unresolved symbols out of
// PapyrusCompiler.exe output and maps known script extenders to Nexus pages.
std::string diagnoseDependencies(const std::string& compilerOutput) {
    const std::regex patterns[] = {
        std::regex("unknown type (\\S+)"),
        std::regex("(\\S+) is not a known user-defined script type"),
        std::regex("cannot convert to unknown type (\\S+)"),
        std::regex("\\b([A-Za-z_]\\w*) is not a function or does not exist"),
        std::regex("[Ss]cript '?([A-Za-z_][\\w:]*)'? (?:does not exist|could not be "
                   "found|is not imported)"),
    };

    std::set<std::string, CiLess> symbols;
    for (const auto& pattern : patterns) {
        std::smatch match;
        auto cursor = compilerOutput.cbegin();
        while (std::regex_search(cursor, compilerOutput.cend(), match, pattern)) {
            auto symbol = trim(match[1].str());
            while (!symbol.empty() && (symbol.front() == '"' || symbol.front() == '\'')) {
                symbol.erase(symbol.begin());
            }
            while (!symbol.empty() && (symbol.back() == '"' || symbol.back() == '\'')) {
                symbol.pop_back();
            }
            if (!symbol.empty()) symbols.insert(symbol);
            cursor = match.suffix().first;
        }
    }
    if (symbols.empty()) return "";

    struct KnownExtender {
        std::string_view match;
        std::string_view name;
        std::string_view url;
    };
    constexpr KnownExtender kKnownExtenders[] = {
        {"hudframework", "HUDFramework", "https://www.nexusmods.com/fallout4/mods/20309"},
        {"workshopframework", "Workshop Framework", "https://www.nexusmods.com/fallout4/mods/35004"},
        {"wsfw", "Workshop Framework", "https://www.nexusmods.com/fallout4/mods/35004"},
        {"mcm", "Mod Configuration Menu", "https://www.nexusmods.com/fallout4/mods/21497"},
        {"baka", "Baka Framework", "https://www.nexusmods.com/fallout4/search/?gsearch=Baka+Framework"
                                  "&gsearchtype=mods"},
    };

    const auto escape = [](std::string_view value) {
        std::ostringstream out;
        for (const unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out << static_cast<char>(c);
            } else {
                out << '%' << std::uppercase << std::hex << static_cast<int>(c) << std::nouppercase
                    << std::dec;
            }
        }
        return out.str();
    };

    std::ostringstream sb;
    sb << "--- DEPENDENCY HELP ---\n";
    sb << "F4SE + vanilla base scripts are already on the import path. A remaining unresolved\n";
    sb << "type/function usually means another script extender / framework is required:\n";
    sb << "add that mod's Data\\Scripts\\Source folder to the Import roots and recompile.\n\n";
    for (const auto& symbol : symbols) {
        const auto low = toLower(symbol);
        const KnownExtender* known = nullptr;
        for (const auto& candidate : kKnownExtenders) {
            if (low.find(candidate.match) != std::string::npos) {
                known = &candidate;
                break;
            }
        }
        if (known) {
            sb << "  - " << symbol << "  ->  likely " << known->name << ".  Nexus: " << known->url
               << "\n";
        } else if (low.find(':') != std::string::npos) {
            sb << "  - " << symbol << "  ->  another script in the SAME mod (namespaced). Make sure "
                  "its .psc/.pex is present in the source folder (some mods ship helper scripts as "
                  "source-only, with no .pex to decompile).\n";
        } else {
            sb << "  - " << symbol << "  ->  unknown source (a script extender plugin or framework). "
                  "Search Nexus: https://www.nexusmods.com/fallout4/search/?gsearch=" << escape(symbol)
               << "&gsearchtype=mods\n";
        }
    }
    return sb.str();
}

enum class PapyrusEngine { Auto, BuiltIn, CreationKit };

PapyrusEngine parseEngine(std::string name) {
    name = toLower(trim(std::move(name)));
    if (name == "builtin" || name == "built-in" || name == "native" || name == "internal") {
        return PapyrusEngine::BuiltIn;
    }
    if (name == "creationkit" || name == "creation-kit" || name == "ck" || name == "external") {
        return PapyrusEngine::CreationKit;
    }
    return PapyrusEngine::Auto;
}

std::string firstLines(std::string text, std::size_t limit) {
    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line) && lines.size() < limit) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    std::ostringstream joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) joined << '\n';
        joined << lines[i];
    }
    return joined.str();
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

NifTarget::NifTarget(assets::NifService& service,
                     assets::TextureResolver& textures,
                     platform::Services& platform)
    : service_(service), textures_(textures), platform_(platform) {}

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
    // Mirrors the C# NifInterop.GetTexture: a PNG data URL for the texture
    // referenced by a NIF (loose file or BA2 under the configured roots),
    // decoded in-process without texconv.
    if (method == "GetTexture") {
        return nlohmann::json(textures_.getTexturePngDataUrl(
            stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2)));
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

CellTarget::CellTarget(plugin::IPluginBackend& backend,
                       assets::NifService& nif,
                       assets::TextureResolver& textures)
    : records_(backend, "cell"), nif_(nif), textures_(textures) {}

Result<nlohmann::json> CellTarget::invoke(std::string_view method,
                                          const nlohmann::json& args) {
    // The three asset ops mirror the C# CellInterop: each returns a serialized
    // JSON string (the cell panel JSON.parses the result). Everything else is
    // a record query delegated to the Radicle "cell" backend facet unchanged.
    if (method == "GetGeometryBatch") {
        std::vector<std::string> paths;
        try {
            const auto parsed = nlohmann::json::parse(stringArgument(args, 0));
            if (parsed.is_array()) {
                for (const auto& entry : parsed) {
                    if (entry.is_string()) paths.push_back(entry.get<std::string>());
                }
            }
        } catch (const std::exception& ex) {
            return nlohmann::json(
                nlohmann::json{{"error", "Error: " + std::string(ex.what())}}.dump());
        }
        return nlohmann::json(geometryBatch(paths));
    }
    if (method == "GetGeometryBatchProgress") {
        return nlohmann::json(nlohmann::json{
            {"done", geoBatchDone_.load(std::memory_order_relaxed)},
            {"total", geoBatchTotal_.load(std::memory_order_relaxed)}}.dump());
    }
    // Mirrors CellInterop.GetTexture: resolve the model to a NIF, then hand the
    // resolved path to the texture resolver; an unresolved model yields "".
    if (method == "GetTexture") {
        const auto resolved = textures_.resolveNif(stringArgument(args, 0));
        if (!resolved) return nlohmann::json(std::string{});
        return nlohmann::json(
            textures_.getTexturePngDataUrl(resolved->string(), stringArgument(args, 1), ""));
    }
    return records_.invoke(method, args);
}

// Faithful port of C# NifService.GeoBatch: case-insensitive dedupe, bounded
// parallelism (min(cores, 8)), each mesh resolved then run through `niftool
// geo`, its JSON object embedded as-is or replaced with an { error } entry.
std::string CellTarget::geometryBatch(const std::vector<std::string>& relModelPaths) {
    std::vector<std::string> unique;
    {
        std::set<std::string> seen;
        for (const auto& raw : relModelPaths) {
            auto path = trim(raw);
            if (path.empty()) continue;
            if (seen.insert(toLower(path)).second) unique.push_back(std::move(path));
        }
    }

    const int total = static_cast<int>(unique.size());
    geoBatchTotal_.store(total, std::memory_order_relaxed);
    geoBatchDone_.store(0, std::memory_order_relaxed);

    // One result slot per unique path: each worker writes only its own slot, so
    // the vector needs no lock; only the work index and progress counters are
    // shared, and those are atomic.
    std::vector<nlohmann::json> results(unique.size());

    const auto processOne = [&](std::size_t i) {
        const auto resolved = textures_.resolveNif(unique[i]);
        if (!resolved) {
            results[i] = nlohmann::json{{"error", "Not found (loose or in any indexed archive)."}};
            geoBatchDone_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const auto geo = nif_.geometry(*resolved);
        const std::string raw = geo ? *geo : ("Error: " + geo.error().message);
        // Embed the niftool geo JSON object as-is when the output is a JSON
        // object (mirrors the C# JRaw path); otherwise surface it as an error
        // entry. Unlike the C#, the parse is validated so a malformed but
        // '{'-leading output cannot corrupt the whole batch payload.
        const auto trimmed = trim(raw);
        nlohmann::json entry;
        bool ok = !trimmed.empty() && trimmed.front() == '{';
        if (ok) {
            entry = nlohmann::json::parse(raw, nullptr, false);
            ok = !entry.is_discarded();
        }
        results[i] = ok ? std::move(entry) : nlohmann::json{{"error", raw}};
        geoBatchDone_.fetch_add(1, std::memory_order_relaxed);
    };

    unsigned int hardware = std::thread::hardware_concurrency();
    if (hardware == 0) hardware = 1;
    const unsigned int degree = std::min<unsigned int>(
        {hardware, 8u, static_cast<unsigned int>(std::max<std::size_t>(unique.size(), 1))});

    if (degree <= 1) {
        for (std::size_t i = 0; i < unique.size(); ++i) processOne(i);
    } else {
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(degree);
        for (unsigned int w = 0; w < degree; ++w) {
            workers.emplace_back([&] {
                for (;;) {
                    const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= unique.size()) break;
                    processOne(i);
                }
            });
        }
        for (auto& worker : workers) worker.join();
    }

    nlohmann::json geometryMap = nlohmann::json::object();
    for (std::size_t i = 0; i < unique.size(); ++i) {
        geometryMap[unique[i]] = std::move(results[i]);
    }
    nlohmann::json payload;
    payload["count"] = total;
    payload["geometry"] = std::move(geometryMap);
    return payload.dump();
}

PapyrusTarget::PapyrusTarget(process::Runner& runner,
                             tools::Locator& locator,
                             platform::Services& platform)
    : runner_(runner), locator_(locator), platform_(platform) {}

Result<nlohmann::json> PapyrusTarget::invoke(std::string_view method,
                                             const nlohmann::json& args) {
    if (method == "BrowseForFile") {
        return pathResult(platform_.pickFile({stringArgument(args, 0), stringArgument(args, 1), {}}));
    }
    if (method == "BrowseForFolder") {
        return pathResult(platform_.pickFolder({stringArgument(args, 0), {}, {}}));
    }
    if (method == "Compile") return compile(args);
    if (method == "Decompile") return decompile(args);
    if (method == "Check") {
        try {
            const auto source = stripQuotes(stringArgument(args, 0));
            const auto* semanticValue = argument(args, 1);
            const bool semantic = !semanticValue || semanticValue->is_null() || boolArgument(args, 1);
            const auto imports = stripQuotes(stringArgument(args, 2));
            const std::string* importsPtr = imports.empty() ? nullptr : &imports;
            return nlohmann::json(papyrus::AnalysisService::check(source, true, semantic, importsPtr));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "Analyze") {
        try {
            const auto text = stringArgument(args, 0);
            const auto path = stringArgument(args, 1);
            const std::string* pathPtr = isWhitespace(path) ? nullptr : &path;
            return nlohmann::json(papyrus::AnalysisService::analyzeJson(text, pathPtr));
        } catch (const std::exception& ex) {
            return nlohmann::json(nlohmann::json{{"error", ex.what()}}.dump());
        }
    }
    if (method == "SymbolAt") {
        try {
            const auto text = stringArgument(args, 0);
            const auto path = stringArgument(args, 1);
            const auto* offsetValue = argument(args, 2);
            const auto offset = offsetValue && offsetValue->is_number() ? offsetValue->get<int>() : 0;
            const auto imports = stringArgument(args, 3);
            const std::string* pathPtr = isWhitespace(path) ? nullptr : &path;
            const std::string* importsPtr = isWhitespace(imports) ? nullptr : &imports;
            return nlohmann::json(papyrus::AnalysisService::symbolAtJson(
                text, pathPtr, offset, importsPtr));
        } catch (const std::exception& ex) {
            return nlohmann::json(nlohmann::json{{"error", ex.what()}}.dump());
        }
    }
    if (method == "ReadScript") {
        const auto path = trim(stringArgument(args, 0));
        if (path.empty()) return nlohmann::json("ERR:No path.");
        if (!std::filesystem::is_regular_file(path)) return nlohmann::json("ERR:Not found: " + path);
        try {
            return nlohmann::json(readFileText(path));
        } catch (const std::exception& ex) {
            return nlohmann::json("ERR:" + std::string(ex.what()));
        }
    }
    if (method == "WriteScript") {
        auto path = trim(stringArgument(args, 0));
        if (path.empty()) return nlohmann::json("ERR:No path.");
        if (!endsWithIgnoreCase(path, ".psc")) {
            return nlohmann::json("ERR:Refusing to write '" +
                                  std::filesystem::path(path).filename().string() +
                                  "' -- this saves Papyrus source, so the path must end in .psc.");
        }
        try {
            const auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }
            writeFileText(path, stringArgument(args, 1));
            return nlohmann::json("");
        } catch (const std::exception& ex) {
            return nlohmann::json("ERR:" + std::string(ex.what()));
        }
    }
    if (method == "OpenFolder") {
        auto path = stripQuotes(stringArgument(args, 0));
        if (path.empty()) return nlohmann::json("No path.");
        if (std::filesystem::is_regular_file(path)) {
            path = std::filesystem::path(path).parent_path().string();
        }
        if (!std::filesystem::is_directory(path)) return nlohmann::json("Folder not found: " + path);
        auto opened = platform_.openFolder(path);
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    if (method == "LookupFunction") {
        try {
            const auto root = locator_.resolve(tools::Tool::CkWiki).string();
            return nlohmann::json(wiki::lookupFunction(root, stringArgument(args, 0),
                                                       stringArgument(args, 1)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "LookupScriptInfo") {
        try {
            const auto root = locator_.resolve(tools::Tool::CkWiki).string();
            return nlohmann::json(wiki::lookupScriptInfo(root, stringArgument(args, 0)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    return std::unexpected(Error{"UnknownMethod", "Unknown Papyrus RPC method: " + std::string(method)});
}

// Port of PapyrusService.Compile: validation, engine selection, then either the
// built-in engine (PapyrusAnalysisService.Compile) or the Creation Kit process
// path when a PapyrusCompiler.exe is configured.
Result<nlohmann::json> PapyrusTarget::compile(const nlohmann::json& args) {
    auto source = stripQuotes(stringArgument(args, 0));
    if (source.empty()) {
        return nlohmann::json("Provide 'source' (a .psc file or a folder of scripts).");
    }
    const bool isDir = std::filesystem::is_directory(source);
    const bool isFile = std::filesystem::is_regular_file(source);
    if (!isDir && !isFile) return nlohmann::json("Source not found: " + source);

    const auto engine = parseEngine(stringArgument(args, 8));
    auto exePath = stripQuotes(stringArgument(args, 7));
    if (exePath.empty()) exePath = locator_.resolve(tools::Tool::PapyrusCompiler).string();
    const bool haveCreationKit = !exePath.empty() && std::filesystem::is_regular_file(exePath);

    if (engine == PapyrusEngine::BuiltIn || (engine == PapyrusEngine::Auto && !haveCreationKit)) {
        const auto output = stripQuotes(stringArgument(args, 1));
        const auto imports = stripQuotes(stringArgument(args, 2));
        const auto flags = stripQuotes(stringArgument(args, 3));
        const bool optimize = boolArgument(args, 5);
        const bool release = boolArgument(args, 6);
        const auto* debugInfoValue = argument(args, 9);
        const bool debugInfo =
            (!debugInfoValue || debugInfoValue->is_null() || !debugInfoValue->is_boolean())
                ? true
                : debugInfoValue->get<bool>();
        const std::string* outputPtr = output.empty() ? nullptr : &output;
        const std::string* importsPtr = imports.empty() ? nullptr : &imports;
        const std::string* flagsPtr = flags.empty() ? nullptr : &flags;
        auto result = papyrus::AnalysisService::compile(source, outputPtr, importsPtr, release,
                                                        debugInfo, flagsPtr);
        if (engine == PapyrusEngine::Auto) {
            result += "\n\nNo PapyrusCompiler.exe was found, so the built-in compiler was used. "
                      "That is not a fallback to something lesser -- it is measured against the "
                      "Creation Kit's own output on real scripts -- but if you meant to use the CK, "
                      "point at it with compiler_path or " +
                      std::string(locator_.describe(tools::Tool::PapyrusCompiler)) + ".";
        }
        if (optimize) {
            result += "\n\nNote: optimize (-op) is a Creation Kit compiler switch and was ignored.";
        }
        return nlohmann::json(result);
    }

    if (!haveCreationKit) {
        return nlohmann::json("PapyrusCompiler.exe not found, and engine='creationkit' was asked "
                              "for explicitly. Pass compiler_path pointing at it, or " +
                              std::string(locator_.describe(tools::Tool::PapyrusCompiler)) +
                              ". Or use engine='builtin', which needs no Creation Kit.");
    }

    if (isFile && endsWithIgnoreCase(source, ".pas")) {
        return nlohmann::json("That's a Papyrus ASSEMBLY file (.pas), not source. PapyrusCompiler "
                              "compiles .psc SOURCE.\nFix: Decompile this script again with "
                              "'Assembly listing' UNCHECKED to get a .psc, then compile that.\n"
                              "(.pas is only used by PapyrusAssembler.exe, a separate tool, and is "
                              "not what you recompile.)");
    }

    // workDir / target, mirroring the C# layout sniffing.
    std::string workDir;
    std::string target;
    std::optional<std::filesystem::path> stagingDir;
    if (isDir) {
        stagingDir = stageNamespacedFolder(source);
        workDir = stagingDir ? stagingDir->string() : source;
        target = workDir;
    } else {
        const auto full = std::filesystem::absolute(source);
        const auto fileDir = full.parent_path();
        std::string objName;
        try {
            objName = extractScriptName(readFileText(full));
        } catch (...) {
        }
        std::vector<std::string> segments;
        {
            std::istringstream stream(objName);
            std::string part;
            while (std::getline(stream, part, ':')) {
                if (!part.empty()) segments.push_back(part);
            }
        }
        const auto depth = static_cast<int>(segments.size()) - 1;
        std::filesystem::path root = fileDir;
        bool layoutMatches = depth > 0;
        for (int i = 0; i < depth; ++i) {
            if (toLower(root.filename().string()) !=
                toLower(segments[static_cast<std::size_t>(depth - 1 - i)])) {
                layoutMatches = false;
                break;
            }
            root = root.parent_path();
            if (root.empty()) {
                layoutMatches = false;
                break;
            }
        }
        if (layoutMatches) {
            workDir = root.string();
            target = objName;
        } else {
            workDir = fileDir.string();
            target = full.filename().string();
        }
    }

    std::vector<std::string> imp;
    const auto importsText = stringArgument(args, 2);
    if (!isWhitespace(importsText)) {
        std::istringstream stream(importsText);
        std::string part;
        while (std::getline(stream, part, ';')) {
            auto cleaned = stripQuotes(part);
            if (!cleaned.empty()) imp.push_back(std::move(cleaned));
        }
    }
    for (const auto& base : locator_.papyrusBaseImports()) imp.push_back(base.string());
    imp.push_back(workDir);

    std::vector<std::string> distinct;
    std::set<std::string, CiLess> seen;
    for (auto& entry : imp) {
        if (seen.insert(entry).second) distinct.push_back(std::move(entry));
    }
    std::ostringstream importArg;
    for (std::size_t i = 0; i < distinct.size(); ++i) {
        if (i) importArg << ';';
        importArg << distinct[i];
    }

    const auto outArg = stripQuotes(stringArgument(args, 1));
    const auto outDir = outArg.empty() ? (isDir ? source : workDir) : outArg;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        return nlohmann::json("Cannot create output dir '" + outDir + "': " + ec.message());
    }

    auto flags = stripQuotes(stringArgument(args, 3));
    if (flags.empty()) flags = "Institute_Papyrus_Flags.flg";
    const bool all = boolArgument(args, 4);
    const bool optimize = boolArgument(args, 5);
    const bool release = boolArgument(args, 6);

    std::vector<std::string> processArgs{
        target, "-f=\"" + flags + "\"", "-i=\"" + importArg.str() + "\"",
        "-o=\"" + outDir + "\""};
    if (all || isDir) processArgs.push_back("-all");
    if (optimize) processArgs.push_back("-op");
    if (release) processArgs.push_back("-r");

    std::ostringstream displayArgs;
    for (std::size_t i = 0; i < processArgs.size(); ++i) {
        if (i) displayArgs << ' ';
        displayArgs << (i == 0 ? "\"" + processArgs[i] + "\"" : processArgs[i]);
    }

    process::Request request;
    request.executable = exePath;
    request.arguments = processArgs;
    request.workingDirectory = workDir;
    request.timeout = std::chrono::seconds(300);
    auto run = runner_.run(request);
    if (!run) return nlohmann::json("Failed to run the Papyrus compiler: " + run.error().message);
    if (!run->started) return nlohmann::json("Failed to start the Papyrus compiler process.");
    if (run->timedOut) return nlohmann::json("Papyrus compile timed out after 300s (killed).");

    std::string combined;
    combined.reserve(run->stdOut.size() + run->stdErr.size() + 1);
    for (const char c : run->stdOut + "\n" + run->stdErr) {
        if (c != '\r') combined.push_back(c);
    }

    std::ostringstream sb;
    std::string batch;
    {
        std::istringstream lines(combined);
        std::string line;
        while (std::getline(lines, line)) {
            if (toLower(line).find("batch compile") != std::string::npos) batch = line;
        }
    }
    std::smatch batchMatch;
    if (std::regex_search(batch, batchMatch, std::regex(R"((\d+)\s+succeeded,\s+(\d+)\s+failed)"))) {
        sb << "RESULT: " << batchMatch[1].str() << " succeeded, " << batchMatch[2].str()
           << " failed (output -> " << outDir << ")\n";
    } else {
        sb << "RESULT: " << (run->exitCode == 0 ? "success" : "FAILED")
           << " (output -> " << outDir << ")\n";
    }
    sb << "\ncompiler: " << exePath << "\n";
    sb << "workdir : " << workDir << "\n";
    sb << "args    : " << displayArgs.str() << "\n\n";

    std::vector<std::string> important;
    {
        std::istringstream lines(combined);
        std::string line;
        while (std::getline(lines, line)) {
            if (trim(line).empty()) continue;
            const auto low = toLower(line);
            if (low.find("error") != std::string::npos ||
                low.find("compilation succeeded") != std::string::npos ||
                low.find("failed") != std::string::npos ||
                low.find("no output generated") != std::string::npos ||
                low.find("batch compile") != std::string::npos) {
                important.push_back(line);
            }
        }
    }
    if (!important.empty()) {
        for (const auto& line : important) sb << line << "\n";
    } else if (!trim(combined).empty()) {
        sb << trim(combined) << "\n";
    }
    sb << "\nexit code: " << run->exitCode << " (" << (run->exitCode == 0 ? "success" : "FAILED")
       << ")\n";
    if (run->exitCode != 0) {
        const auto hint = diagnoseDependencies(combined);
        if (!hint.empty()) sb << "\n" << hint;
    }
    if (stagingDir) {
        std::error_code cleanupEc;
        std::filesystem::remove_all(*stagingDir, cleanupEc);
    }
    return nlohmann::json(sb.str());
}

// Port of PapyrusService.Decompile: single file (write or NOT SAVED preview) or
// a whole folder of .pex, with namespaced output paths so scripts recompile
// as-is.
Result<nlohmann::json> PapyrusTarget::decompile(const nlohmann::json& args) {
    const auto source = stripQuotes(stringArgument(args, 0));
    if (source.empty()) {
        return nlohmann::json("Provide 'source' (a .pex file or a folder of .pex).");
    }
    const bool assembly = boolArgument(args, 2);
    const bool write = boolArgument(args, 3);
    const auto ext = assembly ? ".pas" : ".psc";

    if (std::filesystem::is_regular_file(source)) {
        std::string text;
        try {
            const auto bytes = readFileBytes(source);
            const auto pex = papyrus::PexFile::fromBytes(bytes);
            text = assembly ? papyrus::Decompiler::disassemble(pex)
                            : papyrus::Decompiler::toSource(pex);
        } catch (const std::exception& ex) {
            return nlohmann::json("Decompile failed for " +
                                  std::filesystem::path(source).filename().string() + ": " +
                                  ex.what());
        }

        const auto output = stripQuotes(stringArgument(args, 1));
        const bool doWrite = write || !output.empty();
        if (!doWrite) {
            return nlohmann::json(text +
                                  "\n\n; ---------------------------------------------------------------\n"
                                  "; NOT SAVED - shown in the OUTPUT pane only. Tick 'Write files to disk'\n"
                                  "; (or set an Output folder) to save this as a .psc.");
        }
        const auto outDir = output.empty()
                                ? std::filesystem::absolute(source).parent_path().string()
                                : output;
        std::string outPath;
        try {
            outPath = namespacedOutPath(outDir, text,
                                        std::filesystem::path(source).stem().string(), ext,
                                        assembly);
            writeFileText(outPath, text);
        } catch (const std::exception& ex) {
            return nlohmann::json("Cannot write '" + outPath + "': " + ex.what());
        }
        return nlohmann::json("SAVED -> " + outPath + "\n\n--- preview (first 40 lines) ---\n" +
                              firstLines(text, 40));
    }

    if (std::filesystem::is_directory(source)) {
        const auto files = enumeratePexFiles(source);
        if (files.empty()) {
            return nlohmann::json("RESULT: 0 decompiled. No .pex files found under " + source +
                                  ".");
        }
        const auto output = stripQuotes(stringArgument(args, 1));
        const auto outDir = output.empty() ? source : output;
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        if (ec) return nlohmann::json("Cannot create output dir '" + outDir + "': " + ec.message());

        int ok = 0;
        std::vector<std::string> fails;
        for (const auto& file : files) {
            try {
                const auto bytes = readFileBytes(file);
                const auto pex = papyrus::PexFile::fromBytes(bytes);
                const auto text = assembly ? papyrus::Decompiler::disassemble(pex)
                                           : papyrus::Decompiler::toSource(pex);
                const auto outPath = namespacedOutPath(outDir, text, file.stem().string(), ext,
                                                       assembly);
                writeFileText(outPath, text);
                ++ok;
            } catch (const std::exception& ex) {
                fails.push_back(file.filename().string() + ": " + ex.what());
            }
        }

        std::ostringstream msg;
        msg << "RESULT: " << ok << "/" << files.size() << " decompiled -> " << ext << "\n"
            << "OUTPUT: " << outDir << "\n"
            << "Namespaced scripts were written into namespace subfolders so they recompile as-is "
               "(point Compile at this folder with 'Compile all').";
        if (!fails.empty()) {
            msg << "\nFailures (" << fails.size() << "):\n  ";
            const auto shown = std::min<std::size_t>(20, fails.size());
            for (std::size_t i = 0; i < shown; ++i) {
                if (i) msg << "\n  ";
                msg << fails[i];
            }
        }
        return nlohmann::json(msg.str());
    }

    return nlohmann::json("Source not found: " + source);
}

Result<nlohmann::json> PapyrusTarget::stageDroppedFile(const nlohmann::json& args) const {
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
        stream.write(reinterpret_cast<const char*>(decoded->data()),
                     static_cast<std::streamsize>(decoded->size()));
        stream.close();
        if (!stream) return nlohmann::json("ERR:Could not write staged file");
        return nlohmann::json(path.string());
    } catch (const std::exception& ex) {
        return nlohmann::json("ERR:" + std::string(ex.what()));
    }
}

namespace {

bool graphIEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not open '" + path.string() + "' for writing.");
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("Could not write '" + path.string() + "'.");
}

// Port of GraphInterop.Roots(): the Papyrus base-import directories that exist.
std::vector<std::filesystem::path> graphScriptRoots() {
    std::vector<std::filesystem::path> roots;
    std::error_code errorCode;
    for (const auto& root : papyrus::papyrusBaseImports()) {
        if (std::filesystem::is_directory(root, errorCode)) roots.push_back(root);
    }
    return roots;
}

// Port of GraphInterop.Describe(NodeDefinition).
nlohmann::json describeNodeDefinition(const graph::NodeDefinition& definition) {
    nlohmann::json pins = nlohmann::json::array();
    for (const auto& pin : definition.pins) {
        nlohmann::json entry;
        entry["id"] = pin.id;
        entry["name"] = pin.label.empty() ? pin.id : pin.label;
        entry["kind"] = pin.kind == graph::PinKind::Exec ? "exec" : "data";
        entry["dir"] = pin.direction == graph::PinDirection::In ? "in" : "out";
        entry["dataType"] = pin.type ? pin.type->toString() : std::string{};
        entry["optional"] = pin.isOptional;
        if (pin.declaredDefault) entry["defaultLiteral"] = *pin.declaredDefault;
        if (pin.description) entry["description"] = *pin.description;
        pins.push_back(std::move(entry));
    }

    nlohmann::json out;
    out["type"] = definition.id;
    out["label"] = definition.title;
    out["category"] = definition.category;
    out["kind"] = std::string(graph::graphNodeKindName(definition.kind));
    if (definition.summary) out["summary"] = *definition.summary;
    out["isPure"] = definition.isPure;
    out["isGlobal"] = definition.isGlobal;
    if (definition.ownerScript) out["script"] = *definition.ownerScript;
    out["pins"] = std::move(pins);
    return out;
}

// Port of GraphInterop.Describe(GraphDiagnostic).
nlohmann::json describeGraphDiagnostic(const graph::GraphDiagnostic& diagnostic) {
    nlohmann::json out;
    out["code"] = diagnostic.code;
    out["severity"] = diagnostic.severity == graph::GraphSeverity::Error ? "error" : "warning";
    out["message"] = diagnostic.message;
    if (diagnostic.nodeId) out["nodeId"] = *diagnostic.nodeId;
    if (diagnostic.pinId) out["pinId"] = *diagnostic.pinId;
    if (diagnostic.wireId) out["wireId"] = *diagnostic.wireId;
    out["relatedNodes"] = diagnostic.relatedNodes;
    out["line"] = diagnostic.sourceLine;
    return out;
}

// Port of GraphInterop.Diagnostics(): the { diagnostics, ok } payload.
std::string graphDiagnosticsJson(const std::vector<graph::GraphDiagnostic>& diagnostics) {
    nlohmann::json array = nlohmann::json::array();
    bool anyError = false;
    for (const auto& diagnostic : diagnostics) {
        array.push_back(describeGraphDiagnostic(diagnostic));
        if (diagnostic.severity == graph::GraphSeverity::Error) anyError = true;
    }
    nlohmann::json out;
    out["diagnostics"] = std::move(array);
    out["ok"] = !anyError;
    return out.dump(2);
}

std::vector<graph::GraphDiagnostic> singleDiagnostic(
    const std::optional<graph::GraphDiagnostic>& diagnostic) {
    if (diagnostic) return {*diagnostic};
    return {};
}

}  // namespace

GraphTarget::GraphTarget(platform::Services& platform) : platform_(platform) {}

GraphTarget::~GraphTarget() = default;

graph::GraphCompiler& GraphTarget::compiler() {
    std::string key;
    for (const auto& root : graphScriptRoots()) {
        key += root.string();
        key.push_back('|');
    }
    if (!compiler_ || key != rootsKey_) {
        index_ = std::make_unique<papyrus::ScriptIndex>(graph::graphIndexFor(nullptr, nullptr));
        compiler_ = std::make_unique<graph::GraphCompiler>(*index_);
        rootsKey_ = std::move(key);
    }
    return *compiler_;
}

Result<nlohmann::json> GraphTarget::invoke(std::string_view method, const nlohmann::json& args) {
    // Serialize the whole target: the RPC dispatcher runs invoke() concurrently,
    // and compiler()/Refresh mutate index_/compiler_/rootsKey_.
    std::scoped_lock lock(mutex_);
    try {
        if (method == "Refresh") {
            compiler_.reset();
            index_.reset();
            rootsKey_.clear();
            return nlohmann::json(nullptr);
        }
        if (method == "GetCorePalette") {
            auto& palette = compiler().palette();
            nlohmann::json builtins = nlohmann::json::array();
            for (const auto& definition : palette.builtins()) {
                builtins.push_back(describeNodeDefinition(definition));
            }
            auto scripts = palette.scriptNames();
            std::sort(scripts.begin(), scripts.end(),
                      [](const std::string& a, const std::string& b) {
                          return std::lexicographical_compare(
                              a.begin(), a.end(), b.begin(), b.end(),
                              [](unsigned char x, unsigned char y) {
                                  return std::tolower(x) < std::tolower(y);
                              });
                      });
            const auto stats = palette.wikiStats();
            nlohmann::json out;
            out["version"] = graph::GraphDocument::kCurrentSchema;
            out["builtins"] = std::move(builtins);
            out["scripts"] = scripts;
            out["wiki"] = {{"available", stats.available()}, {"pages", stats.pagesIndexed}};
            return nlohmann::json(out.dump(2));
        }
        if (method == "SearchPalette") {
            const auto kind = stringArgument(args, 0);
            const auto query = stringArgument(args, 1);
            const auto scriptFilter = stringArgument(args, 2);
            const auto* limitValue = argument(args, 3);
            const int requested = limitValue && limitValue->is_number() ? limitValue->get<int>() : 0;
            std::optional<std::string_view> filter;
            if (!isWhitespace(scriptFilter)) filter = std::string_view(scriptFilter);
            const auto result = compiler().palette().search(
                std::string_view(query), requested <= 0 ? 50 : requested, filter);

            const bool filterKind = !isWhitespace(kind) && !graphIEquals(kind, "any");
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& entry : result.entries) {
                const auto kindName = std::string(graph::graphNodeKindName(entry.kind));
                if (filterKind && !graphIEquals(kindName, kind)) continue;
                entries.push_back({{"id", entry.id},
                                   {"title", entry.title},
                                   {"category", entry.category},
                                   {"kind", kindName},
                                   {"signature", entry.signature},
                                   {"isPure", entry.isPure}});
            }
            nlohmann::json out;
            out["entries"] = std::move(entries);
            out["total"] = result.total;
            return nlohmann::json(out.dump(2));
        }
        if (method == "GetNodeSignature") {
            const auto nodeType = stringArgument(args, 0);
            const auto* definition = compiler().palette().find(nodeType);
            if (definition == nullptr) {
                return nlohmann::json("Error: no node type '" + nodeType + "' on this palette");
            }
            return nlohmann::json(describeNodeDefinition(*definition).dump(2));
        }
        if (method == "ValidateGraph") {
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 0));
            if (!loaded.document) {
                return nlohmann::json(graphDiagnosticsJson(singleDiagnostic(loaded.error)));
            }
            const auto validation = compiler().validate(*loaded.document);
            return nlohmann::json(graphDiagnosticsJson(validation.diagnostics));
        }
        if (method == "CompileToSource") {
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 0));
            if (!loaded.document) {
                return nlohmann::json(graphDiagnosticsJson(singleDiagnostic(loaded.error)));
            }
            graph::GraphCompileOptions options;
            options.stopAfterSource = true;
            const auto result = compiler().compile(*loaded.document, options);
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& diagnostic : result.diagnostics) {
                diagnostics.push_back(describeGraphDiagnostic(diagnostic));
            }
            nlohmann::json out;
            if (result.source) out["source"] = *result.source;
            out["diagnostics"] = std::move(diagnostics);
            out["ok"] = result.errors().empty();
            return nlohmann::json(out.dump(2));
        }
        if (method == "CompileToPex") {
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 0));
            if (!loaded.document) {
                return nlohmann::json(graphDiagnosticsJson(singleDiagnostic(loaded.error)));
            }
            const auto outputDirectory = stringArgument(args, 1);
            const auto result = compiler().compile(*loaded.document);
            std::optional<std::string> written;
            if (result.success() && !isWhitespace(outputDirectory)) {
                const std::filesystem::path directory(outputDirectory);
                std::filesystem::create_directories(directory);
                const auto& name = loaded.document->header.scriptName;
                const auto pexPath = directory / (name + ".pex");
                writeFileBytes(pexPath, result.pex->toBytes());
                writeFileText(directory / (name + ".psc"), result.source.value_or(""));
                written = pexPath.string();
            }
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& diagnostic : result.diagnostics) {
                diagnostics.push_back(describeGraphDiagnostic(diagnostic));
            }
            nlohmann::json out;
            if (result.source) out["source"] = *result.source;
            out["diagnostics"] = std::move(diagnostics);
            out["ok"] = result.success();
            if (written) out["written"] = *written;
            return nlohmann::json(out.dump(2));
        }
        if (method == "LoadGraph") {
            const auto path = stringArgument(args, 0);
            if (!std::filesystem::is_regular_file(path)) {
                return nlohmann::json("Error: no file at " + path);
            }
            const auto text = readFileText(path);
            auto loaded = graph::documentJson::tryDeserialize(text);
            if (!loaded.document) {
                return nlohmann::json(
                    "Error: " +
                    (loaded.error ? loaded.error->message : std::string("malformed document")));
            }
            return nlohmann::json(text);
        }
        if (method == "LoadScript") {
            const auto result =
                graph::GraphScriptLoader::load(stringArgument(args, 0), graphScriptRoots());
            if (result.failure) return nlohmann::json("Error: " + *result.failure);
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& diagnostic : result.diagnostics) {
                diagnostics.push_back(describeGraphDiagnostic(diagnostic));
            }
            nlohmann::json out;
            out["ok"] = result.success();
            if (result.document) {
                out["document"] =
                    nlohmann::json::parse(graph::documentJson::serialize(*result.document, false));
            }
            out["diagnostics"] = std::move(diagnostics);
            return nlohmann::json(out.dump(2));
        }
        if (method == "SaveGraph") {
            const auto path = stringArgument(args, 0);
            auto loaded = graph::documentJson::tryDeserialize(stringArgument(args, 1));
            if (!loaded.document) {
                return nlohmann::json(
                    "Error: " +
                    (loaded.error ? loaded.error->message : std::string("malformed document")));
            }
            graph::documentJson::saveFile(*loaded.document, path);
            return nlohmann::json("");
        }
        if (method == "BrowseForGraph") {
            platform::FileDialogOptions options;
            options.filter = "Node graph (*.fograph)|*.fograph";
            if (boolArgument(args, 0)) {
                options.title = "Save graph";
                return pathResult(platform_.pickSavePath(options));
            }
            options.title = "Open graph";
            return pathResult(platform_.pickFile(options));
        }
        if (method == "BrowseForScript") {
            platform::FileDialogOptions options;
            options.title = "Open graph or script";
            options.filter =
                "Graph or script (*.fograph;*.psc;*.pex)|*.fograph;*.psc;*.pex"
                "|Node graph (*.fograph)|*.fograph"
                "|Papyrus source (*.psc)|*.psc"
                "|Compiled Papyrus (*.pex)|*.pex";
            return pathResult(platform_.pickFile(options));
        }
        return std::unexpected(
            Error{"UnknownMethod", "Unknown Graph RPC method: " + std::string(method)});
    } catch (const std::exception& ex) {
        return nlohmann::json("Error: " + std::string(ex.what()));
    }
}

}
