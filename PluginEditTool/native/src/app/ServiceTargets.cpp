#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4/archive/assets.hpp>
#include <fo4/archive/ba2_reader.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/nif/havok_names.hpp>

#include <fo4ide/assets/NifGltfService.hpp>
#include <fo4ide/assets/NifRender.hpp>
#include <fo4ide/assets/NifSkinBinding.hpp>
#include <fo4ide/assets/VertexColorSurface.hpp>

#include <fo4ide/assets/MaterialJson.hpp>
#include <fo4ide/core/StageTimer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <fo4ide/core/PayloadLimits.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <fo4ide/core/JsonInt.hpp>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <fo4ide/assets/NifMaterialLink.hpp>

#include "NifUvOps.hpp"
#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {

using namespace support;

namespace {

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

void updateSecret(const nlohmann::json& payload, const char* key, std::string& target) {
    const auto it = payload.find(key);
    if (it == payload.end() || !it->is_string()) return;
    auto value = trim(it->get<std::string>());
    if (!value.empty()) target = std::move(value);
}

// Constraint: a relative NewPluginFolder or PrevisOutputFolder must be refused before it is stored.
std::string fullPathFolderError(const nlohmann::json& payload, const char* key) {
    const auto it = payload.find(key);
    if (it == payload.end() || !it->is_string()) return {};
    const auto value = trim(it->get<std::string>());
    if (value.empty() || std::filesystem::path(value).is_absolute()) return {};
    return std::string("Invalid ") + key + ": must be a full path or blank: " + value;
}

nlohmann::json redactedSettings(const settings::Settings& current) {
    nlohmann::json value = current;
    value.erase("AnthropicApiKey");
    value.erase("GeminiApiKey");
    value["hasAnthropicApiKey"] = !current.anthropicApiKey.empty();
    value["hasGeminiApiKey"] = !current.geminiApiKey.empty();
    return value;
}

bool resolvesOnPath(const std::filesystem::path& command) {
    const auto resolved = tools::resolveCommand(command);
    std::error_code ec;
    return resolved != command && std::filesystem::is_regular_file(resolved, ec);
}

struct ToolPathRule {
    const char* key;
    std::vector<std::string> fileNames;
    bool allowCommand;
};

std::string toolPathError(const nlohmann::json& payload) {
    const ToolPathRule rules[] = {
        {"PapyrusCompilerPath", {"PapyrusCompiler.exe"}, false},
        {"XwmaEncodePath", {"xWMAEncode.exe"}, false},
        {"Archive2Path", {"Archive2.exe"}, false},
        {"ClaudeCodePath", {}, true},
        {"FfmpegPath", {}, false},
    };
    for (const auto& rule : rules) {
        const auto it = payload.find(rule.key);
        if (it == payload.end() || !it->is_string()) continue;
        const auto value = trim(it->get<std::string>());
        if (value.empty()) continue;
        const std::filesystem::path path(value);
        std::error_code ec;
        const bool exists = (rule.allowCommand && path.parent_path().empty())
                                ? resolvesOnPath(path)
                                : std::filesystem::is_regular_file(path, ec);
        if (!exists) {
            return std::string("Invalid ") + rule.key + ": not an existing file" +
                   (rule.allowCommand ? " or a command on PATH" : "") + ": " + value;
        }
        if (rule.fileNames.empty()) continue;
        const auto name = toLower(path.filename().string());
        const bool named = std::any_of(rule.fileNames.begin(), rule.fileNames.end(),
                                       [&](const std::string& expected) { return toLower(expected) == name; });
        if (named) continue;
        std::string expected;
        for (const auto& candidate : rule.fileNames) expected += (expected.empty() ? "" : " or ") + candidate;
        return std::string("Invalid ") + rule.key + ": expected " + expected;
    }
    return {};
}

nlohmann::json serviceText(Result<std::string> result) {
    if (!result) return "Error: " + result.error().message;
    return std::move(*result);
}

nlohmann::json serviceTextOrExists(Result<std::string> result) {
    if (!result && result.error().type == "OutputExists") return nlohmann::json("EXISTS: " + result.error().message);
    return serviceText(std::move(result));
}

nlohmann::json serviceJsonText(Result<std::string> result) {
    if (!result) {
        return nlohmann::json{{"error", result.error().message}}
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    return std::move(*result);
}

Result<assets::NifCollisionShape> importCollision(const nlohmann::json& args) {
    const auto* value = argument(args, 5);
    if (value && value->is_string()) {
        if (const auto shape = assets::parseCollisionShape(value->get<std::string>())) return *shape;
        return std::unexpected(Error{"InvalidCollision", "collision must be none, box, hull or mesh"});
    }
    if (value && value->is_boolean()) {
        return value->get<bool>() ? assets::NifCollisionShape::box : assets::NifCollisionShape::none;
    }
    return assets::NifCollisionShape::none;
}

Result<double> massArgument(const nlohmann::json& args, std::size_t index) {
    const auto* mass = argument(args, index);
    if (!mass || mass->is_null()) return 0.0;
    if (!mass->is_number()) return std::unexpected(Error{"InvalidMass", "mass_kg must be a number of kilograms"});
    return mass->get<double>();
}

Result<assets::NifCollisionSet> collisionSetArguments(const nlohmann::json& args) {
    assets::NifCollisionSet request;
    request.nifPath = stringArgument(args, 0);
    request.node = stringArgument(args, 1);
    request.outputPath = stringArgument(args, 2);
    const auto source = stringArgument(args, 3);
    if (source != "donor_nif" && source != "kind") {
        return std::unexpected(Error{"InvalidCollisionSource", "source must be donor_nif or kind"});
    }
    request.fromDonor = source == "donor_nif";
    request.donorPath = stringArgument(args, 4);
    if (!request.fromDonor) {
        const auto shape = assets::parseCollisionShape(stringArgument(args, 5));
        if (!shape || *shape == assets::NifCollisionShape::none) {
            return std::unexpected(Error{"InvalidCollisionKind", "kind must be box, hull or mesh"});
        }
        request.shape = *shape;
    }
    const auto mass = massArgument(args, 6);
    if (!mass) return std::unexpected(mass.error());
    request.massKg = *mass;
    if (const auto* body = argument(args, 7); body && !body->is_null()) {
        const auto index = core::jsonInt(*body);
        if (!index || *index < 0) {
            return std::unexpected(Error{"InvalidDonorBody", "donor_body must be a whole number, 0 or more"});
        }
        request.donorBody = static_cast<std::size_t>(*index);
    }
    request.overwrite = boolArgument(args, 8);
    request.expectedSource = stringArgument(args, 9);
    request.expectedDonorSource = stringArgument(args, 10);
    return request;
}

std::vector<fo4::nif::HavokLayerRow> collisionLayers(plugin::IPluginBackend& plugins) {
    std::vector<fo4::nif::HavokLayerRow> rows;
    const auto found = plugins.invokeBackend("GetCollisionLayers", nlohmann::json::array());
    if (!found || !found->is_array()) return rows;
    for (const auto& row : *found) {
        if (!row.is_object()) continue;
        const auto index = core::jsonInt(row.value("index", nlohmann::json{}));
        if (!index || *index < 0) continue;
        rows.push_back({static_cast<std::uint32_t>(*index), row.value("name", std::string{})});
    }
    return rows;
}

std::vector<std::uint32_t> shapeBlocks(const nlohmann::json* value, const char* label = "blocks",
                                       const char* noun = "block ids") {
    if (value == nullptr || value->is_null()) return {};
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    const auto complaint = std::string(label) + " must be a list of " + noun;
    if (!list.is_array()) throw std::invalid_argument(complaint);
    std::vector<std::uint32_t> blocks;
    for (const auto& id : list) {
        if (!id.is_number_unsigned() || id.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument(complaint);
        }
        blocks.push_back(id.get<std::uint32_t>());
    }
    return blocks;
}

std::vector<std::vector<std::uint32_t>> triangleGroups(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return {};
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    if (!list.is_array()) throw std::invalid_argument("groups must be a list of triangle index lists");
    std::vector<std::vector<std::uint32_t>> groups;
    for (const auto& group : list) groups.push_back(shapeBlocks(&group, "each groups entry", "triangle indices"));
    return groups;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> vertexPairs(const nlohmann::json* value, const char* label) {
    if (value == nullptr || value->is_null()) return {};
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    const auto complaint = std::string(label) + " must be a list of [vertex, vertex] pairs";
    if (!list.is_array()) throw std::invalid_argument(complaint);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (const auto& entry : list) {
        const auto pair = shapeBlocks(&entry, label, "[vertex, vertex] pairs");
        if (!entry.is_array() || pair.size() != 2) throw std::invalid_argument(complaint);
        pairs.emplace_back(pair[0], pair[1]);
    }
    return pairs;
}

std::optional<float> shapeNumber(const nlohmann::json* value, const char* label) {
    if (value == nullptr || value->is_null()) return std::nullopt;
    if (!value->is_number()) throw std::invalid_argument(std::string(label) + " must be a number");
    const auto number = value->get<float>();
    if (!std::isfinite(number)) throw std::invalid_argument(std::string(label) + " must be finite");
    return number;
}

std::optional<std::array<float, 3>> shapeNormal(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return std::nullopt;
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    if (!list.is_array() || list.size() != 3) throw std::invalid_argument("normal must be a list of three numbers");
    std::array<float, 3> normal{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto axis = shapeNumber(&list[i], "normal");
        if (!axis) throw std::invalid_argument("normal must be a list of three numbers");
        normal[i] = *axis;
    }
    return normal;
}

// Requires finite numbers: a key time or value may be negative, so the unsigned id parsers are not used.
std::vector<assets::NifKeyArg> keyArguments(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return {};
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    const std::string complaint = "keys must be a list of {\"time\": number, \"value\": [numbers]} entries";
    if (!list.is_array()) throw std::invalid_argument(complaint);
    std::vector<assets::NifKeyArg> keys;
    for (const auto& entry : list) {
        if (!entry.is_object() || !entry.contains("time") || !entry.contains("value") || !entry.at("value").is_array()) {
            throw std::invalid_argument(complaint);
        }
        const auto time = shapeNumber(&entry.at("time"), "key time");
        if (!time) throw std::invalid_argument(complaint);
        assets::NifKeyArg key;
        key.time = *time;
        for (const auto& number : entry.at("value")) {
            const auto parsed = shapeNumber(&number, "key value");
            if (!parsed) throw std::invalid_argument(complaint);
            key.value.push_back(*parsed);
        }
        keys.push_back(std::move(key));
    }
    return keys;
}

assets::NifModelOpArgs shapeModelArguments(const nlohmann::json& args) {
    assets::NifModelOpArgs model;
    model.tris = shapeBlocks(argument(args, 10), "tris", "triangle indices");
    model.distance = shapeNumber(argument(args, 11), "distance");
    model.loopA = shapeBlocks(argument(args, 12), "loop_a", "vertex indices");
    model.loopB = shapeBlocks(argument(args, 13), "loop_b", "vertex indices");
    model.normal = shapeNormal(argument(args, 14));
    model.offset = shapeNumber(argument(args, 15), "offset");
    model.edges = vertexPairs(argument(args, 16), "edges");
    model.width = shapeNumber(argument(args, 17), "width");
    return model;
}

std::uint32_t shapeInfluences(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return 4U;
    const auto count = core::jsonInt(*value);
    if (!count || *count < 1 || *count > 4) throw std::invalid_argument("max_influences must be between 1 and 4");
    return static_cast<std::uint32_t>(*count);
}

float shapePruneThreshold(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return 0.0f;
    if (!value->is_number()) throw std::invalid_argument("prune_threshold must be a number");
    const auto threshold = value->get<float>();
    if (!(threshold >= 0.0f && threshold < 1.0f)) {
        throw std::invalid_argument("prune_threshold must be at least 0 and below 1");
    }
    return threshold;
}

std::optional<std::uint32_t> shapeSourceBlock(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return std::nullopt;
    const auto block = core::jsonInt(*value);
    if (!block || *block < 0) throw std::invalid_argument("source_block must be a block id");
    return static_cast<std::uint32_t>(*block);
}

// Requires an unsigned whole number: a float or a negative number is refused, never converted.
std::optional<std::uint32_t> optionalWhole(const nlohmann::json* value, const char* label, const char* noun) {
    if (value == nullptr || value->is_null()) return std::nullopt;
    if (!value->is_number_integer() || (!value->is_number_unsigned() && value->get<std::int64_t>() < 0) ||
        value->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(label) + " must be " + noun);
    }
    return value->get<std::uint32_t>();
}

std::int64_t requiredIndex(const nlohmann::json* value, const char* label) {
    const auto index = value == nullptr || value->is_null() ? std::nullopt : core::jsonInt(*value);
    if (!index) throw std::invalid_argument(std::string(label) + " must be an integer");
    return *index;
}

std::vector<fo4::nif::Influence> skinInfluences(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return {};
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    if (!list.is_array()) throw std::invalid_argument("influences must be a list of {bone, weight} objects");
    std::vector<fo4::nif::Influence> influences;
    for (const auto& entry : list) {
        if (!entry.is_object() || !entry.contains("bone") || !entry.contains("weight")) {
            throw std::invalid_argument("influences must be a list of {bone, weight} objects");
        }
        const auto bone = requiredIndex(&entry.at("bone"), "influence bone");
        if (!entry.at("weight").is_number()) throw std::invalid_argument("influence weight must be a number");
        influences.push_back(fo4::nif::Influence{static_cast<std::int32_t>(bone),
                                                entry.at("weight").get<float>()});
    }
    return influences;
}

fo4::nif::SkinEditRequest skinEditArguments(const nlohmann::json& args) {
    const auto kind = fo4::nif::parse_skin_edit_kind(stringArgument(args, 2));
    if (!kind) {
        throw std::invalid_argument("kind must be set_influences, copy_bone or mirror_weights");
    }
    const auto block = requiredIndex(argument(args, 3), "block");
    if (block < 0) throw std::invalid_argument("block must be a block id");
    fo4::nif::SkinEditRequest request;
    request.kind = *kind;
    request.block = static_cast<std::uint32_t>(block);
    const auto* vertex = argument(args, 4);
    if (vertex != nullptr && !vertex->is_null()) {
        const auto index = requiredIndex(vertex, "vertex");
        if (index < 0) throw std::invalid_argument("vertex must be a vertex index");
        request.vertex = static_cast<std::uint32_t>(index);
    }
    request.influences = skinInfluences(argument(args, 5));
    const auto* fromBone = argument(args, 6);
    if (fromBone != nullptr && !fromBone->is_null()) {
        request.from_bone = static_cast<std::int32_t>(requiredIndex(fromBone, "from_bone"));
    }
    const auto* toBone = argument(args, 7);
    if (toBone != nullptr && !toBone->is_null()) {
        request.to_bone = static_cast<std::int32_t>(requiredIndex(toBone, "to_bone"));
    }
    request.keep_source = boolArgument(args, 8, false);
    const auto* lane = argument(args, 9);
    if (lane != nullptr && !lane->is_null()) {
        const auto index = requiredIndex(lane, "lane");
        if (index < 0 || index > 2) throw std::invalid_argument("lane must be 0, 1 or 2");
        request.lane = static_cast<std::uint8_t>(index);
    }
    request.from_positive_side = boolArgument(args, 10, true);
    return request;
}

}

SettingsTarget::SettingsTarget(settings::Store& store,
                               settings::SharedSettings& current,
                               platform::Services& platform,
                               process::Runner& runner)
    : store_(store), shared_(current), platform_(platform), runner_(runner) {}

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
    return redactedSettings(shared_.snapshot()).dump();
}

Result<nlohmann::json> SettingsTarget::saveSettings(const nlohmann::json& args) {
    const auto* raw = argument(args, 0);
    if (!raw || !raw->is_string()) return nlohmann::json("Invalid settings payload.");

    try {
        const auto payload = nlohmann::json::parse(raw->get<std::string>());
        if (!payload.is_object()) return nlohmann::json("Invalid settings payload.");
        if (auto error = toolPathError(payload); !error.empty()) return nlohmann::json(error);
        if (auto error = fullPathFolderError(payload, "NewPluginFolder"); !error.empty()) return nlohmann::json(error);
        if (auto error = fullPathFolderError(payload, "PrevisOutputFolder"); !error.empty()) return nlohmann::json(error);

        return shared_.update([&](settings::Settings& current) {
            updateString(payload, "AiProvider", current.aiProvider);
            updateSecret(payload, "AnthropicApiKey", current.anthropicApiKey);
            updateString(payload, "Model", current.model);
            updateSecret(payload, "GeminiApiKey", current.geminiApiKey);
            updateString(payload, "GeminiModel", current.geminiModel);
            updatePath(payload, "ClaudeCodePath", current.claudeCodePath);
            updateString(payload, "OllamaUrl", current.ollamaUrl);
            updateString(payload, "OllamaModel", current.ollamaModel);
            updatePath(payload, "OutputFolder", current.outputFolder);
            updatePath(payload, "Fallout4Path", current.fallout4Path);
            updatePath(payload, "DataFolder", current.dataFolder);
            updatePath(payload, "Mo2InstancePath", current.mo2InstancePath);
            updatePath(payload, "NewPluginFolder", current.newPluginFolder);
            updatePath(payload, "PrevisOutputFolder", current.previsOutputFolder);
            updatePath(payload, "CkWikiPath", current.ckWikiPath);
            updatePath(payload, "PapyrusCompilerPath", current.papyrusCompilerPath);
            updatePath(payload, "PapyrusBaseImports", current.papyrusBaseImports);
            updatePath(payload, "FfmpegPath", current.ffmpegPath);
            updatePath(payload, "XwmaEncodePath", current.xwmaEncodePath);
            updatePath(payload, "Archive2Path", current.archive2Path);
            updateBool(payload, "ReadLargePluginsIntoMemory", current.readLargePluginsIntoMemory);

            auto saved = store_.save(current);
            if (!saved) return nlohmann::json("Save failed: " + saved.error().message);
            return nlohmann::json("Settings saved.");
        });
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
    const auto executable = tools::resolveCommand(path);
    const auto text = executable.string();
    if (const auto extension = toLower(executable.extension().string()); extension == ".cmd" || extension == ".bat") {
        if (text.find_first_of("\"&|<>^%") != std::string::npos) {
            return nlohmann::json("Refused: a .cmd or .bat path cannot contain \" & | < > ^ % because cmd.exe would interpret them. Point ClaudeCodePath at claude.exe instead.");
        }
        request.executable = "cmd.exe";
        request.arguments = {"/d", "/s", "/c", "\"" + text + "\" --version"};
    } else {
        request.executable = executable;
        request.arguments = {"--version"};
    }
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
                     platform::Services& platform,
                     plugin::IPluginBackend& plugins)
    : service_(service), textures_(textures), platform_(platform), plugins_(plugins) {}

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
    if (method == "Geo") return serviceJsonText(service_.geometry(stringArgument(args, 0)));
    if (method == "GeoSkinned") return serviceJsonText(service_.geometry(stringArgument(args, 0), true));
    if (method == "Verify") return serviceText(service_.verify(stringArgument(args, 0)));
    if (method == "Tree") return serviceJsonText(service_.tree(stringArgument(args, 0)));
    if (method == "Segments") {
        const auto block = intNumberArgument(args, 1, -1);
        if (block < 0) return std::unexpected(Error{"BlockIndexMissing", "block index required"});
        return serviceJsonText(service_.segments(stringArgument(args, 0), static_cast<std::uint32_t>(block)));
    }
    if (method == "Fix") {
        if (const auto* check = argument(args, 2); check && !check->is_null() && (!check->is_string() || check->get<std::string>().empty()))
            return serviceText(std::unexpected(Error{"InvalidArgument", "Invalid NIF repair check"}));
        auto fixed = service_.fix(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                  boolArgument(args, 3));
        return serviceTextOrExists(std::move(fixed));
    }
    if (method == "ExportGltf") {
        const auto textureMode = assets::parseGltfTextureMode(stringArgument(args, 4));
        if (!textureMode) return nlohmann::json("Error: textures must be copy, png, embed or uri");
        assets::NifGltfOptions options;
        options.materials = boolArgument(args, 3, true);
        options.textures = *textureMode;
        options.tangents = boolArgument(args, 5, true);
        options.vertexColours = boolArgument(args, 6, true);
        const auto resolver = textures_.assetResolver();
        options.assets = resolver.get();
        auto exported = assets::exportNifGltf(stringArgument(args, 0), stringArgument(args, 1), boolArgument(args, 2), options);
        return serviceTextOrExists(std::move(exported));
    }
    if (method == "ShapeOp") {
        try {
            const auto* crease = argument(args, 4);
            const auto creaseDegrees = crease && crease->is_number() ? crease->get<float>() : 180.0f;
            const auto* ratio = argument(args, 6);
            const auto ratioValue = ratio && ratio->is_number() ? ratio->get<float>() : 0.5f;
            const auto maxInfluences = shapeInfluences(argument(args, 7));
            const auto pruneThreshold = shapePruneThreshold(argument(args, 8));
            const auto sourceBlock = shapeSourceBlock(argument(args, 9));
            auto result = service_.shapeOp(stringArgument(args, 0), stringArgument(args, 1),
                                           stringArgument(args, 2), shapeBlocks(argument(args, 3)),
                                           creaseDegrees, boolArgument(args, 5), ratioValue, maxInfluences,
                                           pruneThreshold, sourceBlock, shapeModelArguments(args));
            return serviceTextOrExists(std::move(result));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "SkinInfluences") {
        try {
            const auto block = requiredIndex(argument(args, 1), "block");
            const auto vertex = requiredIndex(argument(args, 2), "vertex");
            if (block < 0) throw std::invalid_argument("block must be a block id");
            if (vertex < 0) throw std::invalid_argument("vertex must be a vertex index");
            return serviceJsonText(service_.influences(stringArgument(args, 0), static_cast<std::uint32_t>(block),
                                                        static_cast<std::uint32_t>(vertex)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "SkinInfluencedBy") {
        try {
            const auto block = requiredIndex(argument(args, 1), "block");
            const auto bone = requiredIndex(argument(args, 2), "bone");
            if (block < 0) throw std::invalid_argument("block must be a block id");
            if (bone < 0 || bone > std::numeric_limits<std::int32_t>::max()) {
                throw std::invalid_argument("bone must be a bone index");
            }
            const auto* minimum = argument(args, 3);
            float threshold = 0.01f;
            if (minimum != nullptr && !minimum->is_null()) {
                if (!minimum->is_number()) throw std::invalid_argument("minimum must be a number");
                threshold = minimum->get<float>();
            }
            return serviceJsonText(service_.influencedBy(stringArgument(args, 0),
                                                         static_cast<std::uint32_t>(block),
                                                         static_cast<std::int32_t>(bone), threshold));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "SkinValidate") {
        try {
            const auto block = requiredIndex(argument(args, 1), "block");
            if (block < 0) throw std::invalid_argument("block must be a block id");
            return serviceJsonText(service_.validateSkin(stringArgument(args, 0), static_cast<std::uint32_t>(block)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "SkinBinding") {
        try {
            std::optional<std::uint32_t> block;
            if (const auto* value = argument(args, 1); value != nullptr && !value->is_null()) {
                const auto index = requiredIndex(value, "block");
                if (index < 0 || index > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::invalid_argument("block must be a block id");
                }
                block = static_cast<std::uint32_t>(index);
            }
            return serviceJsonText(assets::readSkinBinding(stringArgument(args, 0), block, stringArgument(args, 2),
                                                           stringArgument(args, 3)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "SkinEdit") {
        try {
            const auto request = skinEditArguments(args);
            auto edited = assets::applySkinEdit(stringArgument(args, 0), request, stringArgument(args, 1),
                                                stringArgument(args, 12), boolArgument(args, 11));
            return serviceTextOrExists(std::move(edited));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "SkinToolOp") {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(stringArgument(args, 3));
        } catch (const std::exception& ex) {
            return serviceJsonText(std::unexpected(Error{"SkinToolFailed", std::string("the skin tool arguments are not JSON: ") + ex.what()}));
        }
        return serviceJsonText(assets::applySkinTool(stringArgument(args, 0), stringArgument(args, 2), body,
                                                     stringArgument(args, 1), stringArgument(args, 5),
                                                     boolArgument(args, 4)));
    }
    if (method == "Import") {
        const auto collision = importCollision(args);
        if (!collision) return nlohmann::json("Error: " + collision.error().message);
        assets::NifImportOptions options;
        options.material = stringArgument(args, 2);
        options.diffuseTexture = stringArgument(args, 3);
        options.normalTexture = stringArgument(args, 4);
        options.collision = *collision;
        options.fromBlender = boolArgument(args, 6);
        const auto mass = massArgument(args, 7);
        if (!mass) return nlohmann::json("Error: " + mass.error().message);
        options.massKg = *mass;
        auto imported = service_.importObj(stringArgument(args, 0), stringArgument(args, 1), options, boolArgument(args, 8));
        return serviceTextOrExists(std::move(imported));
    }
    if (method == "CollisionInfo") {
        const auto layers = collisionLayers(plugins_);
        return serviceJsonText(collision_.info(stringArgument(args, 0), layers));
    }
    if (method == "CollisionGeo") return serviceJsonText(collision_.geometry(stringArgument(args, 0)));
    if (method == "CollisionSet") {
        const auto request = collisionSetArguments(args);
        if (!request) return nlohmann::json("Error: " + request.error().message);
        auto result = collision_.set(*request);
        return serviceTextOrExists(std::move(result));
    }
    if (method == "CollisionRemove") {
        auto result = collision_.remove(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                        boolArgument(args, 3), stringArgument(args, 4));
        return serviceTextOrExists(std::move(result));
    }
    if (method == "ImportGltf") {
        assets::NifImportOptions options;
        options.material = stringArgument(args, 2);
        options.diffuseTexture = stringArgument(args, 3);
        options.normalTexture = stringArgument(args, 4);
        const auto collision = importCollision(args);
        if (!collision) return nlohmann::json("Error: " + collision.error().message);
        if (*collision != assets::NifCollisionShape::none && *collision != assets::NifCollisionShape::box) {
            return nlohmann::json("Error: glTF import supports collision 'none' or 'box' only");
        }
        options.collision = *collision;
        options.skeleton = stringArgument(args, 6);
        auto imported = service_.importGltf(stringArgument(args, 0), stringArgument(args, 1), options, boolArgument(args, 7));
        return serviceTextOrExists(std::move(imported));
    }
    if (method == "ApplyEdits") {
        try {
            const auto editsText = stringArgument(args, 1);
            const auto edits = nlohmann::json::parse(editsText);
            return serviceText(service_.applyEdits(stringArgument(args, 0), edits, stringArgument(args, 2),
                                                   stringArgument(args, 3)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "ConnectPoints") return serviceJsonText(service_.connectPoints(stringArgument(args, 0)));
    if (method == "ApplyConnectPoints") {
        return serviceTextOrExists(service_.applyConnectPoints(stringArgument(args, 0), stringArgument(args, 1),
                                                               stringArgument(args, 2), boolArgument(args, 3)));
    }
    if (method == "UvGet") return serviceJsonText(assets::renderUvSummary(stringArgument(args, 0)));
    if (method == "UvTransform") {
        const int block = intNumberArgument(args, 1, -1);
        if (block < 0) return nlohmann::json("Error: block must be a positive shape block id");
        const auto* channelArgument = argument(args, 2);
        const int channel = channelArgument == nullptr || channelArgument->is_null() ? 0 : intNumberArgument(args, 2, -1);
        if (channel != 0 && channel != 2) return nlohmann::json("Error: channel must be 0 or 2");
        const auto number = [&args](std::size_t index, float fallback) {
            const auto* value = argument(args, index);
            if (value == nullptr || !value->is_number()) return fallback;
            const double parsed = value->get<double>();
            return std::isfinite(parsed) ? static_cast<float>(parsed) : fallback;
        };
        assets::UvTransform transform;
        transform.du = number(3, 0.0f);
        transform.dv = number(4, 0.0f);
        transform.sx = number(5, 1.0f);
        transform.sy = number(6, 1.0f);
        transform.rotation_degrees = number(7, 0.0f);
        return serviceTextOrExists(assets::applyUvTransform(stringArgument(args, 0), static_cast<std::uint32_t>(block),
                                                            channel, transform, stringArgument(args, 8),
                                                            boolArgument(args, 9)));
    }
    if (method == "UvOp") {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(stringArgument(args, 1));
        } catch (const std::exception& ex) {
            return serviceJsonText(std::unexpected(Error{"UvOpFailed", std::string("the UV arguments are not JSON: ") + ex.what()}));
        }
        return serviceJsonText(nifUvOp(stringArgument(args, 0), body));
    }
    if (method == "VertexColorGet") return serviceJsonText(assets::renderVertexColors(stringArgument(args, 0)));
    if (method == "VertexColorSet") {
        const auto op = assets::parseVertexColorOp(args);
        if (!op) return nlohmann::json("Error: " + op.error().message);
        return serviceTextOrExists(assets::applyVertexColor(stringArgument(args, 0), *op, stringArgument(args, 9),
                                                            stringArgument(args, 10), boolArgument(args, 11)));
    }
    if (method == "CreateTextureSet") {
        try {
            const auto* block = argument(args, 1);
            const auto* textures = argument(args, 2);
            if (block == nullptr || !block->is_number_integer() ||
                (!block->is_number_unsigned() && block->get<std::int64_t>() < 0) ||
                block->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
                return nlohmann::json("Error: CreateTextureSet needs a whole block index");
            }
            if (textures == nullptr || !textures->is_array()) {
                return nlohmann::json("Error: CreateTextureSet needs a list of texture paths");
            }
            return serviceTextOrExists(service_.createTextureSet(stringArgument(args, 0), block->get<std::uint32_t>(),
                                                                 textures->get<std::vector<std::string>>(),
                                                                 stringArgument(args, 3), stringArgument(args, 4),
                                                                 boolArgument(args, 5)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "AttachAlpha") {
        try {
            const auto* block = argument(args, 1);
            const auto* flags = argument(args, 2);
            const auto* threshold = argument(args, 3);
            if (block == nullptr || !block->is_number_integer() ||
                (!block->is_number_unsigned() && block->get<std::int64_t>() < 0) ||
                block->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
                return nlohmann::json("Error: AttachAlpha needs a whole block index");
            }
            std::uint16_t parsed_flags = 236;
            if (flags != nullptr && !flags->is_null()) {
                if (!flags->is_number_integer() ||
                    (!flags->is_number_unsigned() && flags->get<std::int64_t>() < 0) ||
                    flags->get<std::uint64_t>() > std::numeric_limits<std::uint16_t>::max()) {
                    return nlohmann::json("Error: AttachAlpha needs flags from 0 to 65535");
                }
                parsed_flags = static_cast<std::uint16_t>(flags->get<std::uint64_t>());
            }
            std::uint8_t parsed_threshold = 128;
            if (threshold != nullptr && !threshold->is_null()) {
                if (!threshold->is_number_integer() ||
                    (!threshold->is_number_unsigned() && threshold->get<std::int64_t>() < 0) ||
                    threshold->get<std::uint64_t>() > std::numeric_limits<std::uint8_t>::max()) {
                    return nlohmann::json("Error: AttachAlpha needs a threshold from 0 to 255");
                }
                parsed_threshold = static_cast<std::uint8_t>(threshold->get<std::uint64_t>());
            }
            return serviceTextOrExists(service_.attachAlpha(
                stringArgument(args, 0), block->get<std::uint32_t>(), parsed_flags, parsed_threshold,
                stringArgument(args, 4), stringArgument(args, 5), boolArgument(args, 6)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "RemoveBlock") {
        try {
            const auto* block = argument(args, 1);
            if (block == nullptr || !block->is_number_integer() ||
                (!block->is_number_unsigned() && block->get<std::int64_t>() < 0) ||
                block->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
                return nlohmann::json("Error: RemoveBlock needs a whole block index");
            }
            return serviceTextOrExists(service_.removeBlock(stringArgument(args, 0), block->get<std::uint32_t>(),
                                                            stringArgument(args, 2), stringArgument(args, 3),
                                                            boolArgument(args, 4)));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "BlockOp") {
        try {
            const auto optionalBlock = [&](std::size_t index, const char* label) {
                return optionalWhole(argument(args, index), label, "a whole block index");
            };
            return serviceTextOrExists(service_.blockOp(
                stringArgument(args, 0), stringArgument(args, 1), optionalBlock(2, "block"), optionalBlock(3, "child"),
                stringArgument(args, 4), shapeBlocks(argument(args, 5)), stringArgument(args, 6),
                stringArgument(args, 7), boolArgument(args, 8), triangleGroups(argument(args, 9)),
                stringArgument(args, 10), stringArgument(args, 11), stringArgument(args, 12),
                optionalWhole(argument(args, 13), "interpolation", "a whole number"), keyArguments(argument(args, 14))));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    if (method == "PoseList") {
        try {
            return serviceText(service_.listPoses(stringArgument(args, 0), stringArgument(args, 1),
                                                  optionalWhole(argument(args, 2), "block", "a whole block index")));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
    }
    // Constraint: returns a PNG data URL for a NIF's texture (loose or BA2), decoded in-process without texconv.
    if (method == "GetTexture") {
        const auto* reconstruct = argument(args, 3);
        return nlohmann::json(textures_.getTexturePngDataUrl(
            stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
            reconstruct != nullptr && reconstruct->is_boolean() && reconstruct->get<bool>()));
    }
    if (method == "OpenFolder") {
        auto opened = platform_.openFolder(stringArgument(args, 0));
        return nlohmann::json(opened ? std::string{} : "Error: " + opened.error().message);
    }
    if (method == "MaterialShapes") {
        return serviceJsonText(assets::NifMaterialLink(textures_).shapes(stringArgument(args, 0), stringArgument(args, 1)));
    }
    if (method == "MaterialCompare" || method == "MaterialTextureEdits") {
        const auto* shape = argument(args, 1);
        const int block = shape ? core::jsonIntSaturated(*shape).value_or(-1) : -1;
        const assets::NifMaterialLink link(textures_);
        return serviceJsonText(method == "MaterialCompare"
                                   ? link.compare(stringArgument(args, 0), block, stringArgument(args, 2))
                                   : link.textureEdits(stringArgument(args, 0), block, stringArgument(args, 2)));
    }
    if (method == "StageDroppedFile") return stageDroppedFile(args);
    if (method == "ResolveAsset") return resolveAsset(args);
    if (method == "AuditAssetUsage") return auditAssetUsage(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown NIF RPC method: " + std::string(method)});
}

Result<nlohmann::json> NifTarget::resolveAsset(const nlohmann::json& args) {
    const auto requested = stringArgument(args, 0);
    if (requested.empty()) return std::unexpected(Error{"AssetPathMissing", "Give a game-relative asset path, for example Meshes/Clutter/Rock01.nif"});
    const auto limit = static_cast<std::size_t>(std::max(1, intNumberArgument(args, 2, 25)));
    try {
        const auto resolver = textures_.assetResolver();
        if (resolver->roots().empty() && resolver->archives().empty()) {
            return std::unexpected(Error{"NoDataRoots", "No data roots are known; load a modlist or set a Data folder first."});
        }
        const auto key = fo4::archive::normalize_asset_path(requested);
        const auto owner = [&](const std::filesystem::path& file) {
            for (const auto& root : resolver->roots()) {
                const auto relative = file.lexically_relative(root);
                if (!relative.empty() && *relative.begin() != "..") return root.filename().string();
            }
            return std::string{};
        };

        nlohmann::json providers = nlohmann::json::array();
        std::optional<std::filesystem::path> winnerFile;
        std::optional<std::pair<std::size_t, std::size_t>> winnerEntry;
        for (const auto& root : resolver->roots()) {
            const auto single = fo4::archive::AssetResolver::open_roots(std::span<const std::filesystem::path>(&root, 1), {});
            const auto found = single.locate(key);
            if (!found) continue;
            std::error_code ec;
            const auto size = std::filesystem::file_size(found->loose_path, ec);
            providers.push_back({{"Kind", "loose"}, {"Provider", root.filename().string()},
                                 {"Container", found->loose_path.string()}, {"Size", ec ? 0 : size}});
            if (!winnerFile && !winnerEntry) winnerFile = found->loose_path;
        }
        const auto& archives = resolver->archives();
        for (std::size_t index = archives.size(); index-- > 0;) {
            const auto entry = archives[index].find(key);
            if (!entry) continue;
            providers.push_back({{"Kind", "ba2"}, {"Provider", owner(archives[index].path())},
                                 {"Container", archives[index].path().string()},
                                 {"Inner", archives[index].entries()[*entry].path},
                                 {"Size", archives[index].entries()[*entry].unpacked_size()}});
            if (!winnerFile && !winnerEntry) winnerEntry = std::pair{index, *entry};
        }

        nlohmann::json row{{"Path", key}, {"Found", !providers.empty()}, {"Providers", providers.size()},
                           {"Ambiguous", providers.size() > 1}};
        if (providers.empty()) return nlohmann::json(row.dump());
        row["Winner"] = providers.front();
        nlohmann::json rest = nlohmann::json::array();
        for (std::size_t index = 1; index < providers.size() && index < limit; ++index) rest.push_back(providers[index]);
        row["Others"] = std::move(rest);

        if (boolArgument(args, 1)) {
            if (winnerFile) {
                row["ExtractedTo"] = winnerFile->string();
            } else {
                const auto bytes = archives[winnerEntry->first].extract(winnerEntry->second);
                const auto tag = std::to_string(std::hash<std::string>{}(archives[winnerEntry->first].path().string() + "|" + key));
                const auto extension = std::filesystem::path(key).extension().string();
                const auto out = textures_.cacheDir() / ("asset_" + tag + extension);
                std::filesystem::create_directories(out.parent_path());
                auto temp = out;
                temp += ".tmp";
                {
                    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
                    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    if (!stream) return std::unexpected(Error{"AssetExtractFailed", "Could not write " + out.string()});
                }
                std::filesystem::rename(temp, out);
                row["ExtractedTo"] = out.string();
            }
        }
        return nlohmann::json(row.dump());
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"AssetResolveFailed", ex.what()});
    }
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

std::string geometryBatchProgressJson(int done, int total, assets::ArchiveIndexProgress index) {
    const bool indexing = index.phase == assets::ArchiveIndexPhase::indexing;
    return nlohmann::json{
        {"done", done},
        {"total", total},
        {"phase", indexing ? "indexing" : "converting"},
        {"indexDone", index.done},
        {"indexTotal", index.total}}.dump();
}

namespace {

Result<float> timeArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argument(args, index);
    if (value == nullptr || !value->is_number()) {
        return std::unexpected(Error{"InvalidTime", "time must be a number of seconds"});
    }
    const auto seconds = value->get<double>();
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return std::unexpected(Error{"InvalidTime", "time must be a finite number of seconds, zero or more"});
    }
    return static_cast<float>(seconds);
}

Result<hkx::PoseSpace> poseSpaceArgument(const nlohmann::json& args, std::size_t index) {
    const auto space = stringArgument(args, index);
    if (space.empty() || space == "local") return hkx::PoseSpace::Local;
    if (space == "model") return hkx::PoseSpace::Model;
    return std::unexpected(Error{"InvalidSpace", "space must be \"local\" or \"model\""});
}


Result<std::size_t> mapperIndexArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argument(args, index);
    if (value == nullptr || value->is_null()) return std::size_t{0};
    const auto number = core::jsonInt(*value);
    if (!number || *number < 0) return std::unexpected(Error{"InvalidMapper", "mapper must be an integer from zero to " + std::to_string(std::numeric_limits<int>::max())});
    return static_cast<std::size_t>(*number);
}

}

HkxTarget::HkxTarget(hkx::HkxService& service) : service_(service) {}

Result<nlohmann::json> HkxTarget::invoke(std::string_view method, const nlohmann::json& args) {
    if (method == "Skeleton") return service_.skeleton(stringArgument(args, 0));
    if (method == "LoadClip") return service_.loadClip(stringArgument(args, 0), stringArgument(args, 1));
    if (method == "Summary") return service_.summary(stringArgument(args, 0));
    if (method == "InspectAnimation") {
        std::optional<std::size_t> track;
        if (const auto* value = argument(args, 1); value != nullptr && !value->is_null()) {
            if (!value->is_number_integer()) {
                return std::unexpected(Error{"InvalidTrack", "track must be an integer index"});
            }
            const auto index = value->get<long long>();
            if (index < 0) return std::unexpected(Error{"InvalidTrack", "track must be zero or more"});
            track = static_cast<std::size_t>(index);
        }
        const auto limit = std::clamp(intNumberArgument(args, 2, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.inspectAnimation(stringArgument(args, 0), track, static_cast<std::size_t>(limit));
    }
    if (method == "Pose") {
        const auto seconds = timeArgument(args, 2);
        if (!seconds) return std::unexpected(seconds.error());
        const auto space = poseSpaceArgument(args, 3);
        if (!space) return std::unexpected(space.error());
        return service_.pose(stringArgument(args, 0), stringArgument(args, 1), *seconds, *space);
    }
    if (method == "PoseBoth") {
        const auto seconds = timeArgument(args, 2);
        if (!seconds) return std::unexpected(seconds.error());
        return service_.poseBoth(stringArgument(args, 0), stringArgument(args, 1), *seconds);
    }
    if (method == "Subgraph") {
        return service_.subgraph(stringArgument(args, 0), stringArgument(args, 1),
                                 stringArgument(args, 2));
    }
    if (method == "SubgraphManifest") {
        std::vector<std::string> prefixes;
        if (const auto* list = argument(args, 2); list != nullptr && list->is_array()) {
            for (const auto& entry : *list) {
                if (entry.is_string()) prefixes.push_back(entry.get<std::string>());
            }
        }
        return service_.subgraphManifest(stringArgument(args, 0), stringArgument(args, 1), prefixes,
                                         stringArgument(args, 3), stringArgument(args, 4),
                                         stringArgument(args, 5));
    }
    if (method == "ProjectChain") {
        return service_.projectChain(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                     stringArgument(args, 3));
    }
    if (method == "ProjectCheck") {
        const auto limit = std::clamp(intNumberArgument(args, 4, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.projectCheck(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                     stringArgument(args, 3), static_cast<std::size_t>(limit), stringArgument(args, 5));
    }
    if (method == "CharacterCheck") {
        return service_.characterCheck({stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                        stringArgument(args, 3), stringArgument(args, 4), stringArgument(args, 5),
                                        stringArgument(args, 6), stringArgument(args, 7), stringArgument(args, 8)});
    }
    if (method == "CharacterSave") {
        const auto stage = [&](std::size_t at) {
            const auto* given = argument(args, at + 2);
            return hkx::CharacterSaveStageArgs{stringArgument(args, at), stringArgument(args, at + 1),
                                               given != nullptr ? *given : nlohmann::json(), stringArgument(args, at + 3)};
        };
        const auto mapperIndex = mapperIndexArgument(args, 12);
        if (!mapperIndex) return std::unexpected(mapperIndex.error());
        hkx::CharacterSaveArgs request;
        request.skeleton = stage(0);
        request.ragdoll = stage(4);
        request.mapper = stage(8);
        request.skin = stage(13);
        request.mapperIndex = *mapperIndex;
        request.overwrite = boolArgument(args, 17);
        return service_.characterSave(request);
    }
    if (method == "CharacterSavePlan") {
        const auto* given = argument(args, 8);
        nlohmann::json edits = given != nullptr ? *given : nlohmann::json();
        if (edits.is_string()) {
            const auto text = edits.get<std::string>();
            edits = text.empty() ? nlohmann::json() : nlohmann::json::parse(text, nullptr, false);
        }
        if (edits.is_discarded()) return std::unexpected(Error{"InvalidEdits", "edits must be a JSON array"});
        return service_.characterSavePlan({stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                           stringArgument(args, 3), stringArgument(args, 4), stringArgument(args, 5),
                                           stringArgument(args, 6), stringArgument(args, 7), std::move(edits),
                                           boolArgument(args, 9)});
    }
    if (method == "InspectObjects") {
        const auto limit = std::clamp(intNumberArgument(args, 3, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.inspectObjects(stringArgument(args, 0), stringArgument(args, 1),
                                       boolArgument(args, 2), static_cast<std::size_t>(limit));
    }
    if (method == "RootMotion") {
        const auto bone = intNumberArgument(args, 2, -1);
        if (bone < 0) return std::unexpected(Error{"InvalidBone", "bone must be zero or more"});
        const auto limit = std::clamp(intNumberArgument(args, 3, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.rootMotion(stringArgument(args, 0), stringArgument(args, 1), bone,
                                   static_cast<std::size_t>(limit));
    }
    if (method == "InspectBehavior") {
        const auto limit = std::clamp(intNumberArgument(args, 1, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.inspectBehavior(stringArgument(args, 0), static_cast<std::size_t>(limit));
    }
    if (method == "ReadBehaviorModel") {
        const auto limit = std::clamp(intNumberArgument(args, 1, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.behaviorModel(stringArgument(args, 0), static_cast<std::size_t>(limit));
    }
    if (method == "ReadBehaviorAuthoring") return service_.readBehaviorAuthoring(stringArgument(args, 0));
    if (method == "ValidateBehaviorGraph") return service_.validateBehaviorGraph(stringArgument(args, 0));
    if (method == "EditBehaviorGraph") {
        const auto* edits = argument(args, 1);
        if (edits == nullptr) return std::unexpected(Error{"InvalidEdits", "edits must be a non-empty array"});
        const auto list = edits->is_string() ? nlohmann::json::parse(edits->get<std::string>(), nullptr, false) : *edits;
        if (list.is_discarded()) return std::unexpected(Error{"InvalidEdits", "edits is not valid JSON"});
        return service_.editBehaviorGraph(stringArgument(args, 0), list, stringArgument(args, 2),
                                          boolArgument(args, 3));
    }
    if (method == "ReadRagdoll") {
        const auto limit = std::clamp(intNumberArgument(args, 1, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.ragdoll(stringArgument(args, 0), static_cast<std::size_t>(limit));
    }
    if (method == "SaveRagdoll") {
        const auto* edits = argument(args, 2);
        if (edits == nullptr) return std::unexpected(Error{"InvalidEdits", "edits is required"});
        return service_.saveRagdoll(stringArgument(args, 0), stringArgument(args, 1), *edits, boolArgument(args, 3),
                                    stringArgument(args, 4));
    }
    if (method == "PreviewRagdoll") {
        const auto* options = argument(args, 1);
        return service_.previewRagdoll(stringArgument(args, 0), options != nullptr ? *options : nlohmann::json::object());
    }
    if (method == "ReadMappers") {
        const auto limit = std::clamp(intNumberArgument(args, 2, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.mappers(stringArgument(args, 0), boolArgument(args, 1), static_cast<std::size_t>(limit));
    }
    if (method == "EditMapper") {
        const auto mapper = mapperIndexArgument(args, 1);
        if (!mapper) return std::unexpected(mapper.error());
        const auto* given = argument(args, 2);
        auto edits = given ? *given : nlohmann::json::array();
        if (edits.is_string()) edits = nlohmann::json::parse(edits.get<std::string>(), nullptr, false);
        if (edits.is_discarded()) return std::unexpected(Error{"InvalidMapperEdit", "edits is not valid JSON"});
        const auto limit = std::clamp(intNumberArgument(args, 6, static_cast<int>(core::kDefaultPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.editMapper(stringArgument(args, 0), *mapper,
                                   edits, stringArgument(args, 3),
                                   boolArgument(args, 4), stringArgument(args, 5), static_cast<std::size_t>(limit));
    }
    if (method == "ProjectIndex") {
        const auto limit = std::clamp(intNumberArgument(args, 2, static_cast<int>(core::kMaxPageLimit)),
                                      1, static_cast<int>(core::kMaxPageLimit));
        return service_.projectIndex(stringArgument(args, 0), stringArgument(args, 1),
                                     static_cast<std::size_t>(limit));
    }
    if (method == "ReadScene") return service_.readScene(stringArgument(args, 0));
    if (method == "AnimationIndex") {
        hkx::AnimationIndexQuery request;
        request.dataFolder = fo4::io::from_utf8(stringArgument(args, 0));
        request.mo2Folder = fo4::io::from_utf8(stringArgument(args, 1));
        request.profile = stringArgument(args, 2);
        request.kind = stringArgument(args, 3);
        request.project = stringArgument(args, 4);
        request.race = stringArgument(args, 5);
        request.character = stringArgument(args, 6);
        request.query = stringArgument(args, 7);
        request.resolvePath = stringArgument(args, 8);
        request.offset = static_cast<std::size_t>(std::max(intNumberArgument(args, 9, 0), 0));
        request.limit = static_cast<std::size_t>(std::clamp(intNumberArgument(args, 10, 200), 1,
                                                            static_cast<int>(core::kMaxPageLimit)));
        request.refresh = boolArgument(args, 11);
        return service_.animationIndex(request);
    }
    if (method == "InspectObject") {
        const auto object = intNumberArgument(args, 1, -1);
        if (object < 0) return std::unexpected(Error{"InvalidObject", "object must be zero or more"});
        return service_.inspectObject(stringArgument(args, 0), static_cast<std::size_t>(object),
                                      static_cast<std::size_t>(std::max(0, intNumberArgument(args, 2, 0))));
    }
    if (method == "ObjectTree") {
        return service_.objectTree(stringArgument(args, 0), static_cast<std::size_t>(std::max(0, intNumberArgument(args, 1, 0))),
                                   static_cast<std::size_t>(std::max(0, intNumberArgument(args, 2, 0))));
    }
    if (method == "PackfileDiagnostics") return service_.packfileDiagnostics(stringArgument(args, 0));
    if (method == "RepackCheck") return service_.repackCheck(stringArgument(args, 0));
    if (method == "SearchHkx") {
        return service_.searchHkx(stringArgument(args, 0), stringArgument(args, 1),
                                  static_cast<std::size_t>(std::max(0, intNumberArgument(args, 2, 0))));
    }
    if (method == "AuditChannels") {
        return service_.auditChannels(stringArgument(args, 0), stringArgument(args, 1),
                                      static_cast<std::size_t>(std::max(0, intNumberArgument(args, 2, 0))));
    }
    if (method == "SymbolUsage") {
        return service_.symbolUsage(stringArgument(args, 0), static_cast<std::size_t>(std::max(0, intNumberArgument(args, 1, 0))));
    }
    if (method == "SaveGltfAnimation") {
        hkx::GltfSaveRequest request;
        request.templatePath = stringArgument(args, 0);
        request.skeletonPath = stringArgument(args, 1);
        request.gltfPath = stringArgument(args, 2);
        request.outputPath = stringArgument(args, 3);
        request.animation = stringArgument(args, 4);
        request.skin = stringArgument(args, 5);
        request.replaceAnnotations = boolArgument(args, 6);
        request.replaceMotion = boolArgument(args, 7);
        request.overwrite = boolArgument(args, 8);
        return service_.saveGltfAnimation(request);
    }
    if (method == "ExtractAnimationAsset") {
        hkx::AnimationIndexQuery sources;
        sources.dataFolder = fo4::io::from_utf8(stringArgument(args, 0));
        sources.mo2Folder = fo4::io::from_utf8(stringArgument(args, 1));
        sources.profile = stringArgument(args, 2);
        return service_.extractAnimationAsset(sources, stringArgument(args, 3));
    }
    if (method == "SkeletonDocument") return service_.skeletonDocument(stringArgument(args, 0));
    if (method == "SkeletonEdit") {
        const auto bone = intNumberArgument(args, 2, -1);
        if (bone < 0) return std::unexpected(Error{"InvalidBone", "bone must be zero or more"});
        return service_.editSkeleton(stringArgument(args, 0), stringArgument(args, 1),
                                     static_cast<std::size_t>(bone), stringArgument(args, 3),
                                     intNumberArgument(args, 4, -1));
    }
    if (method == "SkeletonPreview" || method == "SkeletonSave") {
        const auto editsText = stringArgument(args, 1);
        const auto edits = nlohmann::json::parse(editsText, nullptr, false);
        if (edits.is_discarded()) return std::unexpected(Error{"InvalidEdits", "edits must be a JSON array"});
        if (method == "SkeletonPreview") return service_.previewSkeleton(stringArgument(args, 0), edits);
        return service_.saveSkeleton(stringArgument(args, 0), edits, stringArgument(args, 2), stringArgument(args, 3));
    }
    if (method == "SkeletonWrite") {
        const auto* given = argument(args, 1);
        auto edits = given ? *given : nlohmann::json::array();
        if (edits.is_string()) edits = nlohmann::json::parse(edits.get<std::string>(), nullptr, false);
        if (edits.is_discarded()) return std::unexpected(Error{"InvalidEdits", "edits must be a JSON array"});
        return service_.writeSkeleton(stringArgument(args, 0), edits, stringArgument(args, 2),
                                      boolArgument(args, 3), stringArgument(args, 4));
    }
    if (method == "ClipEdit") {
        const auto* given = argument(args, 1);
        nlohmann::json ops = given == nullptr ? nlohmann::json() : *given;
        if (ops.is_string()) ops = nlohmann::json::parse(ops.get<std::string>(), nullptr, false);
        if (ops.is_discarded()) return std::unexpected(Error{"InvalidClipEdit", "ops is not valid JSON"});
        hkx::HkxService::ClipSaveOptions save;
        if (const auto* spline = argument(args, 5); spline != nullptr && spline->is_boolean()) save.spline = spline->get<bool>();
        const auto limit = [&](std::size_t index) -> std::optional<float> {
            const auto* value = argument(args, index);
            if (value == nullptr || !value->is_number()) return std::nullopt;
            return value->get<float>();
        };
        save.maxPositionError = limit(6);
        save.maxRotationError = limit(7);
        save.maxScaleError = limit(8);
        return service_.clipEdit(stringArgument(args, 0), ops, save, stringArgument(args, 2),
                                 stringArgument(args, 3), boolArgument(args, 4));
    }
    if (method == "ConvertAsset") {
        return service_.convertAsset(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2),
                                     stringArgument(args, 3), boolArgument(args, 4));
    }
    return std::unexpected(Error{"UnknownMethod", "Unknown HKX RPC method: " + std::string(method)});
}

CellTarget::CellTarget(plugin::IPluginBackend& backend,
                       assets::NifService& nif,
                       assets::TextureResolver& textures)
    : records_(backend, "cell"), nif_(nif), textures_(textures) {}

Result<nlohmann::json> CellTarget::invoke(std::string_view method,
                                          const nlohmann::json& args) {
    // Constraint: the asset ops return a serialized JSON string; every other method goes to the Radicle cell facet unchanged.
    if (method == "GetGeometryBatch") {
        try {
            const auto parsed = nlohmann::json::parse(stringArgument(args, 0));
            if (!parsed.is_array()) {
                return nlohmann::json(nlohmann::json{
                    {"error", "Error: GetGeometryBatch expects a JSON array of model paths."}}
                                          .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            }
            std::vector<std::string> paths;
            for (const auto& entry : parsed) {
                if (entry.is_string()) paths.push_back(entry.get<std::string>());
            }
            return nlohmann::json(geometryBatch(paths));
        } catch (const std::exception& ex) {
            return nlohmann::json(
                nlohmann::json{{"error", "Error: " + std::string(ex.what())}}
                    .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
    }
    if (method == "GetGeometryBatchProgress") {
        return nlohmann::json(geometryBatchProgressJson(
            geoBatchDone_.load(std::memory_order_relaxed),
            geoBatchTotal_.load(std::memory_order_relaxed),
            textures_.archiveIndexProgress()));
    }
    // Requires the model to resolve to a NIF first; an unresolved model yields an empty string.
    if (method == "GetTexture") {
        try {
            const auto model = stringArgument(args, 0);
            const auto known = trim(model);
            std::string nifPath;
            std::error_code ec;
            if (std::filesystem::path(known).is_absolute() && std::filesystem::is_regular_file(known, ec)) {
                nifPath = known;
            } else {
                const auto resolved = textures_.resolveNif(model);
                if (!resolved) return nlohmann::json(std::string{});
                nifPath = resolved->string();
            }
            return nlohmann::json(textures_.getTexturePngDataUrl(nifPath, stringArgument(args, 1), ""));
        } catch (const std::exception&) {
            return nlohmann::json(std::string{});
        } catch (...) {
            return nlohmann::json(std::string{});
        }
    }
    if (method == "GetCellTimings") {
        nlohmann::json batch;
        assets::TextureResolverStats since{};
        {
            std::lock_guard<std::mutex> guard(batchMutex_);
            batch = lastBatch_;
            since = batchEndStats_;
        }
        const auto now = textures_.stats();
        const auto delta = [](std::uint64_t current, std::uint64_t then) {
            return current > then ? current - then : std::uint64_t{0};
        };
        const auto ms = [](std::uint64_t ns) { return static_cast<double>(ns) / 1e6; };
        nlohmann::json textures{
            {"textureCalls", delta(now.textureCalls, since.textureCalls)},
            {"decodeMs", ms(delta(now.decodeNs, since.decodeNs))},
            {"pngWriteMs", ms(delta(now.pngWriteNs, since.pngWriteNs))},
            {"base64Ms", ms(delta(now.base64Ns, since.base64Ns))}};
        return nlohmann::json(nlohmann::json{{"batch", batch}, {"textures", textures}}.dump());
    }
    return records_.invoke(method, args);
}

std::string CellTarget::geometryBatch(const std::vector<std::string>& relModelPaths) {
    std::vector<std::string> unique;
    std::vector<std::pair<std::string, std::size_t>> requested;
    std::vector<std::size_t> uses;
    {
        std::map<std::string, std::size_t> slotOf;
        for (const auto& raw : relModelPaths) {
            auto path = trim(raw);
            if (path.empty()) continue;
            const auto [slot, inserted] = slotOf.try_emplace(toLower(path), unique.size());
            if (inserted) {
                unique.push_back(std::move(path));
                uses.push_back(0);
            }
            ++uses[slot->second];
            requested.emplace_back(raw, slot->second);
        }
    }

    const int total = static_cast<int>(unique.size());
    geoBatchTotal_.store(total, std::memory_order_relaxed);
    geoBatchDone_.store(0, std::memory_order_relaxed);

    // Invariant: each worker writes only its own result slot; the work index and progress counters are atomic.
    const auto batchStart = core::StageClock::now();
    const auto before = textures_.stats();
    std::vector<nlohmann::json> results(unique.size());
    std::vector<std::uint64_t> itemNs(unique.size(), 0);
    std::atomic<std::uint64_t> convertNs{0};
    auto& materials = textures_.bgsmMemo();

    const auto processOne = [&](std::size_t i) {
        const auto itemStart = core::StageClock::now();
        // Constraint: a worker-thread exception escaping this lambda calls std::terminate.
        try {
            const auto resolved = textures_.resolveNif(unique[i]);
            if (!resolved) {
                results[i] = nlohmann::json{{"error", "Not found (loose or in any indexed archive)."}};
            } else {
                nlohmann::json entry;
                std::string raw;
                bool ok = false;
                {
                    core::ScopedNs convert(convertNs);
                    const auto geo = nif_.geometry(*resolved);
                    raw = geo ? *geo : ("Error: " + geo.error().message);
                    const auto trimmed = trim(raw);
                    ok = !trimmed.empty() && trimmed.front() == '{';
                    if (ok) {
                        entry = nlohmann::json::parse(raw, nullptr, false);
                        ok = !entry.is_discarded();
                    }
                }
                if (ok) assets::attachShapeMaterials(entry, *resolved, textures_, materials);
                results[i] = ok ? std::move(entry) : nlohmann::json{{"error", raw}};
            }
        } catch (const std::exception& ex) {
            results[i] = nlohmann::json{{"error", std::string("Error: ") + ex.what()}};
        } catch (...) {
            results[i] = nlohmann::json{{"error", "Error: unknown failure resolving geometry"}};
        }
        itemNs[i] = core::elapsedNs(itemStart);
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
        std::vector<std::jthread> workers;
        workers.reserve(degree);
        try {
            for (unsigned int w = 0; w < degree; ++w) {
                workers.emplace_back([&] {
                    for (;;) {
                        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                        if (i >= unique.size()) break;
                        processOne(i);
                    }
                });
            }
        } catch (const std::system_error&) {
        }
    }
    for (std::size_t i = 0; i < unique.size(); ++i) {
        if (results[i].is_null()) processOne(i);
    }

    const auto wallNs = core::elapsedNs(batchStart);
    std::uint64_t threadSumNs = 0;
    std::uint64_t slowestNs = 0;
    std::size_t slowest = 0;
    std::size_t errors = 0;
    for (std::size_t i = 0; i < unique.size(); ++i) {
        threadSumNs += itemNs[i];
        if (itemNs[i] > slowestNs) {
            slowestNs = itemNs[i];
            slowest = i;
        }
        if (results[i].is_object() && results[i].contains("error")) ++errors;
    }
    const auto after = textures_.stats();
    const auto delta = [](std::uint64_t now, std::uint64_t then) {
        return now > then ? now - then : std::uint64_t{0};
    };
    const auto ms = [](std::uint64_t ns) { return static_cast<double>(ns) / 1e6; };
    const auto indexNs = delta(after.indexNs, before.indexNs);
    const auto extractNs = delta(after.extractNs, before.extractNs);
    const auto nestedNs = indexNs + extractNs;
    const auto resolveNs = delta(after.resolveNs, before.resolveNs);
    const std::string slowestPath = unique.empty() ? std::string{} : unique[slowest];

    nlohmann::json geometryMap = nlohmann::json::object();
    for (const auto& [raw, slot] : requested) {
        geometryMap[raw] = --uses[slot] == 0 ? std::move(results[slot]) : results[slot];
    }
    nlohmann::json payload;
    payload["count"] = total;
    payload["geometry"] = std::move(geometryMap);

    nlohmann::json timings{
        {"meshes", total},
        {"errors", errors},
        {"wallMs", ms(wallNs)},
        {"threadSumMs", ms(threadSumNs)},
        {"slowestMs", ms(slowestNs)},
        {"slowestPath", slowestPath},
        {"indexMs", ms(indexNs)},
        {"resolveMs", ms(resolveNs > nestedNs ? resolveNs - nestedNs : std::uint64_t{0})},
        {"extractMs", ms(extractNs)},
        {"convertMs", ms(convertNs.load(std::memory_order_relaxed))},
        {"resolver", {
            {"lookups", delta(after.lookups, before.lookups)},
            {"memoHits", delta(after.memoHits, before.memoHits)},
            {"memoMisses", delta(after.memoMisses, before.memoMisses)},
            {"archiveOpens", delta(after.archiveOpens, before.archiveOpens)},
            {"extractCount", delta(after.extractCount, before.extractCount)},
            {"extractCacheHits", delta(after.extractCacheHits, before.extractCacheHits)}}}};

    const auto dumpStart = core::StageClock::now();
    std::string text = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    timings["dumpMs"] = ms(core::elapsedNs(dumpStart));
    timings["jsonBytes"] = text.size();
    {
        std::lock_guard<std::mutex> guard(batchMutex_);
        lastBatch_ = timings;
        batchEndStats_ = textures_.stats();
    }
    text.pop_back();
    text += ",\"timings\":" + timings.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "}";
    return text;
}

}
