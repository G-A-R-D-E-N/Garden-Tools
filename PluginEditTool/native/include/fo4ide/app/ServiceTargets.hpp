#pragma once

#include <string_view>

#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

namespace fo4ide::app {

class SettingsTarget final : public rpc::Target {
public:
    SettingsTarget(settings::Store& store,
                   settings::Settings& current,
                   platform::Services& platform,
                   process::Runner& runner);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> getSettings() const;
    Result<nlohmann::json> saveSettings(const nlohmann::json& args);
    Result<nlohmann::json> browseFolder(const nlohmann::json& args);
    Result<nlohmann::json> browseFile(const nlohmann::json& args);
    Result<nlohmann::json> testClaude(const nlohmann::json& args);

    settings::Store& store_;
    settings::Settings& current_;
    platform::Services& platform_;
    process::Runner& runner_;
};

class NifTarget final : public rpc::Target {
public:
    NifTarget(assets::NifService& service,
              assets::TextureResolver& textures,
              platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> stageDroppedFile(const nlohmann::json& args) const;

    assets::NifService& service_;
    assets::TextureResolver& textures_;
    platform::Services& platform_;
};

// Port of the C# PapyrusInterop + PapyrusService surface: compile (built-in
// engine, with the Creation Kit process path when a compiler is configured),
// decompile (file or folder), the analysis JSON entry points, script file
// read/write, and platform plumbing. The CK wiki lookups (LookupFunction /
// LookupScriptInfo) return the C# "not configured" message because the native
// host has no offline wiki mirror feature yet.
class PapyrusTarget final : public rpc::Target {
public:
    PapyrusTarget(process::Runner& runner,
                  tools::Locator& locator,
                  platform::Services& platform);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> compile(const nlohmann::json& args);
    Result<nlohmann::json> decompile(const nlohmann::json& args);
    Result<nlohmann::json> stageDroppedFile(const nlohmann::json& args) const;

    process::Runner& runner_;
    tools::Locator& locator_;
    platform::Services& platform_;
};

}
