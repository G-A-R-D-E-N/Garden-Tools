#include <fo4ide/assets/NifRender.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/error.hpp>
#include <fo4/nif/describe.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/nif/shader.hpp>
#include <fo4/nif/sidecars.hpp>

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

nlohmann::json effectParams(const fo4::nif::EffectShader& effect, const std::optional<fo4::nif::AlphaProperty>& alpha) {
    const auto& color = effect.base_color;
    nlohmann::json params{
        {"baseColor", nlohmann::json::array({color[0], color[1], color[2], color[3]})},
        {"baseColorScale", effect.base_color_scale},
        {"emissiveColor", nlohmann::json::array({color[0], color[1], color[2]})},
        {"emissiveMultiple", effect.base_color_scale},
        {"useFalloff", ((effect.flags.word1 >> 6U) & 1U) != 0},
        {"falloffStartAngle", effect.falloff_start_angle},
        {"falloffStopAngle", effect.falloff_stop_angle},
        {"falloffStartOpacity", effect.falloff_start_opacity},
        {"falloffStopOpacity", effect.falloff_stop_opacity},
        {"softFalloffDepth", effect.soft_falloff_depth},
        {"vertexAlpha", ((effect.flags.word1 >> 3U) & 1U) != 0},
        {"alphaBlend", alpha.has_value() && alpha->blend_enabled},
    };
    if (alpha) {
        params["alphaSrcBlend"] = static_cast<std::uint32_t>(alpha->source_blend);
        params["alphaDstBlend"] = static_cast<std::uint32_t>(alpha->destination_blend);
    }
    return params;
}

nlohmann::json segmentTableJson(const fo4::nif::SegmentTable& table) {
    nlohmann::json out{
        {"primitives", table.primitives},
        {"totalSegments", table.total_segments},
        {"segments", nlohmann::json::array()},
        {"segmentStarts", table.segment_starts},
        {"userData", nlohmann::json::array()},
        {"ssfFile", table.ssf_file},
    };
    for (const auto& segment : table.segments) {
        auto subSegments = nlohmann::json::array();
        for (const auto& sub : segment.sub_segments) {
            subSegments.push_back({{"startIndex", sub.start_index},
                                   {"primitives", sub.primitives},
                                   {"parentArrayIndex", sub.parent_array_index},
                                   {"unused", sub.unused}});
        }
        out["segments"].push_back({{"startIndex", segment.start_index},
                                   {"primitives", segment.primitives},
                                   {"parentArrayIndex", segment.parent_array_index},
                                   {"subSegments", subSegments}});
    }
    for (const auto& user : table.user_data) {
        out["userData"].push_back({{"userIndex", user.user_index},
                                  {"boneId", user.bone_id},
                                  {"cutOffsets", user.cut_offsets}});
    }
    return out;
}

nlohmann::json sidecarJson(const std::filesystem::path& nifPath, const std::string& ssfFile, const std::string& shape) {
    const auto cut = ssfFile.find_last_of("\\/");
    const auto leaf = cut == std::string::npos ? ssfFile : ssfFile.substr(cut + 1);
    const auto path = nifPath.parent_path() / leaf;
    const bool found = std::filesystem::is_regular_file(path);
    nlohmann::json out{{"path", path.string()}, {"found", found}, {"listed", false}};
    if (!found) return out;
    try {
        std::ifstream stream(path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        for (const auto& entry : fo4::nif::read_ssf(text)) {
            if (entry.name != shape) continue;
            out["listed"] = true;
            if (entry.base_bone) out["baseBone"] = *entry.base_bone;
            auto deltas = nlohmann::json::array();
            for (const auto& delta : entry.deltas) {
                deltas.push_back({{"bone", delta.bone_name}, {"segmentIds", delta.segment_ids}});
            }
            out["deltas"] = std::move(deltas);
            break;
        }
    } catch (const fo4::parse_error& error) {
        out["error"] = error.parse_message();
    }
    return out;
}

nlohmann::json inspectShape(const fo4::nif::InspectShape& shape) {
    nlohmann::json out{
        {"name", shape.name},
        {"verts", shape.verts},
        {"tris", shape.tris},
        {"skinned", shape.skinned},
        {"shader", shape.material},
        {"textures", textureRefs(shape.textures)},
        {"tangents", shape.tangents},
    };
    if (shape.segments) {
        std::size_t subSegments = 0;
        for (const auto& segment : shape.segments->segments) subSegments += segment.sub_segments.size();
        out["segments"] = shape.segments->segments.size();
        out["subSegments"] = subSegments;
        if (!shape.segments->ssf_file.empty()) out["ssfFile"] = shape.segments->ssf_file;
    }
    if (shape.alpha) {
        out["alphaFlags"] = shape.alpha->flags;
        out["alphaThreshold"] = static_cast<std::uint32_t>(shape.alpha->threshold);
    }
    if (shape.effect) {
        out["effectShader"] = true;
        out["effectName"] = shape.effect->name;
    }
    return out;
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

std::vector<float> columnMajor(const fo4::nif::Transform& xf) {
    std::vector<float> m(16, 0.0f);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) m[col * 4 + row] = xf.s * xf.r[row * 3 + col];
        m[12 + row] = xf.t[row];
    }
    m[15] = 1.0f;
    return m;
}

std::string verifyText(const fo4::nif::VerifyReport& report) {
    std::string out = "RESULT: " + std::to_string(report.checks.size()) + " checks, " +
        std::to_string(report.failed()) + " failed";
    for (const auto& check : report.checks) {
        out += check.failure.empty() ? "\n[ok] " + check.name : "\n[FAIL] " + check.name + ": " + check.failure;
    }
    return out;
}

// Constraint: a sidecar that is not present passes, since vanilla names sidecars it does not ship.
fo4::nif::VerifyReport verifyReport(const std::filesystem::path& nifPath) {
    const auto folder = nifPath.parent_path();
    const fo4::nif::SidecarProbe readable = [&folder](std::string_view name) {
        std::error_code error;
        const auto sidecar = folder / std::string(name);
        if (!std::filesystem::exists(sidecar, error)) return true;
        return std::ifstream(sidecar, std::ios::binary).good();
    };
    try {
        return fo4::nif::verify_nif(loadNif(nifPath), readable);
    } catch (const fo4::parse_error& error) {
        return fo4::nif::VerifyReport{{{"header parses", error.parse_message()}}};
    } catch (const std::exception& error) {
        return fo4::nif::VerifyReport{{{"header parses", error.what()}}};
    }
}

nlohmann::json verifyJson(const fo4::nif::VerifyReport& report) {
    auto checks = nlohmann::json::array();
    for (const auto& check : report.checks) {
        checks.push_back({{"name", check.name}, {"failure", check.failure}, {"fixable", check.fixable}});
    }
    return {{"text", verifyText(report)}, {"checks", std::move(checks)}};
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
    if (rawValue.is_array() && std::all_of(rawValue.begin(), rawValue.end(),
                                           [](const nlohmann::json& item) { return item.is_number(); })) {
        return fo4::nif::Edit{id, key, rawValue.get<std::vector<float>>()};
    }
    return std::unexpected(Error{"NifEditFailed", "key '" + key +
        "' has a value the in-process NIF writer cannot set (text, numbers and number lists only)"});
}

Result<std::filesystem::path> saveNifBytes(const std::vector<std::byte>& bytes, const std::filesystem::path& nifPath,
                                           const std::filesystem::path& outputPath, const std::string& failure) {
    const auto target = outputPath.empty() ? nifPath : outputPath;
    auto temp = target;
    temp += ".niftmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) return std::unexpected(Error{failure, "could not create the output file"});
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) return std::unexpected(Error{failure, "could not write the output file"});
    }
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return std::unexpected(Error{failure, "could not replace the target NIF"});
    }
    return target;
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
                entry["world"] = columnMajor(shape.world);
            }
            if (shape.effect) {
                entry["effectShader"] = true;
                entry["effect"] = effectParams(*shape.effect, shape.alpha);
                if (entry["textures"].empty() && !shape.effect->source_texture.empty()) {
                    auto refs = nlohmann::json::array();
                    refs.push_back({{"slot", 0}, {"path", shape.effect->source_texture}});
                    entry["textures"] = refs;
                }
                if (((shape.effect->flags.word2 >> 5U) & 1U) != 0 && !shape.colors.empty()) {
                    entry["vertexColors"] = shape.colors;
                }
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
        if (report.bsx_flags) out["bsxFlags"] = *report.bsx_flags;
        if (!report.connect_points.empty()) {
            auto list = nlohmann::json::array();
            for (const auto& point : report.connect_points) {
                const auto& r = point.rotation_wxyz;
                const auto& t = point.translation;
                list.push_back({{"parent", point.parent},
                                {"name", point.name},
                                {"rotation", nlohmann::json::array({r[0], r[1], r[2], r[3]})},
                                {"translation", nlohmann::json::array({t[0], t[1], t[2]})},
                                {"scale", point.scale}});
            }
            out["connectPoints"] = list;
        }
        if (!report.connect_children.empty()) {
            auto list = nlohmann::json::array();
            for (const auto& child : report.connect_children) {
                list.push_back({{"skinned", child.skinned}, {"names", child.names}});
            }
            out["connectChildren"] = list;
        }
        for (const auto& shape : report.shapes) out["shapes"].push_back(inspectShape(shape));
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
    return verifyText(verifyReport(nifPath));
}

Result<std::string> renderVerifyJson(const std::filesystem::path& nifPath) {
    return dumpJson(verifyJson(verifyReport(nifPath)));
}

Result<std::string> renderSegments(const std::filesystem::path& nifPath, std::uint32_t block) {
    try {
        const auto nif = loadNif(nifPath);
        if (block >= nif.blocks().size() || nif.blocks()[block].type != "BSSubIndexTriShape") {
            return std::unexpected(Error{"NifParseFailed", "block " + std::to_string(block) + " is not a BSSubIndexTriShape"});
        }
        const auto table = fo4::nif::read_segments(nif, block);
        const auto name = fo4::nif::read_shape(nif, block).name;
        nlohmann::json out = segmentTableJson(table);
        out["block"] = block;
        out["name"] = name;
        if (!table.ssf_file.empty()) out["sidecar"] = sidecarJson(nifPath, table.ssf_file, name);
        return dumpJson(out);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> renderUvSummary(const std::filesystem::path& nifPath) {
    try {
        const auto shapes = fo4::nif::read_shapes(loadNif(nifPath));
        auto out = uvSummaryJson(shapes);
        out["file"] = nifPath.filename().string();
        return out.dump();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> applyUvTransform(const std::filesystem::path& nifPath, std::uint32_t block, int channel,
                                     const UvTransform& transform, const std::filesystem::path& outputPath) {
    if (channel != 0 && channel != 2) {
        return std::unexpected(Error{"UvTransformFailed", "channel must be 0 or 2"});
    }
    try {
        const auto shapes = fo4::nif::read_shapes(loadNif(nifPath));
        const auto shape = std::find_if(shapes.begin(), shapes.end(),
                                        [block](const fo4::nif::Shape& item) { return item.block == block; });
        if (shape == shapes.end()) {
            return std::unexpected(Error{"UvTransformFailed", "block " + std::to_string(block) + " is not a shape"});
        }
        const auto& current = channel == 2 ? shape->uvs2 : shape->uvs;
        auto next = transformedUvs(current, transform);
        if (!next) return std::unexpected(next.error());
        nlohmann::json edits = nlohmann::json::array();
        edits.push_back({{"id", block}, {"key", channel == 2 ? "uvs2" : "uvs"}, {"value", *next}});
        auto bytes = applyEditsInProcess(nifPath, edits);
        if (!bytes) return std::unexpected(bytes.error());
        const auto target = saveNifBytes(*bytes, nifPath, outputPath, "UvTransformFailed");
        if (!target) return std::unexpected(target.error());
        return "set OK (in-process)\nSaved: " + target->string();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

Result<std::string> renderVertexColors(const std::filesystem::path& nifPath) {
    try {
        const auto shapes = fo4::nif::read_shapes(loadNif(nifPath));
        auto out = vertexColorSummaryJson(shapes);
        out["file"] = nifPath.filename().string();
        return out.dump();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> applyVertexColor(const std::filesystem::path& nifPath, const fo4::nif::VertexColorOp& op,
                                     const std::filesystem::path& outputPath) {
    try {
        const auto bytes = fo4::nif::apply_vertex_color(loadNif(nifPath), op);
        const auto target = saveNifBytes(bytes, nifPath, outputPath, "VertexColorFailed");
        if (!target) return std::unexpected(target.error());
        return "vertex color OK (in-process)\nSaved: " + target->string();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

}  // namespace fo4ide::assets
