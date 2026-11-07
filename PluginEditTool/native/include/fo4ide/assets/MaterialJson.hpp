#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <fo4/material/material_file.hpp>

namespace fo4ide::assets {

class TextureResolver;

class BgsmMemo {
public:
    std::shared_ptr<const fo4::material::MaterialFile> load(const std::filesystem::path& path);

private:
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const fo4::material::MaterialFile>> files_;
};

nlohmann::json bgsmMaterialJson(const fo4::material::MaterialFile& file);

nlohmann::json bgsmMaterialWithRoots(
    const fo4::material::MaterialFile& file,
    const std::function<std::optional<fo4::material::MaterialFile>(const std::string&)>& loadRoot);

void attachShapeMaterials(nlohmann::json& geometry,
                          const std::filesystem::path& nifPath,
                          TextureResolver& textures,
                          BgsmMemo& memo);

}  // namespace fo4ide::assets
