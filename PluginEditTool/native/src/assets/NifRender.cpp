#include <fo4ide/assets/NifRender.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
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
#include <fo4/io/atomic_write.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/nif/customization_remap.hpp>
#include <fo4/nif/describe.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/nif/outfit_studio.hpp>
#include <fo4/nif/shader.hpp>
#include <fo4/nif/sidecars.hpp>
#include <fo4/nif/weight_brush.hpp>
#include <fo4/nif/weight_transfer.hpp>

#include <nlohmann/json.hpp>

namespace fo4ide::assets {
namespace {

std::vector<std::byte> readNifBytes(const std::filesystem::path& nifPath) {
    std::ifstream stream(nifPath, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open NIF: " + nifPath.string());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto bytes = std::as_bytes(std::span(raw));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

bool isMaterialPath(std::string path) {
    std::ranges::transform(path, path.begin(),
                           [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    return path.ends_with(".bgsm") || path.ends_with(".bgem");
}

bool isEditorMarker(std::string name) {
    std::ranges::transform(name, name.begin(),
                           [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    return name.find("editormarker") != std::string::npos;
}

fo4::nif::NifFile loadNif(const std::filesystem::path& nifPath) {
    return fo4::nif::read_nif(readNifBytes(nifPath));
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
    if (const auto* word = std::get_if<std::uint32_t>(&value)) return *word;
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
    if (rawValue.is_number_unsigned() &&
        rawValue.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return fo4::nif::Edit{id, key, static_cast<std::uint32_t>(rawValue.get<std::uint64_t>())};
    }
    if (rawValue.is_number()) return fo4::nif::Edit{id, key, rawValue.get<float>()};
    if (rawValue.is_array() && std::all_of(rawValue.begin(), rawValue.end(),
                                           [](const nlohmann::json& item) { return item.is_number(); })) {
        return fo4::nif::Edit{id, key, rawValue.get<std::vector<float>>()};
    }
    return std::unexpected(Error{"NifEditFailed", "key '" + key +
        "' has a value the in-process NIF writer cannot set (text, numbers and number lists only)"});
}

Result<std::filesystem::path> saveNifBytes(const std::vector<std::byte>& bytes, const std::filesystem::path& nifPath,
                                           const std::filesystem::path& outputPath, const std::string& failure,
                                           bool overwrite) {
    const auto target = outputPath.empty() ? nifPath : outputPath;
    const bool replace = overwrite || isInPlaceSave(nifPath, outputPath);
    std::error_code present;
    if (!replace && std::filesystem::exists(target, present)) return std::unexpected(outputExistsError(target));
    try {
        auto staged = fo4::io::StagedFile::create(target);
        staged.write(bytes);
        staged.finish();
        staged.verify(bytes.size());
        if (replace) {
            std::move(staged).commit();
        } else if (!std::move(staged).commit_if_absent()) {
            return std::unexpected(outputExistsError(target));
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{failure, error.what()});
    }
    return target;
}

Result<std::vector<std::byte>> applyEditsInProcess(const fo4::nif::NifFile& nif, const nlohmann::json& edits) {
    std::vector<fo4::nif::Edit> converted;
    for (const auto& raw : edits) {
        auto edit = toEdit(raw);
        if (!edit) return std::unexpected(edit.error());
        converted.push_back(std::move(*edit));
    }
    try {
        return fo4::nif::apply_edits(nif, converted);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

bool vertexColoursEnabled(const fo4::nif::NifFile& nif, std::int32_t shader) {
    if (shader < 0) return true;
    const auto index = static_cast<std::uint32_t>(shader);
    const auto& type = nif.blocks().at(index).type;
    fo4::nif::ShaderFlags flags;
    if (type == "BSLightingShaderProperty") {
        flags = fo4::nif::read_lighting_shader(nif, index).flags;
    } else if (type == "BSEffectShaderProperty") {
        flags = fo4::nif::read_effect_shader(nif, index).flags;
    } else {
        return true;
    }
    const auto names = fo4::nif::shader_flag_names(flags);
    return std::find(names.begin(), names.end(), "Vertex_Colors") != names.end();
}

// Invariant: the value is nif.xml TexClampMode (bit 1 wraps S, bit 0 wraps T); a shape without a readable shader has none.
std::optional<std::uint32_t> shaderTextureClamp(const fo4::nif::NifFile& nif, std::int32_t shader) {
    if (shader < 0) return std::nullopt;
    try {
        const auto index = static_cast<std::uint32_t>(shader);
        const auto& type = nif.blocks().at(index).type;
        if (type == "BSLightingShaderProperty") return fo4::nif::read_lighting_shader(nif, index).texture_clamp;
        if (type == "BSEffectShaderProperty") return fo4::nif::read_effect_shader(nif, index).texture_clamp;
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

nlohmann::json skinJson(const fo4::nif::GeometryShape& shape) {
    auto bones = nlohmann::json::array();
    for (const auto& bone : shape.bones) {
        bones.push_back({
            {"name", bone.name},
            {"rotation", std::vector<float>(bone.rotation, bone.rotation + 9)},
            {"translation", std::vector<float>(bone.translation, bone.translation + 3)},
            {"scale", bone.scale},
        });
    }
    return {{"weights", shape.weights}, {"boneIndices", shape.bone_indices}, {"bones", std::move(bones)}};
}

}  // namespace

bool isInPlaceSave(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath) {
    std::error_code ec;
    return outputPath.empty() || std::filesystem::equivalent(outputPath, nifPath, ec);
}

Error outputExistsError(const std::filesystem::path& target) {
    return Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + target.string()};
}

Result<std::string> renderGeometry(const std::filesystem::path& nifPath, bool includeSkin) {
    try {
        const auto bytes = readNifBytes(nifPath);
        const auto nif = fo4::nif::read_nif(bytes);
        const auto report = fo4::nif::geometry_nif(nif);
        nlohmann::json out{{"fo4", report.fo4}, {"shapes", nlohmann::json::array()}, {"source", fo4::io::sha256_hex(bytes)}};
        for (const auto& shape : report.shapes) {
            if (shape.hidden || isEditorMarker(shape.name)) continue;
            nlohmann::json entry{
                {"block", shape.block},
                {"name", shape.name},
                {"verts", shape.verts},
                {"tris", shape.tris},
                {"normals", shape.normals},
                {"uvs", shape.uvs},
                {"textures", textureRefs(shape.textures)},
                {"skinned", shape.skinned},
                {"shaderType", shape.shader_kind},
            };
            if (!shape.uvs2.empty()) entry["uvs2"] = shape.uvs2;
            if (!shape.colors.empty() && vertexColoursEnabled(nif, shape.shader_ref)) entry["vertexColors"] = shape.colors;
            if (includeSkin && !shape.bones.empty()) entry["skin"] = skinJson(shape);
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
            }
            if (const auto clamp = shaderTextureClamp(nif, shape.shader_ref)) entry["textureClamp"] = *clamp;
            if (isMaterialPath(shape.material)) entry["bgsmPath"] = shape.material;
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
        const auto bytes = readNifBytes(nifPath);
        const auto tree = fo4::nif::describe_nif(fo4::nif::read_nif(bytes));
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
            {"source", fo4::io::sha256_hex(bytes)},
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

std::vector<std::string> renderNewVerifyFailures(const std::filesystem::path& candidate,
                                                 const std::optional<std::filesystem::path>& baseline) {
    std::vector<std::string> failedBefore;
    if (baseline) {
        for (const auto& check : verifyReport(*baseline).checks) {
            if (!check.failure.empty()) failedBefore.push_back(check.name);
        }
    }
    std::vector<std::string> fresh;
    for (const auto& check : verifyReport(candidate).checks) {
        if (check.failure.empty()) continue;
        if (std::find(failedBefore.begin(), failedBefore.end(), check.name) != failedBefore.end()) continue;
        fresh.push_back(check.name + ": " + check.failure);
    }
    return fresh;
}

Result<std::string> renderSegments(const std::filesystem::path& nifPath, std::uint32_t block) {
    try {
        const auto bytes = readNifBytes(nifPath);
        const auto nif = fo4::nif::read_nif(bytes);
        if (block >= nif.blocks().size() || nif.blocks()[block].type != "BSSubIndexTriShape") {
            return std::unexpected(Error{"NifParseFailed", "block " + std::to_string(block) + " is not a BSSubIndexTriShape"});
        }
        const auto table = fo4::nif::read_segments(nif, block);
        const auto name = fo4::nif::read_shape(nif, block).name;
        nlohmann::json out = segmentTableJson(table);
        out["block"] = block;
        out["name"] = name;
        out["source"] = fo4::io::sha256_hex(bytes);
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
        return dumpJson(out);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> commitUvTransform(const std::filesystem::path& nifPath, const std::vector<std::byte>& source,
                                      const std::vector<std::byte>& bytes, const std::filesystem::path& outputPath,
                                      bool overwrite) {
    const bool inPlace = isInPlaceSave(nifPath, outputPath);
    if (inPlace && readNifBytes(nifPath) != source) {
        return std::unexpected(Error{"UvTransformFailed", "the NIF changed while its UVs were being transformed; nothing was written"});
    }
    const auto target = saveNifBytes(bytes, nifPath, outputPath, "UvTransformFailed", overwrite);
    if (!target) return std::unexpected(target.error());
    return "set OK (in-process)\nSaved: " + target->string();
}

Result<std::string> applyUvTransform(const std::filesystem::path& nifPath, std::uint32_t block, int channel,
                                     const UvTransform& transform, const std::filesystem::path& outputPath,
                                     bool overwrite) {
    if (channel != 0 && channel != 2) {
        return std::unexpected(Error{"UvTransformFailed", "channel must be 0 or 2"});
    }
    try {
        const auto source = readNifBytes(nifPath);
        const auto nif = fo4::nif::read_nif(source);
        const auto shapes = fo4::nif::read_shapes(nif);
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
        auto bytes = applyEditsInProcess(nif, edits);
        if (!bytes) return std::unexpected(bytes.error());
        return commitUvTransform(nifPath, source, *bytes, outputPath, overwrite);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

Result<std::string> renderVertexColors(const std::filesystem::path& nifPath) {
    try {
        const auto bytes = readNifBytes(nifPath);
        const auto shapes = fo4::nif::read_shapes(fo4::nif::read_nif(bytes));
        auto out = vertexColorSummaryJson(shapes);
        out["file"] = nifPath.filename().string();
        out["source"] = fo4::io::sha256_hex(bytes);
        return dumpJson(out);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifParseFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifParseFailed", error.what()});
    }
}

Result<std::string> applyVertexColor(const std::filesystem::path& nifPath, const fo4::nif::VertexColorOp& op,
                                     const std::filesystem::path& outputPath, std::string_view expectedSource,
                                     bool overwrite) {
    try {
        const auto source = readNifBytes(nifPath);
        if (!expectedSource.empty() && fo4::io::sha256_hex(source) != expectedSource) {
            return std::unexpected(Error{"NifChanged", "Not saved: " + nifPath.string() +
                " changed on disk after it was loaded -- reload it before saving"});
        }
        const auto bytes = fo4::nif::apply_vertex_color(fo4::nif::read_nif(source), op);
        const auto target = saveNifBytes(bytes, nifPath, outputPath, "VertexColorFailed", overwrite);
        if (!target) return std::unexpected(target.error());
        return "vertex color OK (in-process)\nSaved: " + target->string();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

Result<std::string> applySkinEdit(const std::filesystem::path& nifPath, const fo4::nif::SkinEditRequest& request,
                                  const std::filesystem::path& outputPath, std::string_view expectedSource,
                                  bool overwrite) {
    try {
        const auto source = readNifBytes(nifPath);
        if (!expectedSource.empty() && fo4::io::sha256_hex(source) != expectedSource) {
            return std::unexpected(Error{"NifChanged", "Not saved: " + nifPath.string() +
                " changed on disk after it was loaded -- reload it before saving"});
        }
        fo4::nif::SkinEditReport report;
        const auto bytes = fo4::nif::apply_skin_edit(fo4::nif::read_nif(source), request, &report);
        const auto target = saveNifBytes(bytes, nifPath, outputPath, "SkinEditFailed", overwrite);
        if (!target) return std::unexpected(target.error());

        std::string skipped;
        for (const auto& entry : report.skipped) skipped += (skipped.empty() ? "" : ", ") + entry;
        std::string text = "RESULT: skin edit " + std::string(fo4::nif::skin_edit_kind_name(request.kind)) +
                           " OK (in-process)\nSaved: " + target->string() + "\nBlock: " +
                           std::to_string(report.block) + "\nVertices changed: " +
                           std::to_string(report.vertices_changed) + ", largest weight change " +
                           std::to_string(report.max_weight_change);
        if (request.kind == fo4::nif::SkinEditKind::mirror_weights) {
            text += "\nMirrored: " + std::to_string(report.mirrored) + ", no partner vertex " +
                    std::to_string(report.no_partner_vertex) + ", ambiguous " +
                    std::to_string(report.ambiguous_vertex) + ", no partner bone " +
                    std::to_string(report.no_partner_bone);
        }
        if (!skipped.empty()) text += "\nSkipped bones: " + skipped;
        return text;
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"SkinEditFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SkinEditFailed", error.what()});
    }
}

namespace {

std::uint32_t skinToolBlock(const nlohmann::json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_number_integer()) throw std::invalid_argument(std::string(key) + " must be a block index");
    const auto value = it->get<std::int64_t>();
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument(std::string(key) + " must be a block index");
    }
    return static_cast<std::uint32_t>(value);
}

std::uint8_t skinToolBone(const nlohmann::json& value, const char* label) {
    if (!value.is_number_integer()) throw std::invalid_argument(std::string(label) + " must be a bone index");
    const auto index = value.get<std::int64_t>();
    if (index < 0 || index > 255) throw std::invalid_argument(std::string(label) + " must be 0 to 255");
    return static_cast<std::uint8_t>(index);
}

float skinToolNumber(const nlohmann::json& value, const char* key) {
    if (!value.is_number()) throw std::invalid_argument(std::string(key) + " must be a number");
    const auto number = value.get<double>();
    if (!std::isfinite(number) || std::fabs(number) > std::numeric_limits<float>::max()) {
        throw std::invalid_argument(std::string(key) + " must be a finite number");
    }
    return static_cast<float>(number);
}

float skinToolFloat(const nlohmann::json& body, const char* key, std::optional<float> fallback = std::nullopt) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        if (fallback) return *fallback;
        throw std::invalid_argument(std::string(key) + " is required");
    }
    return skinToolNumber(*it, key);
}

template <typename Enum>
Enum skinToolName(const nlohmann::json& body, const char* key, std::initializer_list<std::pair<std::string_view, Enum>> names) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) throw std::invalid_argument(std::string(key) + " is required");
    const auto& text = it->get_ref<const std::string&>();
    for (const auto& entry : names) {
        if (entry.first == text) return entry.second;
    }
    throw std::invalid_argument("unknown " + std::string(key) + " '" + text + "'");
}

std::string skinToolHex(std::span<const std::byte> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(digits[std::to_integer<unsigned>(byte) >> 4]);
        out.push_back(digits[std::to_integer<unsigned>(byte) & 15U]);
    }
    return out;
}

int skinToolNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

fo4::nif::RemapNewBone skinToolNewBone(const nlohmann::json& value) {
    if (!value.is_string()) throw std::invalid_argument("a new bone must be a hex string");
    const auto& text = value.get_ref<const std::string&>();
    fo4::nif::RemapNewBone bone;
    if (text.size() != 2 * bone.raw.size()) throw std::invalid_argument("a new bone is 208 bytes (416 hex characters)");
    for (std::size_t i = 0; i < bone.raw.size(); ++i) {
        const int high = skinToolNibble(text[2 * i]);
        const int low = skinToolNibble(text[2 * i + 1]);
        if (high < 0 || low < 0) throw std::invalid_argument("a new bone must be hex digits only");
        bone.raw[i] = static_cast<std::byte>(high * 16 + low);
    }
    return bone;
}

std::vector<fo4::nif::RemapRecord> skinToolRecords(const nlohmann::json& list) {
    if (!list.is_array()) throw std::invalid_argument("records must be an array");
    std::vector<fo4::nif::RemapRecord> records;
    records.reserve(list.size());
    for (const auto& item : list) {
        if (!item.is_object() || !item.contains("bones") || !item.contains("weights") || !item.at("bones").is_array() ||
            !item.at("weights").is_array() || item.at("bones").size() != 4 || item.at("weights").size() != 4) {
            throw std::invalid_argument("a remap record holds four bones and four weights");
        }
        fo4::nif::RemapRecord record;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            record.bone[slot] = skinToolBone(item.at("bones")[slot], "record bone");
            const auto weight = skinToolNumber(item.at("weights")[slot], "record weight");
            if (weight < 0.0f || weight > 1.0f) throw std::invalid_argument("a remap weight must be from 0 to 1");
            record.weight[slot] = weight;
        }
        records.push_back(record);
    }
    return records;
}

std::vector<fo4::nif::RemapRecord> skinToolSkinRecords(const fo4::nif::NifFile& nif, std::uint32_t block) {
    std::vector<fo4::nif::RemapRecord> records;
    for (const auto& vertex : fo4::nif::read_shape_weights(nif, block)) records.push_back({vertex.weight, vertex.bone});
    return records;
}

bool skinToolFlag(const nlohmann::json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end()) return false;
    if (!it->is_boolean()) throw std::invalid_argument(std::string(key) + " must be true or false");
    return it->get<bool>();
}

// Invariant: a remap with new bones, or a record past the skin's bones, is never overwritten by the skin weights.
void skinToolRefuseSkinOverwrite(const fo4::nif::CustomizationRemap& remap, std::size_t skinBoneCount,
                                 bool replaceRecords) {
    const auto pastSkin = [skinBoneCount](std::uint8_t bone) {
        return static_cast<std::size_t>(bone) >= skinBoneCount;
    };
    const bool namesNewBone = std::any_of(remap.records.begin(), remap.records.end(),
                                          [&](const fo4::nif::RemapRecord& record) {
                                              return std::any_of(record.bone.begin(), record.bone.end(), pastSkin);
                                          });
    if (!remap.new_bones.empty() || namesNewBone) {
        throw std::invalid_argument("this remap uses new bones; edit records instead");
    }
    if (!replaceRecords) {
        throw std::invalid_argument(
            "from_skin replaces every remap record with the skin weights; send replace_records true to confirm");
    }
}

nlohmann::json skinToolRemapJson(const fo4::nif::CustomizationRemap& remap) {
    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : remap.records) records.push_back({{"bones", record.bone}, {"weights", record.weight}});
    nlohmann::json newBones = nlohmann::json::array();
    for (const auto& bone : remap.new_bones) {
        newBones.push_back({{"name", fo4::nif::remap_new_bone_name(bone)}, {"raw", skinToolHex(bone.raw)}});
    }
    return {{"remap_block", remap.remap_block}, {"new_bones_block", remap.new_bones_block},
            {"records", std::move(records)}, {"new_bones", std::move(newBones)}};
}

std::string skinToolFile(const nlohmann::json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(std::string(key) + " is required");
    }
    const std::filesystem::path path(it->get_ref<const std::string&>());
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::string skinToolText(const nlohmann::json& body, const char* key) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(std::string(key) + " must be a name");
    }
    return it->get<std::string>();
}

float skinToolSliderWeight(const nlohmann::json& body) {
    const auto weight = skinToolFloat(body, "weight");
    if (weight < 0.0f || weight > 1.0f) throw std::invalid_argument("weight must be from 0 to 1");
    return weight;
}

nlohmann::json skinToolOspJson(const fo4::nif::OspFile& file) {
    nlohmann::json sets = nlohmann::json::array();
    for (const auto& set : file.sets) {
        nlohmann::json shapes = nlohmann::json::array();
        for (const auto& shape : set.shapes) shapes.push_back({{"name", shape.name}, {"target", shape.target}});
        nlohmann::json sliders = nlohmann::json::array();
        for (const auto& slider : set.sliders) {
            nlohmann::json data = nlohmann::json::array();
            for (const auto& entry : slider.data) {
                data.push_back({{"name", entry.name}, {"target", entry.target}, {"local", entry.local},
                                {"file", entry.file}, {"name_in_file", entry.name_in_file},
                                {"legacy_bsd", entry.legacy_bsd}});
            }
            sliders.push_back({{"name", slider.name}, {"invert", slider.invert}, {"hidden", slider.hidden},
                               {"zap", slider.zap}, {"uv", slider.uv}, {"small", slider.small_value},
                               {"big", slider.big_value}, {"data", std::move(data)}});
        }
        sets.push_back({{"name", set.name}, {"shapes", std::move(shapes)}, {"sliders", std::move(sliders)}});
    }
    return {{"version", file.version}, {"sets", std::move(sets)}};
}

nlohmann::json skinToolOsdJson(const fo4::nif::OsdFile& file) {
    nlohmann::json sets = nlohmann::json::array();
    for (const auto& set : file.sets) sets.push_back({{"name", set.name}, {"offsets", set.offsets.size()}});
    return {{"version", file.version}, {"sets", std::move(sets)}};
}

// Invariant: a slider flagged invert, zap or uv is never added as a position offset, because the library carries those flags unread.
const fo4::nif::OsdDataSet& skinToolSliderDiff(const nlohmann::json& body, const fo4::nif::OsdFile& osd,
                                               const fo4::nif::NifFile& nif, std::uint32_t block) {
    const auto ospText = skinToolFile(body, "osp_path");
    const auto osp = fo4::nif::parse_osp(ospText);
    const fo4::nif::OspSet* set = nullptr;
    if (const auto it = body.find("set"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) throw std::invalid_argument("set must be a set name");
        set = osp.find_set(it->get_ref<const std::string&>());
        if (set == nullptr) throw std::invalid_argument("the .osp holds no set named " + it->get<std::string>());
    } else if (osp.sets.size() == 1) {
        set = &osp.sets.front();
    } else {
        throw std::invalid_argument("the .osp holds several sets; name one");
    }
    const auto sliderName = skinToolText(body, "slider");
    const auto* slider = set->find_slider(sliderName);
    if (slider == nullptr) throw std::invalid_argument("the set has no slider named " + sliderName);
    if (slider->invert || slider->zap || slider->uv) {
        throw std::invalid_argument("slider " + sliderName + " is flagged invert, zap or uv; its weight scale is not confirmed");
    }
    const auto shapeName = fo4::nif::read_shape(nif, block).name;
    const auto* diff = fo4::nif::find_osd_diff(osd, body.at("osd_path").get_ref<const std::string&>(), *set,
                                               sliderName, shapeName);
    if (diff == nullptr) {
        throw std::invalid_argument("slider " + sliderName + " has no data for shape " + shapeName + " in this .osd");
    }
    return *diff;
}

void skinToolNewBones(const nlohmann::json& body, fo4::nif::CustomizationRemap& remap) {
    const auto it = body.find("new_bones");
    if (it == body.end() || it->is_null()) return;
    if (!it->is_array()) throw std::invalid_argument("new_bones must be an array");
    remap.new_bones.clear();
    for (const auto& item : *it) remap.new_bones.push_back(skinToolNewBone(item));
}

}  // namespace

Result<std::string> applySkinTool(const std::filesystem::path& nifPath, std::string_view op, const nlohmann::json& body,
                                  const std::filesystem::path& outputPath, std::string_view expectedSource,
                                  bool overwrite) {
    try {
        if (!body.is_object()) throw std::invalid_argument("the skin tool arguments must be an object");
        if (op == "osp_read" || op == "osd_read") {
            nlohmann::json reply = {{"ok", true}, {"op", std::string(op)}, {"saved", ""}};
            const auto text = skinToolFile(body, "path");
            if (op == "osp_read") {
                reply.update(skinToolOspJson(fo4::nif::parse_osp(text)));
            } else {
                reply.update(skinToolOsdJson(fo4::nif::parse_osd(std::as_bytes(std::span(text)))));
            }
            return dumpJson(reply);
        }
        const auto source = readNifBytes(nifPath);
        if (!expectedSource.empty() && fo4::io::sha256_hex(source) != expectedSource) {
            return std::unexpected(Error{"NifChanged", "Not saved: " + nifPath.string() +
                " changed on disk after it was loaded -- reload it before saving"});
        }
        const auto nif = fo4::nif::read_nif(source);
        const auto block = skinToolBlock(body, "block");
        nlohmann::json reply = {{"ok", true}, {"op", std::string(op)}, {"block", block}, {"saved", ""}};
        const auto save = [&](const std::vector<std::byte>& bytes) -> std::optional<Error> {
            const auto target = saveNifBytes(bytes, nifPath, outputPath, "SkinToolFailed", overwrite);
            if (!target) return target.error();
            reply["saved"] = target->string();
            return std::nullopt;
        };

        if (op == "brush") {
            fo4::nif::WeightBrush brush;
            brush.mode = skinToolName<fo4::nif::BrushMode>(body, "mode", {
                {"add", fo4::nif::BrushMode::add}, {"subtract", fo4::nif::BrushMode::subtract},
                {"replace", fo4::nif::BrushMode::replace}, {"smooth", fo4::nif::BrushMode::smooth}});
            brush.falloff = skinToolName<fo4::nif::BrushFalloff>(body, "falloff", {
                {"smoothstep", fo4::nif::BrushFalloff::smoothstep}, {"linear", fo4::nif::BrushFalloff::linear},
                {"constant", fo4::nif::BrushFalloff::constant}});
            const auto centre = body.find("center");
            if (centre == body.end() || !centre->is_array() || centre->size() != 3) {
                throw std::invalid_argument("center must be three numbers");
            }
            for (std::size_t axis = 0; axis < 3; ++axis) brush.center[axis] = skinToolNumber((*centre)[axis], "center");
            brush.radius = skinToolFloat(body, "radius");
            brush.strength = skinToolFloat(body, "strength", 1.0f);
            brush.target = skinToolFloat(body, "target", 1.0f);
            if (brush.mode == fo4::nif::BrushMode::smooth) {
                brush.bone = 0;
            } else {
                if (!body.contains("bone")) throw std::invalid_argument("bone is required");
                brush.bone = skinToolBone(body.at("bone"), "bone");
            }
            std::vector<std::uint8_t> locked;
            if (const auto it = body.find("locked"); it != body.end() && !it->is_null()) {
                if (!it->is_array()) throw std::invalid_argument("locked must be an array of bone indices");
                for (const auto& item : *it) locked.push_back(skinToolBone(item, "locked bone"));
            }
            fo4::nif::WeightBrushReport report;
            const auto bytes = fo4::nif::brush_shape_weights(nif, block, brush, locked, &report);
            if (report.changed != 0) {
                if (const auto failed = save(bytes)) return std::unexpected(*failed);
            }
            reply["in_reach"] = report.in_reach;
            reply["changed"] = report.changed;
            reply["skipped"] = report.skipped;
            reply["max_weight_change"] = report.max_weight_change;
        } else if (op == "transfer") {
            const auto sourcePath = body.find("source_path");
            if (sourcePath == body.end() || !sourcePath->is_string() || sourcePath->get_ref<const std::string&>().empty()) {
                throw std::invalid_argument("source_path is required");
            }
            fo4::nif::WeightTransferOptions options;
            options.method = skinToolName<fo4::nif::TransferMethod>(body, "method", {
                {"nearest_vertex", fo4::nif::TransferMethod::nearest_vertex},
                {"barycentric", fo4::nif::TransferMethod::barycentric}});
            options.max_distance = skinToolFloat(body, "max_distance", 0.0f);
            if (options.max_distance < 0.0f) throw std::invalid_argument("max_distance must not be negative");
            const auto sourceBytes = readNifBytes(std::filesystem::path(sourcePath->get_ref<const std::string&>()));
            fo4::nif::WeightTransferReport report;
            const auto bytes = fo4::nif::transfer_shape_weights(sourceBytes, skinToolBlock(body, "source_block"), source,
                                                                block, options, &report);
            if (report.transferred != 0) {
                if (const auto failed = save(bytes)) return std::unexpected(*failed);
            }
            reply["transferred"] = report.transferred;
            reply["out_of_reach"] = report.out_of_reach;
            reply["unmapped"] = report.unmapped;
        } else if (op == "remap_read") {
            reply.update(skinToolRemapJson(fo4::nif::read_customization_remap(nif, block)));
        } else if (op == "remap_validate") {
            auto remap = fo4::nif::read_customization_remap(nif, block);
            if (const auto it = body.find("records"); it != body.end() && !it->is_null()) remap.records = skinToolRecords(*it);
            skinToolNewBones(body, remap);
            const auto shape = fo4::nif::read_shape(nif, block);
            reply["messages"] = fo4::nif::validate_customization_remap(remap, shape.bones.size(), shape.verts.size() / 3);
        } else if (op == "remap_create") {
            fo4::nif::CustomizationRemap remap;
            if (const auto it = body.find("records"); it != body.end() && !it->is_null()) {
                remap.records = skinToolRecords(*it);
            } else {
                remap.records = skinToolSkinRecords(nif, block);
            }
            skinToolNewBones(body, remap);
            if (const auto failed = save(fo4::nif::create_customization_remap(nif, block, remap))) {
                return std::unexpected(*failed);
            }
            reply["records"] = remap.records.size();
            reply["new_bones"] = remap.new_bones.size();
        } else if (op == "remap_write") {
            auto remap = fo4::nif::read_customization_remap(nif, block);
            if (remap.remap_block < 0) throw std::invalid_argument("the shape owns no customization remap block to write");
            const bool fromSkin = skinToolFlag(body, "from_skin");
            const bool replaceRecords = skinToolFlag(body, "replace_records");
            if (const auto it = body.find("records"); it != body.end() && !it->is_null()) {
                remap.records = skinToolRecords(*it);
            } else if (fromSkin) {
                skinToolRefuseSkinOverwrite(remap, fo4::nif::read_shape(nif, block).bones.size(), replaceRecords);
                remap.records = skinToolSkinRecords(nif, block);
            }
            skinToolNewBones(body, remap);
            if (const auto failed = save(fo4::nif::write_customization_remap(nif, block, remap))) {
                return std::unexpected(*failed);
            }
            reply["records"] = remap.records.size();
            reply["new_bones"] = remap.new_bones.size();
        } else if (op == "osd_apply") {
            const auto weight = skinToolSliderWeight(body);
            const auto osdText = skinToolFile(body, "osd_path");
            const auto osd = fo4::nif::parse_osd(std::as_bytes(std::span(osdText)));
            if (body.contains("data_set") == body.contains("slider")) {
                throw std::invalid_argument("send either data_set, or osp_path with slider");
            }
            const fo4::nif::OsdDataSet* diff = nullptr;
            if (body.contains("data_set")) {
                const auto name = skinToolText(body, "data_set");
                diff = osd.find(name);
                if (diff == nullptr) throw std::invalid_argument("the .osd holds no data set named " + name);
            } else {
                diff = &skinToolSliderDiff(body, osd, nif, block);
            }
            std::size_t moved = 0;
            const auto bytes = fo4::nif::apply_osd_to_shape(nif, block, *diff, weight, &moved);
            if (moved != 0) {
                if (const auto failed = save(bytes)) return std::unexpected(*failed);
            }
            reply["moved"] = moved;
            reply["offsets"] = diff->offsets.size();
            reply["data_set"] = diff->name;
        } else {
            throw std::invalid_argument("unknown skin tool operation '" + std::string(op) + "'");
        }
        return dumpJson(reply);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"SkinToolFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SkinToolFailed", error.what()});
    }
}
}  // namespace fo4ide::assets
