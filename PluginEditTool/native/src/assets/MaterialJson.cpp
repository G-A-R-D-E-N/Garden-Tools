#include <fo4ide/assets/MaterialJson.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/material/MaterialService.hpp>

namespace fo4ide::assets {
namespace {

using fo4::material::MaterialFile;

constexpr std::size_t kMaxRootDepth = 4;

std::string textField(const MaterialFile& file, std::string_view name) {
    const auto value = fo4::material::get_field<std::string>(file, name);
    return value ? std::string(fo4::material::text(*value)) : std::string{};
}

bool flagField(const MaterialFile& file, std::string_view name) {
    return fo4::material::get_field<bool>(file, name).value_or(false);
}

float floatField(const MaterialFile& file, std::string_view name) {
    return fo4::material::get_field<float>(file, name).value_or(0.0F);
}

nlohmann::json colourField(const MaterialFile& file, std::string_view name) {
    const auto color = fo4::material::get_field<fo4::material::Color3>(file, name).value_or(
        fo4::material::Color3{});
    return nlohmann::json::array({color.r, color.g, color.b});
}

}  // namespace

nlohmann::json bgsmMaterialJson(const MaterialFile& file) {
    const auto blendMode = fo4::material::get_field<std::uint8_t>(file, "AlphaBlendMode0").value_or(0U);
    const auto alphaRef = fo4::material::get_field<std::uint8_t>(file, "AlphaTestRef").value_or(0U);
    return nlohmann::json{
        {"diffuse", textField(file, "DiffuseTexture")},
        {"normal", textField(file, "NormalTexture")},
        {"smoothSpec", textField(file, "SmoothSpecTexture")},
        {"glow", textField(file, "GlowTexture")},
        {"rootMaterialPath", textField(file, "RootMaterialPath")},
        {"alphaBlend", blendMode != 0U},
        {"alphaTest", flagField(file, "AlphaTest")},
        {"alphaTestRef", alphaRef},
        {"alpha", floatField(file, "Alpha")},
        {"twoSided", flagField(file, "TwoSided")},
        {"decal", flagField(file, "Decal")},
        {"emitEnabled", flagField(file, "EmitEnabled")},
        {"emittanceColor", colourField(file, "EmittanceColor")},
        {"emittanceMult", floatField(file, "EmittanceMult")},
        {"specularEnabled", flagField(file, "SpecularEnabled")},
        {"specularColor", colourField(file, "SpecularColor")},
        {"specularMult", floatField(file, "SpecularMult")},
        {"smoothness", floatField(file, "Smoothness")},
        {"pbr", flagField(file, "Pbr")},
        {"format", "BGSM"},
    };
}

nlohmann::json bgsmMaterialWithRoots(
    const MaterialFile& file,
    const std::function<std::optional<MaterialFile>(const std::string&)>& loadRoot) {
    auto out = bgsmMaterialJson(file);
    std::string root = out["rootMaterialPath"].get<std::string>();
    for (std::size_t depth = 0; depth < kMaxRootDepth && !root.empty(); ++depth) {
        const auto rootFile = loadRoot(root);
        if (!rootFile) break;
        const auto rootJson = bgsmMaterialJson(*rootFile);
        for (const char* key : {"diffuse", "normal", "smoothSpec", "glow"}) {
            if (out[key].get<std::string>().empty()) out[key] = rootJson[key];
        }
        root = rootJson["rootMaterialPath"].get<std::string>();
    }
    return out;
}

std::shared_ptr<const MaterialFile> BgsmMemo::load(const std::filesystem::path& path) {
    const auto key = path.string();
    std::scoped_lock lock(mutex_);
    if (const auto found = files_.find(key); found != files_.end()) return found->second;
    std::shared_ptr<const MaterialFile> file;
    const material::MaterialService service;
    if (auto loaded = service.load(path)) file = std::make_shared<const MaterialFile>(std::move(*loaded));
    files_.emplace(key, file);
    return file;
}

void attachShapeMaterials(nlohmann::json& geometry,
                          const std::filesystem::path& nifPath,
                          TextureResolver& textures,
                          BgsmMemo& memo) {
    if (!geometry.is_object() || !geometry.contains("shapes") || !geometry["shapes"].is_array()) return;
    const auto nifText = nifPath.string();
    const auto loadRoot = [&](const std::string& rel) -> std::optional<MaterialFile> {
        const auto path = textures.resolveMaterialPath(nifText, rel, "");
        if (!path) return std::nullopt;
        const auto file = memo.load(*path);
        if (!file) return std::nullopt;
        return *file;
    };
    for (auto& shape : geometry["shapes"]) {
        if (!shape.is_object() || !shape.contains("bgsmPath") || !shape["bgsmPath"].is_string()) continue;
        const auto rel = shape["bgsmPath"].get<std::string>();
        const auto path = textures.resolveMaterialPath(nifText, rel, "");
        if (!path) continue;
        const auto file = memo.load(*path);
        if (!file) continue;
        shape["material"] = bgsmMaterialWithRoots(*file, loadRoot);
    }
}

}  // namespace fo4ide::assets
