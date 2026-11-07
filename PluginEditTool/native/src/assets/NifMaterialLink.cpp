#include <fo4ide/assets/NifMaterialLink.hpp>

#include <fo4/error.hpp>
#include <fo4/material/material_file.hpp>
#include <fo4/nif/material_link.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4ide/assets/TextureResolver.hpp>
#include <fo4ide/material/MaterialService.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fo4ide::assets {
namespace {

struct LinkedShape {
    fo4::nif::NifFile nif;
    std::uint32_t block{0};
    std::string material;
    std::optional<fo4::material::MaterialFile> file;
};

std::string dumpJson(const nlohmann::json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

Result<void> requireNif(const std::filesystem::path& path) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", "NIF path is empty"});
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", "NIF not found: " + path.string()});
    }
    return {};
}

fo4::nif::NifFile readNif(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

std::optional<fo4::material::MaterialFile> loadMaterial(TextureResolver& textures,
                                                        const std::filesystem::path& nifPath,
                                                        std::string_view material,
                                                        std::string_view textureRoot) {
    if (material.empty()) return std::nullopt;
    const auto path = textures.resolveMaterialFile(nifPath.string(), fo4::nif::data_relative_material_path(material), textureRoot);
    if (!path) return std::nullopt;
    auto loaded = fo4ide::material::MaterialService{}.load(*path);
    if (!loaded) return std::nullopt;
    return std::move(*loaded);
}

Result<LinkedShape> linkShape(TextureResolver& textures,
                              const std::filesystem::path& nifPath,
                              int shape,
                              std::string_view textureRoot) {
    if (auto valid = requireNif(nifPath); !valid) return std::unexpected(valid.error());
    if (shape < 0) return std::unexpected(Error{"InvalidShape", "Shape block must be zero or more"});
    auto nif = readNif(nifPath);
    const auto block = static_cast<std::uint32_t>(shape);
    const auto shapes = fo4::nif::read_shapes(nif);
    const auto found = std::find_if(shapes.begin(), shapes.end(),
                                    [block](const fo4::nif::Shape& candidate) { return candidate.block == block; });
    if (found == shapes.end()) {
        return std::unexpected(Error{"ShapeNotFound", "block " + std::to_string(shape) + " is not a shape"});
    }
    LinkedShape linked{std::move(nif), block, found->material, std::nullopt};
    linked.file = loadMaterial(textures, nifPath, linked.material, textureRoot);
    return linked;
}

}  // namespace

NifMaterialLink::NifMaterialLink(TextureResolver& textures) : textures_(textures) {}

Result<std::string> NifMaterialLink::compare(const std::filesystem::path& nifPath,
                                             int shape,
                                             std::string_view textureRoot) const {
    try {
        auto linked = linkShape(textures_, nifPath, shape, textureRoot);
        if (!linked) return std::unexpected(linked.error());
        nlohmann::json report{{"shape", shape},
                              {"material", linked->material},
                              {"found", linked->file.has_value()},
                              {"format", ""},
                              {"mismatches", nlohmann::json::array()}};
        if (!linked->file) return dumpJson(report);
        report["format"] = linked->file->format == fo4::material::MaterialFormat::bgem ? "BGEM" : "BGSM";
        for (const auto& mismatch : fo4::nif::compare_shape_material(linked->nif, linked->block, *linked->file)) {
            report["mismatches"].push_back(
                {{"field", mismatch.field}, {"nif", mismatch.nif_value}, {"material", mismatch.material_value}});
        }
        return dumpJson(report);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifMaterialLinkFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifMaterialLinkFailed", ex.what()});
    }
}

Result<std::string> NifMaterialLink::textureEdits(const std::filesystem::path& nifPath,
                                                  int shape,
                                                  std::string_view textureRoot) const {
    try {
        auto linked = linkShape(textures_, nifPath, shape, textureRoot);
        if (!linked) return std::unexpected(linked.error());
        nlohmann::json edits = nlohmann::json::array();
        if (linked->file) {
            for (const auto& edit : fo4::nif::shape_material_texture_edits(linked->nif, linked->block, *linked->file)) {
                edits.push_back({{"id", edit.id}, {"key", edit.key}, {"value", std::get<std::string>(edit.value)}});
            }
        }
        return dumpJson({{"shape", shape},
                         {"material", linked->material},
                         {"found", linked->file.has_value()},
                         {"edits", edits}});
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifMaterialLinkFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifMaterialLinkFailed", ex.what()});
    }
}

Result<std::string> NifMaterialLink::shapes(const std::filesystem::path& nifPath, std::string_view material) const {
    if (auto valid = requireNif(nifPath); !valid) return std::unexpected(valid.error());
    try {
        const auto nif = readNif(nifPath);
        nlohmann::json blocks = nlohmann::json::array();
        for (const auto block : fo4::nif::shapes_using_material(nif, material)) {
            blocks.push_back({{"block", block}, {"name", fo4::nif::read_shape(nif, block).name}});
        }
        return dumpJson({{"material", std::string(material)}, {"shapes", blocks}});
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifMaterialLinkFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifMaterialLinkFailed", ex.what()});
    }
}

}  // namespace fo4ide::assets
