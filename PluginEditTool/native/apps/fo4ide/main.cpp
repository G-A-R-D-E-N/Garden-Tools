#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/app/AppInteropTarget.hpp>
#include <fo4ide/app/ArchiveTarget.hpp>
#include <fo4ide/app/AudioTarget.hpp>
#include <fo4ide/app/MaterialTarget.hpp>
#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/archive/ArchiveService.hpp>
#include <fo4ide/archive/RadicleArchiveBackend.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/audio/AudioService.hpp>
#include <fo4ide/events/EventBus.hpp>
#if defined(FO4IDE_HAS_HTTP)
#include <fo4ide/http/HttpServer.hpp>
#endif
#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/material/MaterialService.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/platform/NativeWindow.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

namespace {

std::atomic_bool stopRequested{false};

void requestStop(int) {
    stopRequested.store(true);
}

std::filesystem::path configDirectory() {
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"); appData && *appData) {
        return std::filesystem::path(appData) / "FO4RecordEditor";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "FO4RecordEditor";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "FO4RecordEditor";
    }
#endif
    return std::filesystem::current_path() / ".fo4ide";
}

std::filesystem::path webRootFromArgs(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--web-root") return argv[i + 1];
    }

    const auto besideExe = std::filesystem::absolute(argv[0]).parent_path() / "web" / "dist";
    if (std::filesystem::is_directory(besideExe)) return besideExe;

    const std::filesystem::path sourceRoot(FO4IDE_SOURCE_WEB_ROOT);
    if (std::filesystem::is_directory(sourceRoot)) return sourceRoot;
    return {};
}

class AppTarget final : public fo4ide::rpc::Target {
public:
    AppTarget(fo4ide::plugin::IPluginBackend& plugins,
              fo4ide::archive::ArchiveService& archives,
              std::filesystem::path nifTool)
        : plugins_(plugins), archives_(archives), nifTool_(std::move(nifTool)) {}

    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                           const nlohmann::json&) override {
        if (method == "GetHealth") {
            const auto capabilities = plugins_.capabilities();
            return nlohmann::json{
                {"ok", true},
                {"native", true},
                {"pluginBackend", capabilities.available},
                {"pluginRead", capabilities.readPlugins},
                {"pluginWrite", capabilities.writePlugins},
                {"archiveReadBackend", archives_.readBackendAvailable()},
                {"nifTool", !nifTool_.empty()},
                {"materialCodec", true},
                {"audioTools", true},
                {"embeddedWindow", fo4ide::platform::NativeWindow::available()}
            };
        }
        return std::unexpected(fo4ide::Error{"UnknownMethod", "Unknown app RPC method"});
    }

private:
    fo4ide::plugin::IPluginBackend& plugins_;
    fo4ide::archive::ArchiveService& archives_;
    std::filesystem::path nifTool_;
};

bool registerTarget(fo4ide::rpc::Dispatcher& rpc,
                    std::string name,
                    std::shared_ptr<fo4ide::rpc::Target> target,
                    const std::shared_ptr<spdlog::logger>& log) {
    auto registered = rpc.registerTarget(std::move(name), std::move(target));
    if (registered) return true;
    log->error("RPC setup failed: {}", registered.error().message);
    return false;
}

bool registerMcpTool(fo4ide::mcp::Registry& registry,
                     std::shared_ptr<fo4ide::mcp::Tool> tool,
                     const std::shared_ptr<spdlog::logger>& log) {
    auto registered = registry.registerTool(std::move(tool));
    if (registered) return true;
    log->error("MCP setup failed: {}", registered.error().message);
    return false;
}

nlohmann::json objectSchema(nlohmann::json properties,
                            nlohmann::json required = nlohmann::json::array()) {
    nlohmann::json schema{{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) schema["required"] = std::move(required);
    schema["additionalProperties"] = false;
    return schema;
}

fo4ide::mcp::ToolResult rpcToolText(const fo4ide::rpc::Response& response) {
    if (!response.ok) {
        const auto prefix = response.type.empty() ? std::string{} : response.type + ": ";
        return {prefix + response.error, true};
    }
    if (response.value.is_string()) return {response.value.get<std::string>(), false};
    if (response.value.is_null()) return {"", false};
    return {response.value.dump(), false};
}

bool registerNativeMcpTools(fo4ide::mcp::Registry& registry,
                            const fo4ide::rpc::Dispatcher& rpc,
                            const std::shared_ptr<spdlog::logger>& log) {
    using fo4ide::mcp::FunctionTool;
    using fo4ide::mcp::RpcTool;
    using fo4ide::mcp::ToolDefinition;

    const auto add = [&](ToolDefinition definition,
                         std::string target,
                         std::string method,
                         std::vector<std::string> argumentOrder = {}) {
        return registerMcpTool(
            registry,
            std::make_shared<RpcTool>(
                std::move(definition), rpc, std::move(target), std::move(method), std::move(argumentOrder)),
            log);
    };

    const auto emptySchema = objectSchema(nlohmann::json::object());
    if (!add({"native_status", "Report the native FO4IDE host and backend capability status", emptySchema},
             "app", "GetHealth")) return false;
    if (!add({"get_mo2_status", "Report the loaded MO2 instance/profile and whether its files changed on disk", emptySchema},
             "appInterop", "GetMo2Status")) return false;
    if (!add({"reload_mo2_profile", "Reload the currently configured MO2 profile from disk", emptySchema},
             "appInterop", "ReloadMo2Profile")) return false;
    if (!add({"get_settings", "Return the current FO4IDE settings", emptySchema},
             "settings", "GetSettings")) return false;

    const auto papyrusCompileSchema = objectSchema(
        {
            {"source", {{"type", "string"},
                        {"description", "A .psc file path, or a folder of scripts (with all=true)."}}},
            {"output", {{"type", "string"}, {"description", "Output dir for .pex (default: the source folder)."}}},
            {"imports", {{"type", "string"}, {"description", "Extra import roots, semicolon-separated."}}},
            {"flags", {{"type", "string"}, {"description", "Flags file (default Institute_Papyrus_Flags.flg)."}}},
            {"all", {{"type", "boolean"}, {"description", "Compile every .psc in the source folder."}}},
            {"optimize", {{"type", "boolean"}, {"description", "Optimize (-op). Creation Kit engine only."}}},
            {"release", {{"type", "boolean"}, {"description", "Release build: strips DebugOnly/BetaOnly calls."}}},
            {"engine", {{"type", "string"},
                        {"description", "'auto' (default), 'builtin', or 'creationkit'."}}},
            {"debug_info", {{"type", "boolean"}, {"description", "Emit line numbers and property groups (default true)."}}},
            {"compiler_path", {{"type", "string"}, {"description", "Override path to PapyrusCompiler.exe."}}}
        },
        nlohmann::json::array({"source"}));
    if (!add({
            "compile_papyrus",
            "Compile Papyrus source .psc -> .pex with the built-in engine (auto-detects a single file "
            "or a folder; 'creationkit' shells out to PapyrusCompiler.exe). Returns a 'RESULT: X "
            "succeeded, Y failed' summary.",
            papyrusCompileSchema},
            "papyrus", "Compile",
            {"source", "output", "imports", "flags", "all", "optimize", "release",
             "compiler_path", "engine", "debug_info"})) return false;
    if (!add({
            "decompile_papyrus",
            "Decompile compiled Papyrus .pex -> source .psc with the built-in decompiler (single file "
            "or folder; assembly=true emits a bytecode listing). Namespaced scripts are written into "
            "namespace subfolders so they recompile as-is.",
            objectSchema({
                {"source", {{"type", "string"}, {"description", "A .pex file, or a folder of .pex files."}}},
                {"output", {{"type", "string"}, {"description", "Output dir when writing."}}},
                {"assembly", {{"type", "boolean"}, {"description", "Emit bytecode disassembly instead of source."}}},
                {"write", {{"type", "boolean"}, {"description", "Write .psc/.pas files instead of returning text inline."}}}
            }, nlohmann::json::array({"source"}))},
            "papyrus", "Decompile", {"source", "output", "assembly", "write"})) return false;
    if (!add({
            "papyrus_check",
            "Check Papyrus SOURCE (.psc) with the built-in front end (no Creation Kit): syntax, then "
            "name resolution and type checking. Reports 'file(line,col): error CODE: message' per "
            "problem; files whose parent/import/type is not on the roots are counted separately.",
            objectSchema({
                {"source", {{"type", "string"}, {"description", "A .psc file path, or a folder of scripts."}}},
                {"semantic", {{"type", "boolean"}, {"description", "Resolve names and check types too (default true)."}}},
                {"imports", {{"type", "string"}, {"description", "Extra source roots, semicolon-separated."}}}
            }, nlohmann::json::array({"source"}))},
            "papyrus", "Check", {"source", "semantic", "imports"})) return false;
    if (!add({
            "papyrus_function_lookup",
            "Look up a Papyrus function in the offline Creation Kit Wiki mirror.",
            objectSchema({
                {"function", {{"type", "string"}, {"description", "Function name, e.g. 'GetBaseObject'."}}},
                {"script", {{"type", "string"}, {"description", "Owning script (optional but disambiguates)."}}}
            }, nlohmann::json::array({"function"}))},
            "papyrus", "LookupFunction", {"script", "function"})) return false;
    if (!add({
            "papyrus_script_info",
            "Get a Papyrus script's overview (Extends, Global/Member Functions, Events) from the "
            "offline Creation Kit Wiki mirror.",
            objectSchema({{"script", {{"type", "string"},
                                         {"description", "Script name, e.g. 'ObjectReference' or 'Actor'."}}}},
                         nlohmann::json::array({"script"}))},
            "papyrus", "LookupScriptInfo", {"script"})) return false;

    const auto nifPathSchema = objectSchema(
        {{"path", {{"type", "string"}, {"description", "Path to the NIF file"}}}},
        nlohmann::json::array({"path"}));
    if (!add({"nif_inspect", "Inspect a NIF through the native niftool wrapper", nifPathSchema},
             "nif", "Inspect", {"path"})) return false;
    if (!add({"nif_verify", "Verify a NIF through the native niftool wrapper", nifPathSchema},
             "nif", "Verify", {"path"})) return false;
    if (!add({"nif_tree", "Return the NIF block tree through the native niftool wrapper", nifPathSchema},
             "nif", "Tree", {"path"})) return false;
    if (!add({"nif_geometry", "Return geometry data through the native niftool wrapper", nifPathSchema},
             "nif", "Geo", {"path"})) return false;

    if (!add({
            "audio_convert_to_xwm",
            "Convert one audio/video file or a folder to Fallout 4 XWM",
            objectSchema({
                {"source", {{"type", "string"}}},
                {"output", {{"type", "string"}}},
                {"bitrateBps", {{"type", "integer"}, {"minimum", 1}}}
            }, nlohmann::json::array({"source"}))},
            "audio", "ConvertToXwm", {"source", "output", "bitrateBps"})) return false;
    if (!add({
            "audio_convert_from_xwm",
            "Decode an XWM file to WAV or another ffmpeg-supported format",
            objectSchema({
                {"source", {{"type", "string"}}},
                {"output", {{"type", "string"}}},
                {"targetExt", {{"type", "string"}, {"default", "wav"}}}
            }, nlohmann::json::array({"source"}))},
            "audio", "ConvertFromXwm", {"source", "output", "targetExt"})) return false;
    if (!add({
            "audio_make_fuz",
            "Create a Fallout 4 FUZ package from audio/XWM and an optional LIP file",
            objectSchema({
                {"audioSource", {{"type", "string"}}},
                {"lipPath", {{"type", "string"}}},
                {"fuzOutput", {{"type", "string"}}},
                {"noLip", {{"type", "boolean"}, {"default", false}}}
            }, nlohmann::json::array({"audioSource", "fuzOutput"}))},
            "audio", "MakeFuz", {"audioSource", "lipPath", "fuzOutput", "noLip"})) return false;
    if (!add({
            "audio_extract_fuz",
            "Extract XWM/LIP data from a Fallout 4 FUZ package",
            objectSchema({
                {"fuzPath", {{"type", "string"}}},
                {"xwmOutput", {{"type", "string"}}},
                {"lipOutput", {{"type", "string"}}},
                {"alsoWav", {{"type", "boolean"}, {"default", false}}}
            }, nlohmann::json::array({"fuzPath"}))},
            "audio", "ExtractFuz", {"fuzPath", "xwmOutput", "lipOutput", "alsoWav"})) return false;

    if (!add({
            "material_inspect",
            "Inspect a Fallout 4 BGSM or BGEM material through the native codec",
            objectSchema({{"path", {{"type", "string"}}}}, nlohmann::json::array({"path"}))},
            "material", "Inspect", {"path"})) return false;

    ToolDefinition materialSet{
        .name = "material_set_fields",
        .description = "Set one or more named BGSM/BGEM material fields through the native codec",
        .inputSchema = objectSchema({
            {"path", {{"type", "string"}}},
            {"fields", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}},
            {"output", {{"type", "string"}}}
        }, nlohmann::json::array({"path", "fields"}))
    };
    if (!registerMcpTool(
            registry,
            std::make_shared<FunctionTool>(std::move(materialSet), [&rpc](const nlohmann::json& args) {
                if (!args.is_object() || !args.contains("path") || !args.at("path").is_string() ||
                    !args.contains("fields") || !args.at("fields").is_object()) {
                    return fo4ide::mcp::ToolResult{"path and fields are required", true};
                }
                const auto output = args.value("output", std::string{});
                const auto response = rpc.invoke({
                    "material",
                    "SetFields",
                    nlohmann::json::array({args.at("path"), args.at("fields").dump(), output})
                });
                return rpcToolText(response);
            }),
            log)) return false;

    ToolDefinition archivePack{
        .name = "archive_pack",
        .description = "Pack folders into a Fallout 4 BA2 through Archive2.exe",
        .inputSchema = objectSchema({
            {"sources", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}}},
            {"output", {{"type", "string"}}},
            {"format", {{"type", "string"}, {"enum", nlohmann::json::array({"General", "DDS"})}}},
            {"rootDir", {{"type", "string"}}},
            {"compress", {{"type", "boolean"}, {"default", true}}}
        }, nlohmann::json::array({"sources", "output", "rootDir"}))
    };
    if (!registerMcpTool(
            registry,
            std::make_shared<FunctionTool>(std::move(archivePack), [&rpc](const nlohmann::json& args) {
                if (!args.is_object() || !args.contains("sources") || !args.at("sources").is_array()) {
                    return fo4ide::mcp::ToolResult{"sources is required", true};
                }
                const auto response = rpc.invoke({
                    "archive",
                    "Pack",
                    nlohmann::json::array({
                        args.at("sources").dump(),
                        args.value("output", std::string{}),
                        args.value("format", std::string("General")),
                        args.value("rootDir", std::string{}),
                        args.value("compress", true)
                    })
                });
                return rpcToolText(response);
            }),
            log)) return false;

    return true;
}

}

int main(int argc, char** argv) {
    const auto configDir = configDirectory();
    auto logInit = fo4ide::logging::initialize({.directory = configDir / "logs"});
    if (!logInit) {
        std::cerr << "Logging initialization failed: " << logInit.error().message << '\n';
        return 1;
    }

    auto log = fo4ide::logging::get("app");
    log->info("FO4IDE native host starting");

    fo4ide::settings::Store settingsStore(configDir / "settings.json");
    fo4ide::settings::Settings currentSettings;
    if (auto loaded = settingsStore.load()) {
        currentSettings = std::move(*loaded);
    } else {
        log->warn("Settings load failed: {}", loaded.error().message);
    }

    fo4ide::process::ReprocRunner processRunner;
    fo4ide::platform::NativeServices platformServices(processRunner);
    fo4ide::tools::Locator toolLocator(currentSettings, argv[0]);
    const auto nifTool = toolLocator.resolve(fo4ide::tools::Tool::NifTool);
    fo4ide::assets::NifService nifService(processRunner, nifTool);
    fo4ide::audio::AudioService audioService(processRunner, toolLocator);
    fo4ide::material::MaterialService materialService;
    fo4ide::archive::RadicleArchiveBackend archiveBackend;
    fo4ide::archive::ArchiveService archiveService(processRunner, toolLocator, archiveBackend);
    // Texture resolution mirrors the C# TextureService: data roots come from
    // the configured game folder and MO2 instance (mods), so loose files and
    // BA2 archives are searched in that order, matching the C# GlobalRoots.
    fo4ide::assets::TextureResolver textureResolver;
    {
        std::vector<std::filesystem::path> roots;
        if (!currentSettings.dataFolder.empty()) roots.push_back(currentSettings.dataFolder);
        if (!currentSettings.mo2InstancePath.empty()) {
            roots.push_back(currentSettings.mo2InstancePath / "mods");
        }
        textureResolver.setSessionRoots(std::move(roots));
    }
    fo4ide::events::EventBus events;
    fo4ide::plugin::RadiclePluginBackend plugins;
    // Path-based game-Data protection: plugins physically inside the game's
    // Data directory are read-only even if their reported source is missing.
    if (const auto falloutRoot = toolLocator.fallout4Root(); !falloutRoot.empty()) {
        plugins.setDataDirectory(falloutRoot / "Data");
    }
    fo4ide::rpc::Dispatcher rpc;
    auto appInteropTarget = std::make_shared<fo4ide::app::AppInteropTarget>(
        settingsStore, currentSettings, platformServices, plugins);

    if (!registerTarget(rpc, "app", std::make_shared<AppTarget>(plugins, archiveService, nifTool), log) ||
        !registerTarget(rpc, "appInterop", appInteropTarget, log) ||
        !registerTarget(rpc, "settings", std::make_shared<fo4ide::app::SettingsTarget>(settingsStore, currentSettings, platformServices, processRunner), log) ||
        !registerTarget(rpc, "nif", std::make_shared<fo4ide::app::NifTarget>(nifService, textureResolver, platformServices), log) ||
        !registerTarget(rpc, "papyrus", std::make_shared<fo4ide::app::PapyrusTarget>(processRunner, toolLocator, platformServices), log) ||
        !registerTarget(rpc, "audio", std::make_shared<fo4ide::app::AudioTarget>(audioService, platformServices), log) ||
        !registerTarget(rpc, "material", std::make_shared<fo4ide::app::MaterialTarget>(materialService, platformServices), log) ||
        !registerTarget(rpc, "archive", std::make_shared<fo4ide::app::ArchiveTarget>(archiveService, platformServices), log) ||
        !registerTarget(rpc, "backend", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "backend"), log) ||
        !registerTarget(rpc, "masters", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "masters"), log) ||
        !registerTarget(rpc, "cell", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "cell"), log) ||
        !registerTarget(rpc, "graph", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "graph"), log)) {
        fo4ide::logging::shutdown();
        return 1;
    }

    fo4ide::mcp::Registry mcpRegistry;
    if (!registerNativeMcpTools(mcpRegistry, rpc, log)) {
        fo4ide::logging::shutdown();
        return 1;
    }

    bool printHealth = false;
    bool runServer = false;
    bool openBrowser = false;
    bool openWindow = false;
    bool runMcp = false;
    int requestedPort = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--health") printHealth = true;
        if (arg == "--headless" || arg == "--serve") runServer = true;
        if (arg == "--browser") { runServer = true; openBrowser = true; }
        if (arg == "--window") { runServer = true; openWindow = true; }
        if (arg == "--mcp") runMcp = true;
        if (arg == "--port" && i + 1 < argc) requestedPort = std::stoi(argv[++i]);
    }

    if (!printHealth && !runMcp && !runServer) {
#if defined(_WIN32)
        runServer = true;
        openWindow = true;
#else
        runServer = true;
        openBrowser = true;
#endif
    }

    if (runMcp) {
        log->info("Starting native stdio MCP server");
        fo4ide::mcp::StdioServer server(mcpRegistry);
        server.run(std::cin, std::cout);
        events.shutdown();
        fo4ide::logging::shutdown();
        return 0;
    }

    if (printHealth) {
        const auto response = rpc.invoke({"app", "GetHealth", nlohmann::json::array()});
        std::cout << response.toJson().dump(2) << '\n';
    }

#if defined(FO4IDE_HAS_HTTP)
    if (runServer) {
        const auto webRoot = webRootFromArgs(argc, argv);
        if (webRoot.empty()) {
            std::cerr << "web/dist was not found. Build it with: cd web && npm ci && npm run build\n";
            fo4ide::logging::shutdown();
            return 2;
        }

        fo4ide::http::HttpServer server(rpc, events);
        auto started = server.start({
            .host = "127.0.0.1",
            .port = requestedPort,
            .webRoot = webRoot,
        });
        if (!started) {
            std::cerr << "HTTP server failed: " << started.error().message << '\n';
            fo4ide::logging::shutdown();
            return 3;
        }

        const auto url = "http://127.0.0.1:" + std::to_string(*started) + "/#/main";
        std::cout << "FO4IDE native server: " << url << '\n';
        log->info("HTTP server listening at {}", url);

        int exitCode = 0;
        if (openWindow) {
            fo4ide::platform::NativeWindow window;
            auto windowResult = window.run({
                .title = "FO4IDE",
                .url = url,
                .userDataDirectory = configDir / "WebView2",
                .width = 1440,
                .height = 900,
            });
            if (!windowResult) {
                log->error("Native window failed: {}", windowResult.error().message);
                std::cerr << "Native window failed: " << windowResult.error().message << '\n';
                exitCode = 5;
            } else {
                exitCode = *windowResult;
            }
        } else {
            if (openBrowser) {
                auto opened = platformServices.openExternal(url);
                if (!opened) log->warn("Could not open browser: {}", opened.error().message);
            }
            std::signal(SIGINT, requestStop);
            std::signal(SIGTERM, requestStop);
            while (!stopRequested.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        server.stop();
        events.shutdown();
        log->info("FO4IDE native host stopped");
        fo4ide::logging::shutdown();
        return exitCode;
    }
#else
    if (runServer) {
        std::cerr << "This build was configured with FO4IDE_BUILD_HTTP=OFF.\n";
        fo4ide::logging::shutdown();
        return 4;
    }
#endif

    events.shutdown();
    log->info("FO4IDE native host stopped");
    fo4ide::logging::shutdown();
    return 0;
}
