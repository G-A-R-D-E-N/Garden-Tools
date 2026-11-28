#include <fo4ide/assets/MaterialJson.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/material/MaterialService.hpp>

namespace fo4ide::assets {
namespace {

using fo4::material::MaterialFile;

constexpr std::size_t kMaxRootDepth = 4;
// Invariant: TileFlags bit 0 is TileV and bit 1 is TileU (Radicle/docs/MATERIALS.md); a material without the field tiles both axes.
constexpr std::uint32_t kTileV = 1U;
constexpr std::uint32_t kTileU = 2U;
constexpr std::uint32_t kTileBoth = kTileU | kTileV;

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

std::string comparableTexture(std::string_view path) {
    std::string out(path);
    for (auto& ch : out) {
        ch = ch == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::optional<std::size_t> slotIndex(const nlohmann::json& textures, int slot) {
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].value("slot", -1) == slot) return i;
    }
    return std::nullopt;
}

std::string slotPath(const nlohmann::json& textures, int slot) {
    const auto index = slotIndex(textures, slot);
    if (!index) return {};
    return textures[*index].value("path", std::string{});
}

void setSlotPath(nlohmann::json& textures, int slot, const std::string& path) {
    const auto index = slotIndex(textures, slot);
    if (path.empty()) {
        if (index) textures.erase(*index);
        return;
    }
    if (index) {
        textures[*index]["path"] = path;
        return;
    }
    textures.push_back({{"slot", slot}, {"path", path}});
}

nlohmann::json commonMaterialJson(const MaterialFile& file) {
    const auto blendMode = fo4::material::get_field<std::uint8_t>(file, "AlphaBlendMode0").value_or(0U);
    const auto alphaRef = fo4::material::get_field<std::uint8_t>(file, "AlphaTestRef").value_or(0U);
    const auto tileFlags = fo4::material::get_field<std::uint32_t>(file, "TileFlags").value_or(kTileBoth);
    return nlohmann::json{
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
        {"tileU", (tileFlags & kTileU) != 0U},
        {"tileV", (tileFlags & kTileV) != 0U},
    };
}

}  // namespace

nlohmann::json bgsmMaterialJson(const MaterialFile& file) {
    auto out = commonMaterialJson(file);
    out["diffuse"] = textField(file, "DiffuseTexture");
    out["normal"] = textField(file, "NormalTexture");
    out["smoothSpec"] = textField(file, "SmoothSpecTexture");
    out["glow"] = textField(file, "GlowTexture");
    out["rootMaterialPath"] = textField(file, "RootMaterialPath");
    out["format"] = "BGSM";
    return out;
}

nlohmann::json bgemMaterialJson(const MaterialFile& file) {
    auto out = commonMaterialJson(file);
    out["diffuse"] = textField(file, "BaseTexture");
    out["normal"] = textField(file, "NormalTexture");
    out["smoothSpec"] = std::string{};
    out["glow"] = textField(file, "GlowTexture");
    out["rootMaterialPath"] = std::string{};
    out["format"] = "BGEM";
    return out;
}

nlohmann::json bgsmMaterialWithRoots(
    const MaterialFile& file,
    const std::function<std::shared_ptr<const MaterialFile>(const std::string&)>& loadRoot) {
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

std::string mergeMaterialTexture(const std::string& own, const std::string& nif, const TextureExists& exists) {
    if (own.empty()) return nif;
    if (nif.empty()) return own;
    if (comparableTexture(own) == comparableTexture(nif)) return nif;
    return exists(own) || !exists(nif) ? own : nif;
}

void mergeMaterialSlots(nlohmann::json& shape, const nlohmann::json& material, const TextureExists& exists) {
    if (!shape.is_object() || !material.is_object()) return;
    if (!shape.contains("textures") || !shape["textures"].is_array()) shape["textures"] = nlohmann::json::array();
    auto& textures = shape["textures"];
    const std::pair<int, const char*> slots[] = {{0, "diffuse"}, {1, "normal"}};
    for (const auto& [slot, key] : slots) {
        const auto nifPath = slotPath(textures, slot);
        const auto merged = mergeMaterialTexture(material.value(key, std::string{}), nifPath, exists);
        if (merged != nifPath) setSlotPath(textures, slot, merged);
    }
}

std::shared_ptr<const MaterialFile> BgsmMemo::load(const std::filesystem::path& path) {
    // Invariant: the key carries the file's size and write time, so an edited material is parsed again.
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto written = std::filesystem::last_write_time(path, ec);
    const auto key = path.string() + '|' + std::to_string(ec ? 0 : size) + '|' +
                     std::to_string(written.time_since_epoch().count());
    std::promise<std::shared_ptr<const MaterialFile>> promise;
    std::shared_future<std::shared_ptr<const MaterialFile>> pending;
    bool owner = false;
    {
        std::scoped_lock lock(mutex_);
        if (const auto found = files_.find(key); found != files_.end()) {
            pending = found->second;
        } else {
            pending = promise.get_future().share();
            files_.emplace(key, pending);
            owner = true;
        }
    }
    if (owner) {
        try {
            std::shared_ptr<const MaterialFile> file;
            const material::MaterialService service;
            if (auto loaded = service.load(path)) file = std::make_shared<const MaterialFile>(std::move(*loaded));
            promise.set_value(std::move(file));
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    }
    return pending.get();
}

void attachShapeMaterials(nlohmann::json& geometry,
                          const std::filesystem::path& nifPath,
                          TextureResolver& textures,
                          BgsmMemo& memo) {
    if (!geometry.is_object() || !geometry.contains("shapes") || !geometry["shapes"].is_array()) return;
    const auto nifText = nifPath.string();
    const auto loadRoot = [&](const std::string& rel) -> std::shared_ptr<const MaterialFile> {
        const auto path = textures.resolveMaterialPath(nifText, rel, "");
        if (!path) return nullptr;
        return memo.load(*path);
    };
    const TextureExists exists = [&](const std::string& rel) {
        return textures.resolveTextureFile(nifText, rel, "").has_value();
    };
    for (auto& shape : geometry["shapes"]) {
        if (!shape.is_object() || !shape.contains("bgsmPath") || !shape["bgsmPath"].is_string()) continue;
        // Constraint: a material that cannot be read leaves the shape on its NIF textures; it must not fail the whole model.
        try {
            const auto rel = shape["bgsmPath"].get<std::string>();
            const auto path = textures.resolveMaterialPath(nifText, rel, "");
            if (!path) continue;
            const auto file = memo.load(*path);
            if (!file) continue;
            auto material = file->format == fo4::material::MaterialFormat::bgem
                ? bgemMaterialJson(*file)
                : bgsmMaterialWithRoots(*file, loadRoot);
            mergeMaterialSlots(shape, material, exists);
            shape["material"] = std::move(material);
        } catch (const std::exception&) {
            shape.erase("material");
        }
    }
}

}  // namespace fo4ide::assets
