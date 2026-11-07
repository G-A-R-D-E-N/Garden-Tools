#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4/nif/havok_names.hpp>

#include <fo4ide/assets/NifGltfService.hpp>
#include <fo4ide/assets/NifRender.hpp>
#include <fo4ide/assets/VertexColorSurface.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <fo4ide/core/JsonInt.hpp>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <fo4ide/assets/NifMaterialLink.hpp>

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
        {"TexconvPath", {"Texconvx64.exe", "Texconv.exe", "texconv"}, false},
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
    if (value && value->is_bool()) {
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

std::vector<std::uint32_t> shapeBlocks(const nlohmann::json* value) {
    if (value == nullptr || value->is_null()) return {};
    const auto list = value->is_string() ? nlohmann::json::parse(value->get<std::string>()) : *value;
    if (!list.is_array()) throw std::invalid_argument("blocks must be a list of block ids");
    std::vector<std::uint32_t> blocks;
    for (const auto& id : list) {
        if (!id.is_number_unsigned() || id.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("blocks must be a list of block ids");
        }
        blocks.push_back(id.get<std::uint32_t>());
    }
    return blocks;
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
            updatePath(payload, "CkWikiPath", current.ckWikiPath);
            updatePath(payload, "TexconvPath", current.texconvPath);
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
    if (method == "Verify") return serviceText(service_.verify(stringArgument(args, 0)));
    if (method == "Tree") return serviceJsonText(service_.tree(stringArgument(args, 0)));
    if (method == "Segments") {
        const auto block = intNumberArgument(args, 1, -1);
        if (block < 0) return std::unexpected(Error{"BlockIndexMissing", "block index required"});
        return serviceJsonText(service_.segments(stringArgument(args, 0), static_cast<std::uint32_t>(block)));
    }
    if (method == "Fix") {
        if (const auto* check = argument(args, 2); check && (!check->is_string() || check->get<std::string>().empty()))
            return serviceText(std::unexpected(Error{"InvalidArgument", "Invalid NIF repair check"}));
        return serviceText(service_.fix(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2)));
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
        if (!exported && exported.error().type == "OutputExists") return nlohmann::json("EXISTS: " + exported.error().message);
        return serviceText(std::move(exported));
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
                                           pruneThreshold, sourceBlock);
            if (!result && result.error().type == "OutputExists") return nlohmann::json("Error: " + result.error().message);
            return serviceText(std::move(result));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
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
        return serviceText(service_.importObj(stringArgument(args, 0), stringArgument(args, 1), options));
    }
    if (method == "CollisionInfo") {
        const auto layers = collisionLayers(plugins_);
        return serviceJsonText(collision_.info(stringArgument(args, 0), layers));
    }
    if (method == "CollisionGeo") return serviceJsonText(collision_.geometry(stringArgument(args, 0)));
    if (method == "CollisionSet") {
        const auto request = collisionSetArguments(args);
        if (!request) return nlohmann::json("Error: " + request.error().message);
        return serviceText(collision_.set(*request));
    }
    if (method == "CollisionRemove") {
        return serviceText(collision_.remove(stringArgument(args, 0), stringArgument(args, 1), stringArgument(args, 2)));
    }
    if (method == "ImportGltf") {
        assets::NifImportOptions options;
        options.material = stringArgument(args, 2);
        options.diffuseTexture = stringArgument(args, 3);
        options.normalTexture = stringArgument(args, 4);
        options.collision = boolArgument(args, 5) ? assets::NifCollisionShape::box : assets::NifCollisionShape::none;
        options.skeleton = stringArgument(args, 6);
        auto imported = service_.importGltf(stringArgument(args, 0), stringArgument(args, 1), options, boolArgument(args, 7));
        if (!imported && imported.error().type == "OutputExists") return nlohmann::json("EXISTS: " + imported.error().message);
        return serviceText(std::move(imported));
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
    if (method == "ConnectPoints") return serviceJsonText(service_.connectPoints(stringArgument(args, 0)));
    if (method == "ApplyConnectPoints") {
        return serviceText(service_.applyConnectPoints(stringArgument(args, 0), stringArgument(args, 1),
                                                       stringArgument(args, 2)));
    }
    if (method == "UvGet") return serviceJsonText(assets::renderUvSummary(stringArgument(args, 0)));
    if (method == "UvTransform") {
        const int block = intNumberArgument(args, 1, -1);
        if (block < 0) return nlohmann::json("Error: block must be a positive shape block id");
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
        return serviceText(assets::applyUvTransform(stringArgument(args, 0), static_cast<std::uint32_t>(block),
                                                    intNumberArgument(args, 2, 0), transform,
                                                    stringArgument(args, 8)));
    }
    if (method == "VertexColorGet") return serviceJsonText(assets::renderVertexColors(stringArgument(args, 0)));
    if (method == "VertexColorSet") {
        const auto op = assets::parseVertexColorOp(args);
        if (!op) return nlohmann::json("Error: " + op.error().message);
        return serviceText(assets::applyVertexColor(stringArgument(args, 0), *op, stringArgument(args, 9)));
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
            return serviceText(service_.createTextureSet(stringArgument(args, 0), block->get<std::uint32_t>(),
                                                         textures->get<std::vector<std::string>>(),
                                                         stringArgument(args, 3)));
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

}

HkxTarget::HkxTarget(hkx::HkxService& service) : service_(service) {}

Result<nlohmann::json> HkxTarget::invoke(std::string_view method, const nlohmann::json& args) {
    if (method == "Skeleton") return service_.skeleton(stringArgument(args, 0));
    if (method == "Pose") {
        const auto seconds = timeArgument(args, 2);
        if (!seconds) return std::unexpected(seconds.error());
        const auto space = poseSpaceArgument(args, 3);
        if (!space) return std::unexpected(space.error());
        return service_.pose(stringArgument(args, 0), stringArgument(args, 1), *seconds, *space);
    }
    return std::unexpected(Error{"UnknownMethod", "Unknown HKX RPC method: " + std::string(method)});
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
        return nlohmann::json(nlohmann::json{
            {"done", geoBatchDone_.load(std::memory_order_relaxed)},
            {"total", geoBatchTotal_.load(std::memory_order_relaxed)}}.dump());
    }
    // Mirrors CellInterop.GetTexture: resolve the model to a NIF, then hand the
    // resolved path to the texture resolver; an unresolved model yields "".
    if (method == "GetTexture") {
        try {
            const auto resolved = textures_.resolveNif(stringArgument(args, 0));
            if (!resolved) return nlohmann::json(std::string{});
            return nlohmann::json(
                textures_.getTexturePngDataUrl(resolved->string(), stringArgument(args, 1), ""));
        } catch (const std::exception&) {
            return nlohmann::json(std::string{});
        } catch (...) {
            return nlohmann::json(std::string{});
        }
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

    // One result slot per unique path: each worker writes only its own slot, so
    // the vector needs no lock; only the work index and progress counters are
    // shared, and those are atomic.
    std::vector<nlohmann::json> results(unique.size());

    const auto processOne = [&](std::size_t i) {
        // A worker-thread exception escaping this lambda calls std::terminate.
        try {
            const auto resolved = textures_.resolveNif(unique[i]);
            if (!resolved) {
                results[i] = nlohmann::json{{"error", "Not found (loose or in any indexed archive)."}};
            } else {
                const auto geo = nif_.geometry(*resolved);
                const std::string raw = geo ? *geo : ("Error: " + geo.error().message);
                const auto trimmed = trim(raw);
                nlohmann::json entry;
                bool ok = !trimmed.empty() && trimmed.front() == '{';
                if (ok) {
                    entry = nlohmann::json::parse(raw, nullptr, false);
                    ok = !entry.is_discarded();
                }
                results[i] = ok ? std::move(entry) : nlohmann::json{{"error", raw}};
            }
        } catch (const std::exception& ex) {
            results[i] = nlohmann::json{{"error", std::string("Error: ") + ex.what()}};
        } catch (...) {
            results[i] = nlohmann::json{{"error", "Error: unknown failure resolving geometry"}};
        }
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

    nlohmann::json geometryMap = nlohmann::json::object();
    for (const auto& [raw, slot] : requested) {
        geometryMap[raw] = --uses[slot] == 0 ? std::move(results[slot]) : results[slot];
    }
    nlohmann::json payload;
    payload["count"] = total;
    payload["geometry"] = std::move(geometryMap);
    return payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}
