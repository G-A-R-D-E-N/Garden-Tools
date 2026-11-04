#include <fo4ide/app/ServiceTargets.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

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
    if (method == "Geo") return serviceJsonText(service_.geometry(stringArgument(args, 0)));
    if (method == "Verify") return serviceText(service_.verify(stringArgument(args, 0)));
    if (method == "Tree") return serviceJsonText(service_.tree(stringArgument(args, 0)));
    if (method == "Fix") {
        return serviceText(service_.fix(stringArgument(args, 0), stringArgument(args, 1)));
    }
    if (method == "ShapeOp") {
        try {
            const auto* crease = argument(args, 4);
            const auto creaseDegrees = crease && crease->is_number() ? crease->get<float>() : 180.0f;
            auto result = service_.shapeOp(stringArgument(args, 0), stringArgument(args, 1),
                                           stringArgument(args, 2), shapeBlocks(argument(args, 3)),
                                           creaseDegrees, boolArgument(args, 5));
            if (!result && result.error().type == "OutputExists") return nlohmann::json("Error: " + result.error().message);
            return serviceText(std::move(result));
        } catch (const std::exception& ex) {
            return nlohmann::json("Error: " + std::string(ex.what()));
        }
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
