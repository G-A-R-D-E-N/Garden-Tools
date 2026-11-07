#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/app/PluginBackendTarget.hpp>
#include <fo4ide/assets/NifCollisionService.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/hkx/HkxService.hpp>
#include <fo4ide/platform/PlatformServices.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/process/ProcessRunner.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>
#include <fo4ide/settings/Settings.hpp>
#include <fo4ide/tools/ToolLocator.hpp>

namespace fo4::papyrus {
class ScriptIndex;
}

namespace fo4::graph {
class GraphCompiler;
}

namespace fo4ide::app {

class SettingsTarget final : public rpc::Target {
public:
    SettingsTarget(settings::Store& store,
                   settings::SharedSettings& current,
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
    settings::SharedSettings& shared_;
    platform::Services& platform_;
    process::Runner& runner_;
};

class NifTarget final : public rpc::Target {
public:
    NifTarget(assets::NifService& service,
              assets::TextureResolver& textures,
              platform::Services& platform,
              plugin::IPluginBackend& plugins);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    Result<nlohmann::json> stageDroppedFile(const nlohmann::json& args) const;

    assets::NifService& service_;
    assets::TextureResolver& textures_;
    platform::Services& platform_;
    plugin::IPluginBackend& plugins_;
    assets::NifCollisionService collision_;
};

class HkxTarget final : public rpc::Target {
public:
    explicit HkxTarget(hkx::HkxService& service);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    hkx::HkxService& service_;
};

// Port of CellInterop: the cell 3D-viewport RPC surface. Record queries
// (SearchCells / SearchWorldspaces / GetPlugins / GetPlacedReferences[AtGrid] /
// SetPlacedReferenceTransform) are delegated unchanged to the Radicle plugin
// backend's "cell" facet; the asset ops (GetGeometryBatch /
// GetGeometryBatchProgress / GetTexture) are served here over the shared NIF
// and texture services, which the bare PluginBackendTarget had no handle to.
// Each asset op returns a serialized JSON string, matching the C# CellInterop.
class CellTarget final : public rpc::Target {
public:
    CellTarget(plugin::IPluginBackend& backend,
               assets::NifService& nif,
               assets::TextureResolver& textures);

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    std::string geometryBatch(const std::vector<std::string>& relModelPaths);

    PluginBackendTarget records_;
    assets::NifService& nif_;
    assets::TextureResolver& textures_;
    // {done, total} of the in-flight geometryBatch, polled via
    // GetGeometryBatchProgress while GetGeometryBatch runs.
    std::atomic<int> geoBatchDone_{0};
    std::atomic<int> geoBatchTotal_{0};
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

// Port of GraphInterop: the Blueprint / visual-scripting RPC surface. Wraps the
// graph compiler (core palette, palette search, node signatures, validate,
// compile to source / PEX) and graph/script file IO, all over the Papyrus
// base-import roots. The compiler is cached and rebuilt when the roots change
// or Refresh is called, mirroring the C# GraphInterop.Compiler() cache. Every
// method returns a JSON string (or an "Error: ..." string), never throwing.
class GraphTarget final : public rpc::Target {
public:
    GraphTarget(platform::Services& platform, tools::Locator& locator);
    ~GraphTarget() override;

    Result<nlohmann::json> invoke(std::string_view method,
                                  const nlohmann::json& args) override;

private:
    fo4::graph::GraphCompiler& compiler();

    platform::Services& platform_;
    tools::Locator& locator_;
    std::mutex mutex_;
    std::unique_ptr<fo4::papyrus::ScriptIndex> index_;
    std::unique_ptr<fo4::graph::GraphCompiler> compiler_;
    std::string rootsKey_;
};

}
