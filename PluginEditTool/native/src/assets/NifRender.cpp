#include <fo4ide/assets/NifRender.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/error.hpp>
#include <fo4/nif/nif.hpp>

#include <nlohmann/json.hpp>

namespace fo4ide::assets {
namespace {

fo4::nif::NifFile loadNif(const std::filesystem::path& nifPath) {
    std::ifstream stream(nifPath, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open NIF: " + nifPath.string());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

bool isBgsmPath(std::string path) {
    std::ranges::transform(path, path.begin(),
                           [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    return path.ends_with(".bgsm");
}

bool hasBlockPrefix(const fo4::nif::NifFile& nif, std::string_view prefix) {
    return std::any_of(nif.blocks().begin(), nif.blocks().end(),
                       [prefix](const auto& block) { return block.type.starts_with(prefix); });
}

nlohmann::json textureRefs(const fo4::nif::Shape& shape) {
    auto refs = nlohmann::json::array();
    for (std::size_t slot = 0; slot < shape.textures.size(); ++slot) {
        if (!shape.textures[slot].empty()) refs.push_back({{"slot", slot}, {"path", shape.textures[slot]}});
    }
    return refs;
}

nlohmann::json shapeGeometry(const fo4::nif::NifFile& nif, const fo4::nif::Shape& shape) {
    nlohmann::json out{
        {"name", shape.name},
        {"verts", shape.verts},
        {"tris", shape.tris},
        {"normals", shape.normals},
        {"uvs", shape.uvs},
        {"textures", textureRefs(shape)},
        {"skinned", shape.skin_ref >= 0},
    };
    const auto mode = fo4::nif::billboard_mode(nif, shape.block);
    if (mode >= 0) {
        out["billboard"] = true;
        out["billboardMode"] = mode;
    }
    if (out["textures"].empty() && isBgsmPath(shape.material)) out["bgsmPath"] = shape.material;
    return out;
}

nlohmann::json shapeInspect(const fo4::nif::Shape& shape) {
    return {
        {"name", shape.name},
        {"verts", shape.verts.size() / 3},
        {"tris", shape.tris.size() / 3},
        {"skinned", shape.skin_ref >= 0},
        {"shader", shape.material},
        {"textures", textureRefs(shape)},
        {"tangents", !shape.tangents.empty()},
    };
}

}

Result<std::string> renderGeometry(const std::filesystem::path& nifPath) {
    try {
        const auto nif = loadNif(nifPath);
        nlohmann::json out{{"fo4", nif.bs_version() == 130U}, {"shapes", nlohmann::json::array()}};
        for (const auto& shape : fo4::nif::read_shapes(nif)) out["shapes"].push_back(shapeGeometry(nif, shape));
        return out.dump();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> renderInspect(const std::filesystem::path& nifPath) {
    try {
        const auto nif = loadNif(nifPath);
        nlohmann::json out{
            {"fo4", nif.bs_version() == 130U},
            {"hasBSX", hasBlockPrefix(nif, "BSXFlags")},
            {"hasCollision", hasBlockPrefix(nif, "bhk")},
            {"shapes", nlohmann::json::array()},
        };
        for (const auto& shape : fo4::nif::read_shapes(nif)) out["shapes"].push_back(shapeInspect(shape));
        return out.dump();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

}
