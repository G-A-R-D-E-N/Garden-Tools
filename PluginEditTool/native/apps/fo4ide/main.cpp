#include "McpTools.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
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
#include <fo4ide/app/SwfTarget.hpp>
#include <fo4ide/archive/ArchiveService.hpp>
#include <fo4ide/archive/RadicleArchiveBackend.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/audio/AudioService.hpp>
#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/http/HttpServer.hpp>
#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/material/MaterialService.hpp>
#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/mo2/Mo2Profile.hpp>
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

std::vector<std::filesystem::path> textureRootsForSettings(
    const fo4ide::settings::Settings& settings) {
    if (!settings.mo2InstancePath.empty()) {
        auto profile = fo4ide::mo2::readProfile(
            settings.mo2InstancePath, {}, settings.dataFolder);
        if (profile) {
            return std::vector<std::filesystem::path>(
                profile->assetRootsLowToHigh.rbegin(),
                profile->assetRootsLowToHigh.rend());
        }
    }

    std::vector<std::filesystem::path> roots;
    if (!settings.dataFolder.empty()) roots.push_back(settings.dataFolder);
    return roots;
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

class RefreshingSettingsTarget final : public fo4ide::rpc::Target {
public:
    RefreshingSettingsTarget(fo4ide::settings::Store& store,
                             fo4ide::settings::SharedSettings& settings,
                             fo4ide::platform::Services& platform,
                             fo4ide::process::Runner& runner,
                             fo4ide::assets::TextureResolver& textures)
        : inner_(store, settings, platform, runner),
          settings_(settings),
          textures_(textures) {}

    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                          const nlohmann::json& args) override {
        auto result = inner_.invoke(method, args);
        if (method == "SaveSettings" && result && result->is_string() &&
            result->get<std::string>() == "Settings saved.") {
            textures_.setSessionRoots(textureRootsForSettings(settings_.snapshot()));
        }
        return result;
    }

private:
    fo4ide::app::SettingsTarget inner_;
    fo4ide::settings::SharedSettings& settings_;
    fo4ide::assets::TextureResolver& textures_;
};

class RefreshingAppInteropTarget final : public fo4ide::rpc::Target {
public:
    RefreshingAppInteropTarget(std::shared_ptr<fo4ide::app::AppInteropTarget> inner,
                               fo4ide::settings::SharedSettings& settings,
                               fo4ide::assets::TextureResolver& textures)
        : inner_(std::move(inner)), settings_(settings), textures_(textures) {}

    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                          const nlohmann::json& args) override {
        auto result = inner_->invoke(method, args);
        const bool profileChanged = method == "OpenMo2Profile" ||
                                    method == "LoadEnvironment" ||
                                    method == "ReloadMo2Profile";
        if (profileChanged && result) {
            textures_.setSessionRoots(textureRootsForSettings(settings_.snapshot()));
        }
        return result;
    }

private:
    std::shared_ptr<fo4ide::app::AppInteropTarget> inner_;
    fo4ide::settings::SharedSettings& settings_;
    fo4ide::assets::TextureResolver& textures_;
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

#if defined(FO4IDE_HAS_HTTP)
std::string newAccessToken() {
    static constexpr std::string_view digits = "0123456789abcdef";
    std::random_device device;
    std::string token;
    token.reserve(32);
    for (int i = 0; i < 16; ++i) {
        const auto byte = static_cast<unsigned char>(device() & 0xFF);
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0x0F]);
    }
    return token;
}
#endif


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
    fo4ide::settings::Settings loadedSettings;
    if (auto loaded = settingsStore.load()) {
        loadedSettings = std::move(*loaded);
    } else {
        log->warn("Settings load failed: {}", loaded.error().message);
    }
    fo4ide::settings::SharedSettings currentSettings(std::move(loadedSettings));

    fo4ide::process::ReprocRunner processRunner;
    fo4ide::platform::NativeServices platformServices(processRunner);
    fo4ide::tools::Locator toolLocator(currentSettings, argv[0]);
    const auto nifTool = toolLocator.resolve(fo4ide::tools::Tool::NifTool);
    fo4ide::assets::NifService nifService(processRunner, nifTool);
    fo4ide::audio::AudioService audioService(processRunner, toolLocator);
    fo4ide::material::MaterialService materialService;
    fo4ide::archive::RadicleArchiveBackend archiveBackend;
    fo4ide::archive::ArchiveService archiveService(processRunner, toolLocator, archiveBackend);
    fo4ide::assets::TextureResolver textureResolver;
    textureResolver.setSessionRoots(textureRootsForSettings(currentSettings.snapshot()));
    fo4ide::events::EventBus events;
    fo4ide::plugin::RadiclePluginBackend plugins;
    if (const auto falloutRoot = toolLocator.fallout4Root(); !falloutRoot.empty()) {
        plugins.setDataDirectory(falloutRoot / "Data");
    }
    fo4ide::rpc::Dispatcher rpc;
    auto appInteropTarget = std::make_shared<fo4ide::app::AppInteropTarget>(
        settingsStore, currentSettings, platformServices, plugins);
    auto refreshingAppInteropTarget = std::make_shared<RefreshingAppInteropTarget>(
        appInteropTarget, currentSettings, textureResolver);

    auto swfTarget = std::make_shared<fo4ide::app::SwfTarget>([&currentSettings] {
        return fo4ide::app::swfRootsFor(currentSettings.snapshot());
    });

    if (!registerTarget(rpc, "app", std::make_shared<AppTarget>(plugins, archiveService, nifTool), log) ||
        !registerTarget(rpc, "appInterop", refreshingAppInteropTarget, log) ||
        !registerTarget(rpc, "settings", std::make_shared<RefreshingSettingsTarget>(settingsStore, currentSettings, platformServices, processRunner, textureResolver), log) ||
        !registerTarget(rpc, "nif", std::make_shared<fo4ide::app::NifTarget>(nifService, textureResolver, platformServices), log) ||
        !registerTarget(rpc, "papyrus", std::make_shared<fo4ide::app::PapyrusTarget>(processRunner, toolLocator, platformServices), log) ||
        !registerTarget(rpc, "audio", std::make_shared<fo4ide::app::AudioTarget>(audioService, platformServices), log) ||
        !registerTarget(rpc, "material", std::make_shared<fo4ide::app::MaterialTarget>(materialService, platformServices), log) ||
        !registerTarget(rpc, "archive", std::make_shared<fo4ide::app::ArchiveTarget>(archiveService, platformServices), log) ||
        !registerTarget(rpc, "backend", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "backend"), log) ||
        !registerTarget(rpc, "masters", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "masters"), log) ||
        !registerTarget(rpc, "cell", std::make_shared<fo4ide::app::CellTarget>(plugins, nifService, textureResolver), log) ||
        !registerTarget(rpc, "strings", std::make_shared<fo4ide::app::PluginBackendTarget>(plugins, "strings"), log) ||
        !registerTarget(rpc, "graph", std::make_shared<fo4ide::app::GraphTarget>(platformServices, toolLocator), log) ||
        !registerTarget(rpc, "swf", swfTarget, log)) {
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
    std::string mo2Instance;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--health") printHealth = true;
        if (arg == "--headless" || arg == "--serve") runServer = true;
        if (arg == "--browser") { runServer = true; openBrowser = true; }
        if (arg == "--window") { runServer = true; openWindow = true; }
        if (arg == "--mcp") runMcp = true;
        if (arg == "--mo2") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--")) {
                std::cerr << "--mo2 requires an instance folder\n";
                fo4ide::logging::shutdown();
                return 6;
            }
            mo2Instance = argv[++i];
            if (mo2Instance.empty()) {
                std::cerr << "--mo2 requires a nonempty instance folder\n";
                fo4ide::logging::shutdown();
                return 6;
            }
        }
        if (arg == "--port") {
            const auto port = i + 1 < argc ? fo4ide::http::parsePort(argv[i + 1]) : std::nullopt;
            if (!port) {
                std::cerr << "--port requires a whole number from 0 to 65535\n";
                fo4ide::logging::shutdown();
                return 6;
            }
            requestedPort = *port;
            ++i;
        }
    }

    if (!mo2Instance.empty()) {
        if (runMcp) {
            std::error_code ec;
            if (!std::filesystem::is_directory(mo2Instance, ec)) {
                std::cerr << "MO2 load failed: instance folder not found: " << mo2Instance << '\n';
                fo4ide::logging::shutdown();
                return 6;
            }
            mcpRegistry.setBeforeFirstCall([&rpc, mo2Instance]() -> std::optional<std::string> {
                const auto loaded = rpc.invoke({"appInterop", "OpenMo2Profile", nlohmann::json::array({mo2Instance})});
                if (loaded.ok) return std::nullopt;
                std::cerr << "MO2 load failed: " << loaded.error << '\n';
                return "MO2 load failed: " + loaded.error;
            });
        } else {
            const auto loaded = rpc.invoke({"appInterop", "OpenMo2Profile", nlohmann::json::array({mo2Instance})});
            if (!loaded.ok) {
                std::cerr << "MO2 load failed: " << loaded.error << '\n';
                fo4ide::logging::shutdown();
                return 6;
            }
        }
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
        std::cout << response.toJson().dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
    }

#if defined(FO4IDE_HAS_HTTP)
    if (runServer) {
        const auto webRoot = webRootFromArgs(argc, argv);
        if (webRoot.empty()) {
            std::cerr << "web/dist was not found. Build it with: cd web && npm ci && npm run build\n";
            fo4ide::logging::shutdown();
            return 2;
        }

        const auto token = newAccessToken();
        fo4ide::http::HttpServer server(rpc, events);
        auto started = server.start({
            .host = "127.0.0.1",
            .port = requestedPort,
            .webRoot = webRoot,
            .token = token,
        });
        if (!started) {
            std::cerr << "HTTP server failed: " << started.error().message << '\n';
            fo4ide::logging::shutdown();
            return 3;
        }

        const auto baseUrl = "http://127.0.0.1:" + std::to_string(*started) + "/#/main";
        const auto url = baseUrl + "?t=" + token;
        std::cout << "FO4IDE native server: " << url << '\n';
        log->info("HTTP server listening at {}", baseUrl);

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
