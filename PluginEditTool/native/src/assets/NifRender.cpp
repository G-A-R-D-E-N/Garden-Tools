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

nlohmann::json treeField(std::string key, std::string label, std::string type, nlohmann::json value) {
    return {{"key", std::move(key)}, {"label", std::move(label)}, {"type", std::move(type)}, {"value", std::move(value)}};
}

nlohmann::json treeBlock(std::uint32_t id, const std::string& type, const std::string& name, const char* group,
                         const char* cat, nlohmann::json fields) {
    return {{"id", id}, {"type", type}, {"name", name}, {"group", group}, {"cat", cat}, {"fields", std::move(fields)}};
}

nlohmann::json vec3(const float* values) {
    return nlohmann::json::array({values[0], values[1], values[2]});
}

nlohmann::json nodeTreeBlock(const fo4::nif::Node& node) {
    return treeBlock(node.block, node.type, node.name, "Nodes", "node", nlohmann::json::array({
        treeField("name", "Name", "string", node.name),
        treeField("translation", "Translation", "vec3", vec3(node.local.t)),
        treeField("scale", "Scale", "float", node.local.s),
    }));
}

nlohmann::json shapeTreeBlock(const fo4::nif::NifFile& nif, const fo4::nif::Shape& shape) {
    const auto transform = fo4::nif::block_transform(nif, shape.block);
    const auto shaderType = shape.shader_ref >= 0
        ? nif.blocks().at(static_cast<std::size_t>(shape.shader_ref)).type
        : std::string{};
    auto fields = nlohmann::json::array({
        treeField("name", "Name", "string", shape.name),
        treeField("translation", "Translation", "vec3", vec3(transform.t)),
        treeField("verts", "Vertices", "readonly", shape.verts.size() / 3),
        treeField("tris", "Triangles", "readonly", shape.tris.size() / 3),
        treeField("shaderType", "Shader", "readonly", shaderType),
        treeField("material", "Material (.bgsm)", "string", shape.material),
    });
    for (std::size_t slot = 0; slot < 8; ++slot) {
        fields.push_back(treeField("tex" + std::to_string(slot), "Texture " + std::to_string(slot), "tex",
                                   slot < shape.textures.size() ? shape.textures[slot] : std::string{}));
    }
    return treeBlock(shape.block, nif.blocks().at(shape.block).type, shape.name, "Shapes", "shape", std::move(fields));
}

struct VerifyCheck {
    std::string name;
    std::string failure;
};

std::string texturePathFault(const std::string& path) {
    if (path.size() >= 2 && path[1] == ':') return "is a drive-letter path";
    if (!path.empty() && (path.front() == '/' || path.front() == '\\')) return "is an absolute path";
    return {};
}

template <typename Probe>
VerifyCheck shapeCheck(std::string name, const std::vector<fo4::nif::Shape>& shapes, Probe probe) {
    for (const auto& shape : shapes) {
        auto fault = probe(shape);
        if (!fault.empty()) {
            return {std::move(name), "shape '" + shape.name + "' (block " + std::to_string(shape.block) + ") " + fault};
        }
    }
    return {std::move(name), {}};
}

std::vector<VerifyCheck> verifyChecks(const fo4::nif::NifFile& nif) {
    std::vector<VerifyCheck> checks{VerifyCheck{"header parses", {}}};

    std::string link;
    for (const auto& node : nif.nodes()) {
        for (const auto child : node.children) {
            if (!link.empty() || nif.parent(child) == static_cast<std::int32_t>(node.block)) continue;
            link = "block " + std::to_string(child) + " is listed under node " + std::to_string(node.block) +
                   " but its parent is " + std::to_string(nif.parent(child));
        }
    }
    checks.push_back({"each block is listed under one parent only", link});

    std::vector<fo4::nif::Shape> shapes;
    try {
        shapes = fo4::nif::read_shapes(nif);
    } catch (const fo4::parse_error& error) {
        checks.push_back({"shapes parse", error.parse_message()});
        return checks;
    }
    checks.push_back({"shapes parse", {}});
    checks.push_back(shapeCheck("shapes have vertices and triangles", shapes, [](const fo4::nif::Shape& shape) -> std::string {
        if (shape.verts.empty() || shape.tris.empty()) return "has no vertices or triangles";
        return {};
    }));
    checks.push_back(shapeCheck("normals match the vertex count", shapes, [](const fo4::nif::Shape& shape) -> std::string {
        if (!shape.normals.empty() && shape.normals.size() != shape.verts.size()) {
            return "has normals that do not match its vertex count";
        }
        return {};
    }));
    checks.push_back(shapeCheck("UVs match the vertex count", shapes, [](const fo4::nif::Shape& shape) -> std::string {
        if (!shape.uvs.empty() && shape.uvs.size() != shape.verts.size() / 3 * 2) {
            return "has UVs that do not match its vertex count";
        }
        return {};
    }));
    checks.push_back(shapeCheck("shader and skin reads are clean", shapes, [](const fo4::nif::Shape& shape) -> std::string {
        if (!shape.warning.empty()) return "reported: " + shape.warning;
        return {};
    }));
    checks.push_back(shapeCheck("texture paths are relative", shapes, [](const fo4::nif::Shape& shape) -> std::string {
        for (const auto& path : shape.textures) {
            if (auto fault = texturePathFault(path); !fault.empty()) return "texture '" + path + "' " + fault;
        }
        return {};
    }));
    return checks;
}

std::string verifyReport(const std::vector<VerifyCheck>& checks) {
    const auto failed = std::ranges::count_if(checks, [](const VerifyCheck& check) { return !check.failure.empty(); });
    std::string out = "RESULT: " + std::to_string(checks.size()) + " checks, " + std::to_string(failed) + " failed";
    for (const auto& check : checks) {
        out += check.failure.empty() ? "\n[ok] " + check.name : "\n[FAIL] " + check.name + ": " + check.failure;
    }
    return out;
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

Result<std::string> renderTree(const std::filesystem::path& nifPath) {
    try {
        const auto nif = loadNif(nifPath);
        auto blocks = nlohmann::json::array();
        for (const auto& node : nif.nodes()) blocks.push_back(nodeTreeBlock(node));
        for (const auto& shape : fo4::nif::read_shapes(nif)) blocks.push_back(shapeTreeBlock(nif, shape));
        nlohmann::json out{
            {"fo4", nif.bs_version() == 130U},
            {"file", nifPath.filename().string()},
            {"blocks", std::move(blocks)},
        };
        return out.dump();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> renderVerify(const std::filesystem::path& nifPath) {
    try {
        return verifyReport(verifyChecks(loadNif(nifPath)));
    } catch (const fo4::parse_error& error) {
        return verifyReport({VerifyCheck{"header parses", error.parse_message()}});
    } catch (const std::exception& error) {
        return verifyReport({VerifyCheck{"header parses", error.what()}});
    }
}

}
