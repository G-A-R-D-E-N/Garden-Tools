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

#include <nlohmann/json.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/events/EventBus.hpp>
#if defined(FO4IDE_HAS_HTTP)
#include <fo4ide/http/HttpServer.hpp>
#endif
#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

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
    AppTarget(fo4ide::plugin::IPluginBackend& plugins, std::filesystem::path nifTool)
        : plugins_(plugins), nifTool_(std::move(nifTool)) {}

    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                           const nlohmann::json&) override {
        if (method == "GetHealth") {
            const auto capabilities = plugins_.capabilities();
            return nlohmann::json{
                {"ok", true},
                {"native", true},
                {"pluginBackend", capabilities.available},
                {"nifTool", !nifTool_.empty()}
            };
        }
        return std::unexpected(fo4ide::Error{"UnknownMethod", "Unknown app RPC method"});
    }

private:
    fo4ide::plugin::IPluginBackend& plugins_;
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

}

int main(int argc, char** argv) {
    const auto configDir = configDirectory();
    auto logInit = fo4ide::logging::initialize({.directory = configDir / "logs"});
    if (!logInit) {
        std::cerr << "Logging initialization failed: " << logInit.error().message << '\n';
        return 1;
    }

    auto log = fo4ide::logging::get("app");
    log->info("FO4IDE native Milestone 1 foundation starting");

    fo4ide::settings::Store settingsStore(configDir / "settings.json");
    fo4ide::settings::Settings currentSettings;
    if (auto loaded = settingsStore.load()) {
        currentSettings = std::move(*loaded);
    } else {
        log->warn("Settings load failed: {}", loaded.error().message);
    }

    fo4ide::process::ReprocRunner processRunner;
    fo4ide::platform::NativeServices platformServices(processRunner);
    const auto nifTool = fo4ide::app::locateNifTool(currentSettings, argv[0]);
    fo4ide::assets::NifService nifService(processRunner, nifTool);
    fo4ide::events::EventBus events;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher rpc;

    if (!registerTarget(rpc, "app", std::make_shared<AppTarget>(plugins, nifTool), log) ||
        !registerTarget(rpc, "settings", std::make_shared<fo4ide::app::SettingsTarget>(settingsStore, currentSettings, platformServices, processRunner), log) ||
        !registerTarget(rpc, "nif", std::make_shared<fo4ide::app::NifTarget>(nifService, platformServices), log)) {
        fo4ide::logging::shutdown();
        return 1;
    }

    bool printHealth = false;
    bool runServer = false;
    bool openBrowser = false;
    int requestedPort = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--health") printHealth = true;
        if (arg == "--headless" || arg == "--serve" || arg == "--browser") runServer = true;
        if (arg == "--browser") openBrowser = true;
        if (arg == "--port" && i + 1 < argc) requestedPort = std::stoi(argv[++i]);
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
        if (openBrowser) {
            auto opened = platformServices.openExternal(url);
            if (!opened) log->warn("Could not open browser: {}", opened.error().message);
        }

        std::signal(SIGINT, requestStop);
        std::signal(SIGTERM, requestStop);
        while (!stopRequested.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
    } else if (!printHealth) {
        std::cout << "FO4IDE native Milestone 1 foundation\n"
                     "Use --headless to serve the existing React UI, --browser to open it, or --health for a quick check.\n";
    }
#else
    if (runServer) {
        std::cerr << "This build was configured with FO4IDE_BUILD_HTTP=OFF.\n";
        fo4ide::logging::shutdown();
        return 4;
    }
#endif

    events.shutdown();
    log->info("FO4IDE native foundation stopped");
    fo4ide::logging::shutdown();
    return 0;
}
