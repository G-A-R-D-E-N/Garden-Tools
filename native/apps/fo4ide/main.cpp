#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>

namespace {

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

class AppTarget final : public fo4ide::rpc::Target {
public:
    explicit AppTarget(fo4ide::plugin::IPluginBackend& plugins) : plugins_(plugins) {}

    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                           const nlohmann::json&) override {
        if (method == "GetHealth") {
            const auto capabilities = plugins_.capabilities();
            return nlohmann::json{
                {"ok", true},
                {"native", true},
                {"pluginBackend", capabilities.available}
            };
        }
        return std::unexpected(fo4ide::Error{"UnknownMethod", "Unknown app RPC method"});
    }

private:
    fo4ide::plugin::IPluginBackend& plugins_;
};

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
    auto settings = settingsStore.load();
    if (!settings) log->warn("Settings load failed: {}", settings.error().message);

    fo4ide::events::EventBus events;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher rpc;
    auto appTarget = std::make_shared<AppTarget>(plugins);
    if (auto registered = rpc.registerTarget("app", appTarget); !registered) {
        log->error("RPC setup failed: {}", registered.error().message);
        fo4ide::logging::shutdown();
        return 1;
    }

    bool printHealth = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--health") printHealth = true;
    }

    if (printHealth) {
        const auto response = rpc.invoke({"app", "GetHealth", nlohmann::json::array()});
        std::cout << response.toJson().dump(2) << '\n';
    } else {
        std::cout << "FO4IDE native Milestone 1 foundation\n"
                     "Use --health to verify the composition root. HTTP/UI hosting is the next slice.\n";
    }

    events.shutdown();
    log->info("FO4IDE native foundation stopped");
    fo4ide::logging::shutdown();
    return 0;
}
