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

nlohmann::json serviceText(Result<std::string> result) {
    if (!result) return "Error: " + result.error().message;
    return std::move(*result);
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
        try {
            const auto parsed = nlohmann::json::parse(stringArgument(args, 0));
            if (!parsed.is_array()) {
                return nlohmann::json(nlohmann::json{
                    {"error", "Error: GetGeometryBatch expects a JSON array of model paths."}}
                                          .dump());
            }
            std::vector<std::string> paths;
            for (const auto& entry : parsed) {
                if (entry.is_string()) paths.push_back(entry.get<std::string>());
            }
            return nlohmann::json(geometryBatch(paths));
        } catch (const std::exception& ex) {
            return nlohmann::json(
                nlohmann::json{{"error", "Error: " + std::string(ex.what())}}.dump());
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
    for (std::size_t i = 0; i < unique.size(); ++i) {
        geometryMap[unique[i]] = std::move(results[i]);
    }
    nlohmann::json payload;
    payload["count"] = total;
    payload["geometry"] = std::move(geometryMap);
    return payload.dump();
}

}
