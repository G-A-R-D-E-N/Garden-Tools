#pragma once

#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include <fo4/material/material_file.hpp>

namespace fo4ide::assets {

class TextureResolver;

// Thread-safe: each path is parsed once; concurrent callers for the same path share its result and the parse runs outside the lock.
class BgsmMemo {
public:
    std::shared_ptr<const fo4::material::MaterialFile> load(const std::filesystem::path& path);

private:
    std::mutex mutex_;
    std::map<std::string, std::shared_future<std::shared_ptr<const fo4::material::MaterialFile>>> files_;
};

nlohmann::json bgsmMaterialJson(const fo4::material::MaterialFile& file);

nlohmann::json bgemMaterialJson(const fo4::material::MaterialFile& file);

nlohmann::json bgsmMaterialWithRoots(
    const fo4::material::MaterialFile& file,
    const std::function<std::shared_ptr<const fo4::material::MaterialFile>(const std::string&)>& loadRoot);

using TextureExists = std::function<bool(const std::string&)>;

std::string mergeMaterialTexture(const std::string& own, const std::string& nif, const TextureExists& exists);

void mergeMaterialSlots(nlohmann::json& shape, const nlohmann::json& material, const TextureExists& exists);

void attachShapeMaterials(nlohmann::json& geometry,
                          const std::filesystem::path& nifPath,
                          TextureResolver& textures,
                          BgsmMemo& memo);

}  // namespace fo4ide::assets
