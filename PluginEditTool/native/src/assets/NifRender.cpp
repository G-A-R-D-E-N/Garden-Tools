#include <fo4ide/assets/NifRender.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fo4/error.hpp>
#include <fo4/nif/describe.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>

#include <nlohmann/json.hpp>

namespace fo4ide::assets {
namespace {

fo4::nif::NifFile loadNif(const std::filesystem::path& nifPath) {
    std::ifstream stream(nifPath, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open NIF: " + nifPath.string());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

// Constraint: a NIF string is raw bytes, so invalid UTF-8 must be replaced rather than thrown from dump().
std::string dumpJson(const nlohmann::json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

nlohmann::json textureRefs(const std::vector<fo4::nif::TextureSlot>& slots) {
    auto refs = nlohmann::json::array();
    for (const auto& slot : slots) refs.push_back({{"slot", slot.slot}, {"path", slot.path}});
    return refs;
}

nlohmann::json treeValue(const fo4::nif::TreeValue& value) {
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    if (const auto* number = std::get_if<float>(&value)) return *number;
    if (const auto* numbers = std::get_if<std::vector<float>>(&value)) return *numbers;
    if (const auto* flag = std::get_if<bool>(&value)) return *flag;
    auto flags = nlohmann::json::array();
    if (const auto* toggles = std::get_if<std::vector<fo4::nif::TreeFlag>>(&value)) {
        for (const auto& toggle : *toggles) {
            flags.push_back({{"key", toggle.key}, {"label", toggle.label}, {"on", toggle.on}});
        }
    }
    return flags;
}

nlohmann::json treeField(const fo4::nif::TreeField& field) {
    return {{"key", field.key}, {"label", field.label}, {"type", field.type}, {"value", treeValue(field.value)}};
}

std::string verifyText(const fo4::nif::VerifyReport& report) {
    std::string out = "RESULT: " + std::to_string(report.checks.size()) + " checks, " +
        std::to_string(report.failed()) + " failed";
    for (const auto& check : report.checks) {
        out += check.failure.empty() ? "\n[ok] " + check.name : "\n[FAIL] " + check.name + ": " + check.failure;
    }
    return out;
}

Result<fo4::nif::Edit> toEdit(const nlohmann::json& raw) {
    if (!raw.is_object() || !raw.contains("id") || !raw.contains("key") || !raw.contains("value")) {
        return std::unexpected(Error{"NifEditFailed", "NIF edit needs id, key and value"});
    }
    const auto& rawId = raw.at("id");
    const auto& rawKey = raw.at("key");
    const auto& rawValue = raw.at("value");
    if (!rawId.is_number_integer() || rawId.get<std::int64_t>() < 0 ||
        rawId.get<std::int64_t>() > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) ||
        !rawKey.is_string()) {
        return std::unexpected(Error{"NifEditFailed", "NIF edit needs an unsigned block id and a text key"});
    }
    const auto id = static_cast<std::uint32_t>(rawId.get<std::int64_t>());
    const auto key = rawKey.get<std::string>();
    if (rawValue.is_string()) return fo4::nif::Edit{id, key, rawValue.get<std::string>()};
    if (rawValue.is_number()) return fo4::nif::Edit{id, key, rawValue.get<float>()};
    if (rawValue.is_array() && std::all_of(rawValue.begin(), rawValue.end(), [](const nlohmann::json& item) { return item.is_number(); })) {
        return fo4::nif::Edit{id, key, rawValue.get<std::vector<float>>()};
    }
    return std::unexpected(Error{"NifEditFailed", "key '" + key +
        "' has a value the in-process NIF writer cannot set (text, numbers and number lists only)"});
}

}  // namespace

Result<std::string> renderGeometry(const std::filesystem::path& nifPath) {
    try {
        const auto report = fo4::nif::geometry_nif(loadNif(nifPath));
        nlohmann::json out{{"fo4", report.fo4}, {"shapes", nlohmann::json::array()}};
        for (const auto& shape : report.shapes) {
            nlohmann::json entry{
                {"name", shape.name},
                {"verts", shape.verts},
                {"tris", shape.tris},
                {"normals", shape.normals},
                {"uvs", shape.uvs},
                {"textures", textureRefs(shape.textures)},
                {"skinned", shape.skinned},
            };
            if (shape.billboard) {
                entry["billboard"] = true;
                entry["billboardMode"] = shape.billboard_mode;
            }
            if (entry["textures"].empty() && fo4::nif::is_bgsm_path(shape.material)) entry["bgsmPath"] = shape.material;
            out["shapes"].push_back(std::move(entry));
        }
        return dumpJson(out);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> renderInspect(const std::filesystem::path& nifPath) {
    try {
        const auto report = fo4::nif::inspect_nif(loadNif(nifPath));
        nlohmann::json out{
            {"fo4", report.fo4},
            {"hasBSX", report.has_bsx},
            {"hasCollision", report.has_collision},
            {"shapes", nlohmann::json::array()},
        };
        for (const auto& shape : report.shapes) {
            out["shapes"].push_back({
                {"name", shape.name},
                {"verts", shape.verts},
                {"tris", shape.tris},
                {"skinned", shape.skinned},
                {"shader", shape.material},
                {"textures", textureRefs(shape.textures)},
                {"tangents", shape.tangents},
            });
        }
        return dumpJson(out);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> renderTree(const std::filesystem::path& nifPath) {
    try {
        const auto tree = fo4::nif::describe_nif(loadNif(nifPath));
        auto blocks = nlohmann::json::array();
        for (const auto& block : tree.blocks) {
            auto fields = nlohmann::json::array();
            for (const auto& field : block.fields) fields.push_back(treeField(field));
            blocks.push_back({{"id", block.id},
                              {"type", block.type},
                              {"name", block.name},
                              {"group", block.group},
                              {"cat", block.cat},
                              {"fields", std::move(fields)}});
        }
        nlohmann::json out{
            {"fo4", tree.fo4},
            {"file", nifPath.filename().string()},
            {"blocks", std::move(blocks)},
        };
        return dumpJson(out);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> renderVerify(const std::filesystem::path& nifPath) {
    try {
        return verifyText(fo4::nif::verify_nif(loadNif(nifPath)));
    } catch (const fo4::parse_error& error) {
        return verifyText(fo4::nif::VerifyReport{{{"header parses", error.parse_message()}}});
    } catch (const std::exception& error) {
        return verifyText(fo4::nif::VerifyReport{{{"header parses", error.what()}}});
    }
}

Result<std::vector<std::byte>> applyEditsInProcess(const std::filesystem::path& nifPath, const nlohmann::json& edits) {
    std::vector<fo4::nif::Edit> converted;
    for (const auto& raw : edits) {
        auto edit = toEdit(raw);
        if (!edit) return std::unexpected(edit.error());
        converted.push_back(std::move(*edit));
    }
    try {
        return fo4::nif::apply_edits(loadNif(nifPath), converted);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

}  // namespace fo4ide::assets
