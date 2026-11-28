#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/layout/LayoutMath.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace fo4ide::app {

using GeometryProvider = std::function<Result<nlohmann::json>(const std::string& modelPath)>;

GeometryProvider nifGeometryProvider(assets::NifService& nif, assets::TextureResolver& textures);

struct BaseModelRow {
    std::string key;
    std::string editorId;
    std::string modelPath;
};

class LayoutTarget final : public rpc::Target {
public:
    LayoutTarget(plugin::IPluginBackend& backend, GeometryProvider geometry);

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    Result<nlohmann::json> getModelBounds(const nlohmann::json& args);
    Result<nlohmann::json> describeKit(const nlohmann::json& args);
    Result<nlohmann::json> snapTransform(const nlohmann::json& args);
    Result<nlohmann::json> checkOverlaps(const nlohmann::json& args);
    Result<nlohmann::json> buildRoom(const nlohmann::json& args);
    Result<nlohmann::json> alignReferences(const nlohmann::json& args);

    Result<layout::Bounds> localBounds(const std::string& modelPath);
    Result<std::vector<BaseModelRow>> baseRows(const nlohmann::json& filter);

    plugin::IPluginBackend& backend_;
    GeometryProvider geometry_;
    std::mutex mutex_;
    std::map<std::string, layout::Bounds> boundsCache_;
};

}
