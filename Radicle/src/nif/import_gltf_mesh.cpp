#include "import_gltf_mesh.hpp"

#include "import_blocks.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif::import_detail {
namespace {

using gltf_detail::Document;
using gltf_detail::element;
using gltf_detail::Frame;
using gltf_detail::Matrix;
using gltf_detail::read_accessor;

constexpr std::uint64_t k_triangles_mode = 4;
constexpr std::size_t k_max_joints = 256;
constexpr std::size_t k_influences = 4;
constexpr float k_half_max = 65504.0f;
constexpr std::uint32_t k_unsigned_byte = 5121;
constexpr std::uint32_t k_unsigned_short = 5123;

[[noreturn]] void refuse(const std::string& shape, const std::string& why) {
  throw std::runtime_error("glTF primitive '" + shape + "' " + why);
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 scaled(const Vec3& a, float factor) { return {a[0] * factor, a[1] * factor, a[2] * factor}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

std::size_t components_of(std::string_view type) {
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  return 1;
}

Vec3 vec3_at(const std::vector<double>& values, std::size_t index, std::size_t stride = 3) {
  return {static_cast<float>(values[index * stride]), static_cast<float>(values[index * stride + 1]),
          static_cast<float>(values[index * stride + 2])};
}

std::vector<double> attribute(const Document& doc, const nlohmann::json& attributes, const char* key,
                              std::string_view type, std::size_t count, const std::string& shape) {
  auto values = read_accessor(doc, attributes.at(key).get<std::size_t>(), type);
  if (values.size() != count * components_of(type)) {
    refuse(shape, std::string("has ") + key + " with a different vertex count than POSITION");
  }
  return values;
}

void refuse_extra_sets(const nlohmann::json& attributes, const std::string& shape) {
  for (const auto& item : attributes.items()) {
    const std::string& key = item.key();
    if (key.starts_with("TEXCOORD_") && key != "TEXCOORD_0") {
      refuse(shape, "has " + key + ", a second UV set the NIF writer does not hold");
    }
    if (key.starts_with("COLOR_") && key != "COLOR_0") {
      refuse(shape, "has " + key + ", a second vertex colour set the NIF writer does not hold");
    }
    if ((key.starts_with("JOINTS_") || key.starts_with("WEIGHTS_")) && key.back() > '1') {
      refuse(shape, "has " + key + ", past the eight bone influences a glTF skin can hold");
    }
  }
  if (attributes.contains("JOINTS_1") != attributes.contains("WEIGHTS_1")) {
    refuse(shape, "has JOINTS_1 and WEIGHTS_1 that do not come as a pair");
  }
  if (attributes.contains("JOINTS_1") && !attributes.contains("JOINTS_0")) refuse(shape, "has JOINTS_1 without JOINTS_0");
}

bool whole_index(double value, std::size_t bound) {
  return std::isfinite(value) && value >= 0.0 && value < static_cast<double>(bound) && value == std::floor(value);
}

void require_joint_type(const Document& doc, const nlohmann::json& attributes, const std::string& key,
                        const std::string& shape) {
  const auto component = element(doc.json, "accessors", attributes.at(key).get<std::size_t>())
                             .at("componentType").get<std::uint32_t>();
  if (component != k_unsigned_byte && component != k_unsigned_short) {
    refuse(shape, "has " + key + " whose component type must be UNSIGNED_BYTE or UNSIGNED_SHORT");
  }
}

void split_per_face(Mesh& mesh, const std::string& shape) {
  if (mesh.tris.size() > k_max_shape_vertices) {
    refuse(shape, "needs " + std::to_string(mesh.tris.size()) +
                      " vertices once its faces are split for flat normals; a NIF shape holds at most 65535");
  }
  const bool colours = !mesh.colours.empty();
  const bool skinned = !mesh.bones.empty();
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::vector<Vec4> colour_values;
  std::vector<Influence> bones;
  std::vector<Vec4> weights;
  for (std::size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
    const std::array<std::uint32_t, 3> corners{mesh.tris[t], mesh.tris[t + 1], mesh.tris[t + 2]};
    const Vec3 area = cross(sub(mesh.positions[corners[1]], mesh.positions[corners[0]]),
                            sub(mesh.positions[corners[2]], mesh.positions[corners[0]]));
    const float length = std::sqrt(dot(area, area));
    const Vec3 normal = length > 0.0f ? scaled(area, 1.0f / length) : Vec3{0.0f, 0.0f, 1.0f};
    for (const auto corner : corners) {
      positions.push_back(mesh.positions[corner]);
      normals.push_back(normal);
      uvs.push_back(mesh.uvs[corner]);
      if (colours) colour_values.push_back(mesh.colours[corner]);
      if (skinned) {
        bones.push_back(mesh.bones[corner]);
        weights.push_back(mesh.weights[corner]);
      }
    }
  }
  for (std::size_t i = 0; i < mesh.tris.size(); ++i) mesh.tris[i] = static_cast<std::uint32_t>(i);
  mesh.positions = std::move(positions);
  mesh.normals = std::move(normals);
  mesh.uvs = std::move(uvs);
  if (colours) mesh.colours = std::move(colour_values);
  if (skinned) {
    mesh.bones = std::move(bones);
    mesh.weights = std::move(weights);
  }
}

std::string data_path(std::string text, std::string_view folder, const std::string& label) {
  std::replace(text.begin(), text.end(), '/', '\\');
  if (text.empty()) throw std::runtime_error(label + " names an empty path");
  if (text.starts_with("\\") || (text.size() >= 2 && text[1] == ':')) {
    const auto at = ("\\" + lower(text)).find("\\" + std::string(folder) + "\\");
    if (at == std::string::npos) {
      throw std::runtime_error(label + " path '" + text + "' is an absolute path outside a " + std::string(folder) +
                               " folder; move the file under a " + std::string(folder) + " folder");
    }
    text = text.substr(at);
  } else if (text.starts_with(".\\")) {
    text.erase(0, 2);
  }
  if (text.find("..") != std::string::npos) {
    throw std::runtime_error(label + " path '" + text + "' climbs out of the Data folder");
  }
  return text;
}

// Keeps a relative recorded path byte for byte, because it is the path the NIF stored; absolute and '..' paths go through data_path.
std::string recorded_path(const std::string& text, std::string_view folder, const std::string& label) {
  const bool absolute = text.starts_with("/") || text.starts_with("\\") || (text.size() >= 2 && text[1] == ':');
  if (!absolute && text.find("..") == std::string::npos) return text;
  return data_path(text, folder, label);
}

std::optional<std::string> string_extra(const nlohmann::json& object, std::string_view key) {
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) return std::nullopt;
  auto text = object.at(key).get<std::string>();
  if (text.empty()) return std::nullopt;
  return text;
}

// Returns the image's uri when it names a DDS file, which the importer then converts to Data separators.
std::optional<std::string> image_uri_path(const nlohmann::json& image) {
  if (!image.contains("uri") || !image.at("uri").is_string()) return std::nullopt;
  const auto uri = gltf_detail::decode_uri(image.at("uri").get<std::string>());
  if (uri.starts_with("data:") || !lower(uri).ends_with(".dds")) return std::nullopt;
  return uri;
}

// Returns the image's name when it names a DDS file, with Blender's .NNN copy suffix removed.
std::optional<std::string> image_name_path(const nlohmann::json& image) {
  if (!image.contains("name") || !image.at("name").is_string()) return std::nullopt;
  auto name = image.at("name").get<std::string>();
  const bool copy_suffix = name.size() > 4 && name[name.size() - 4] == '.' &&
                           std::all_of(name.end() - 3, name.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
  if (copy_suffix) name.erase(name.size() - 4);
  if (!lower(name).ends_with(".dds")) return std::nullopt;
  return name;
}

// Requires option to name the ImportOptions field the author can pass for this slot.
std::string texture_slot(const Document& doc, const nlohmann::json& info, const nlohmann::json& material_extras,
                         std::string_view key, const std::string& label, const std::string& fallback,
                         std::string_view option) {
  if (info.contains("extensions") && info.at("extensions").contains("KHR_texture_transform")) {
    throw std::runtime_error(label + " uses KHR_texture_transform, which the importer does not apply; remove the transform before importing");
  }
  if (info.value("texCoord", std::size_t{0}) != 0) {
    throw std::runtime_error(label + " reads a second UV set, which the NIF writer does not hold");
  }
  const auto& texture = element(doc.json, "textures", info.at("index").get<std::size_t>());
  if (!texture.contains("source")) throw std::runtime_error(label + " has no image");
  const auto& image = element(doc.json, "images", texture.at("source").get<std::size_t>());
  if (const auto recorded = string_extra(image.value("extras", nlohmann::json::object()), "fo4Path")) {
    return recorded_path(*recorded, "textures", label);
  }
  if (const auto uri = image_uri_path(image)) return data_path(*uri, "textures", label);
  if (const auto named = image_name_path(image)) return recorded_path(*named, "textures", label);
  if (const auto recorded = string_extra(material_extras, key)) return recorded_path(*recorded, "textures", label);
  if (!fallback.empty()) return fallback;
  throw std::runtime_error(label + " has no game texture path: its image is embedded or not a DDS, and neither the image nor the material names one; pass " +
                           std::string(option) + " to name it");
}

GltfMaterial resolve_material(const Document& doc, const nlohmann::json& primitive, const ImportOptions& options) {
  GltfMaterial out{options.tex_diffuse, options.tex_normal, options.material};
  if (!primitive.contains("material")) return out;
  const auto& material = element(doc.json, "materials", primitive.at("material").get<std::size_t>());
  const auto name = material.value("name", std::string{});
  const auto label = "glTF material '" + name + "'";
  const auto pbr = material.value("pbrMetallicRoughness", nlohmann::json::object());
  const auto extras = material.value("extras", nlohmann::json::object());
  if (pbr.contains("baseColorTexture")) {
    out.diffuse = texture_slot(doc, pbr.at("baseColorTexture"), extras, "fo4DiffuseTexture", label + " base colour texture",
                               options.tex_diffuse, "tex_diffuse");
  } else if (const auto recorded = string_extra(extras, "fo4DiffuseTexture")) {
    out.diffuse = recorded_path(*recorded, "textures", label + " base colour texture");
  }
  if (material.contains("normalTexture")) {
    out.normal = texture_slot(doc, material.at("normalTexture"), extras, "fo4NormalTexture", label + " normal texture",
                              options.tex_normal, "tex_normal");
  } else if (const auto recorded = string_extra(extras, "fo4NormalTexture")) {
    out.normal = recorded_path(*recorded, "textures", label + " normal texture");
  }
  auto bgsm = string_extra(extras, "bgsm");
  if (!bgsm) bgsm = string_extra(extras, "fo4Material");
  if (!bgsm && lower(name).ends_with(".bgsm")) bgsm = name;
  if (bgsm) out.bgsm = data_path(*bgsm, "materials", label + " BGSM");
  return out;
}

void decode_skin(const Document& doc, const nlohmann::json& attributes, std::size_t skin_index, std::size_t count,
                 const std::string& shape, const Frame& frame, const BoneNames* bones, GltfPrimitive& out) {
  const auto& skin = element(doc.json, "skins", skin_index);
  const auto& joints = skin.at("joints");
  if (!attributes.contains("JOINTS_0") || !attributes.contains("WEIGHTS_0")) {
    refuse(shape, "is skinned but has no JOINTS_0 and WEIGHTS_0");
  }
  if (joints.size() > k_max_joints) {
    refuse(shape, "is skinned to " + std::to_string(joints.size()) + " joints; a NIF shape addresses at most 256");
  }
  std::vector<std::string> names;
  for (const auto& joint : joints) {
    const auto name = element(doc.json, "nodes", joint.get<std::size_t>()).value("name", std::string{});
    if (name.empty()) refuse(shape, "is skinned to a joint with no name, which cannot match a skeleton bone");
    if (bones == nullptr) refuse(shape, "is skinned to joint '" + name + "', but no skeleton NIF was chosen to bind it to");
    const auto found = bones->find(name);
    if (found == bones->end()) refuse(shape, "is skinned to joint '" + name + "', which is not a bone in the chosen skeleton");
    if (found->second > 1) {
      refuse(shape, "is skinned to joint '" + name + "', which names more than one skeleton bone");
    }
    names.push_back(name);
  }
  auto sorted_names = names;
  std::sort(sorted_names.begin(), sorted_names.end());
  const auto repeated = std::adjacent_find(sorted_names.begin(), sorted_names.end());
  if (repeated != sorted_names.end()) refuse(shape, "lists joint '" + *repeated + "' more than once");

  out.bone_binds.assign(joints.size(), Transform{});
  if (skin.contains("inverseBindMatrices")) {
    const auto matrices = read_accessor(doc, skin.at("inverseBindMatrices").get<std::size_t>(), "MAT4");
    if (matrices.size() != 16 * joints.size()) {
      refuse(shape, "has " + std::to_string(matrices.size() / 16) + " inverse bind matrices for " +
                        std::to_string(joints.size()) + " joints");
    }
    for (std::size_t j = 0; j < joints.size(); ++j) {
      Matrix m{};
      std::copy_n(matrices.begin() + static_cast<std::ptrdiff_t>(16 * j), 16, m.begin());
      out.bone_binds[j] = frame.transform(m, "inverse bind matrix of joint '" + names[j] + "'");
    }
  }

  std::vector<std::vector<double>> set_joints;
  std::vector<std::vector<double>> set_weights;
  for (std::size_t set = 0; set < 2 && attributes.contains("JOINTS_" + std::to_string(set)); ++set) {
    const auto joint_key = "JOINTS_" + std::to_string(set);
    const auto weight_key = "WEIGHTS_" + std::to_string(set);
    require_joint_type(doc, attributes, joint_key, shape);
    set_joints.push_back(attribute(doc, attributes, joint_key.c_str(), "VEC4", count, shape));
    set_weights.push_back(attribute(doc, attributes, weight_key.c_str(), "VEC4", count, shape));
  }

  Mesh& mesh = out.mesh;
  mesh.bones.assign(count, Influence{0, 0, 0, 0});
  mesh.weights.assign(count, Vec4{0.0f, 0.0f, 0.0f, 0.0f});
  for (std::size_t v = 0; v < count; ++v) {
    std::vector<std::pair<double, std::uint32_t>> candidates;
    for (std::size_t set = 0; set < set_joints.size(); ++set) {
      for (std::size_t k = 0; k < k_influences; ++k) {
        const double joint = set_joints[set][v * k_influences + k];
        if (!whole_index(joint, joints.size())) {
          refuse(shape, "has a joint index at vertex " + std::to_string(v) +
                            " that is not a whole number or is past the skin's " + std::to_string(joints.size()) + " joints");
        }
        const double weight = set_weights[set][v * k_influences + k];
        if (weight < 0.0) refuse(shape, "has a negative weight at vertex " + std::to_string(v));
        if (weight > 0.0) candidates.emplace_back(weight, static_cast<std::uint32_t>(joint));
      }
    }
    std::vector<std::uint32_t> joint_ids;
    for (const auto& candidate : candidates) joint_ids.push_back(candidate.second);
    std::sort(joint_ids.begin(), joint_ids.end());
    const auto repeated_joint = std::adjacent_find(joint_ids.begin(), joint_ids.end());
    if (repeated_joint != joint_ids.end()) {
      refuse(shape, "repeats joint " + std::to_string(*repeated_joint) + " with two positive influences at vertex " + std::to_string(v));
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    const auto kept = std::min(candidates.size(), k_influences);
    double total = 0.0;
    for (std::size_t k = 0; k < kept; ++k) total += candidates[k].first;
    if (total <= 0.0) continue;
    for (std::size_t k = 0; k < kept; ++k) {
      mesh.bones[v][k] = static_cast<std::uint8_t>(candidates[k].second);
      mesh.weights[v][k] = static_cast<float>(candidates[k].first / total);
    }
  }
  out.bone_names = std::move(names);
}

}

GltfPrimitive decode_primitive(const Document& doc, const nlohmann::json& primitive, const std::string& shape_name,
                               std::optional<std::size_t> skin, const Frame& frame, const BoneNames* bones,
                               const ImportOptions& options) {
  if (primitive.value("mode", k_triangles_mode) != k_triangles_mode) {
    refuse(shape_name, "uses a mode other than TRIANGLES, and only triangle lists are imported");
  }
  if (primitive.contains("targets")) refuse(shape_name, "has morph targets, which are not supported");
  const auto& attributes = primitive.at("attributes");
  if (!attributes.contains("POSITION")) refuse(shape_name, "has no POSITION");
  refuse_extra_sets(attributes, shape_name);

  const auto positions = read_accessor(doc, attributes.at("POSITION").get<std::size_t>(), "VEC3");
  const std::size_t count = positions.size() / 3;
  if (count > k_max_shape_vertices) {
    refuse(shape_name, "has " + std::to_string(count) + " vertices; a NIF shape holds at most 65535");
  }

  GltfPrimitive out;
  Mesh& mesh = out.mesh;
  mesh.name = shape_name;
  if (primitive.contains("indices")) {
    const auto& index_accessor = element(doc.json, "accessors", primitive.at("indices").get<std::size_t>());
    const auto index_type = index_accessor.at("componentType").get<std::uint32_t>();
    if (index_type == 5126) refuse(shape_name, "uses floating-point triangle indices");
    for (const double index : read_accessor(doc, primitive.at("indices").get<std::size_t>(), "SCALAR")) {
      if (!whole_index(index, count)) {
        refuse(shape_name, "has a triangle index that is not a finite whole number within its vertices");
      }
      mesh.tris.push_back(static_cast<std::uint32_t>(index));
    }
  } else {
    for (std::size_t i = 0; i < count; ++i) mesh.tris.push_back(static_cast<std::uint32_t>(i));
  }
  if (mesh.tris.empty()) refuse(shape_name, "has no triangles");
  if (mesh.tris.size() % 3 != 0) {
    refuse(shape_name, "has " + std::to_string(mesh.tris.size()) + " indices, which is not a whole number of triangles");
  }

  for (std::size_t i = 0; i < count; ++i) mesh.positions.push_back(frame.point(vec3_at(positions, i)));
  if (attributes.contains("NORMAL")) {
    const auto normals = attribute(doc, attributes, "NORMAL", "VEC3", count, shape_name);
    for (std::size_t i = 0; i < count; ++i) {
      const auto normal = frame.direction(vec3_at(normals, i));
      const float length = std::sqrt(dot(normal, normal));
      if (length == 0.0f) refuse(shape_name, "has a zero-length normal at vertex " + std::to_string(i));
      mesh.normals.push_back(scaled(normal, 1.0f / length));
    }
  }

  mesh.uvs.assign(count, Vec2{0.0f, 0.0f});
  if (attributes.contains("TEXCOORD_0")) {
    const auto uvs = attribute(doc, attributes, "TEXCOORD_0", "VEC2", count, shape_name);
    for (std::size_t i = 0; i < count; ++i) {
      const Vec2 uv{static_cast<float>(uvs[i * 2]), static_cast<float>(uvs[i * 2 + 1])};
      if (std::fabs(uv[0]) > k_half_max || std::fabs(uv[1]) > k_half_max) {
        refuse(shape_name, "has a texture coordinate outside the half-float range");
      }
      mesh.uvs[i] = uv;
    }
  }

  if (attributes.contains("COLOR_0")) {
    const auto type = gltf_detail::accessor_type(doc, attributes.at("COLOR_0").get<std::size_t>());
    if (type != "VEC3" && type != "VEC4") refuse(shape_name, "has COLOR_0 that is not VEC3 or VEC4");
    const auto colours = attribute(doc, attributes, "COLOR_0", type, count, shape_name);
    const auto width = components_of(type);
    mesh.colours.assign(count, Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    for (std::size_t i = 0; i < count; ++i) {
      for (std::size_t k = 0; k < width; ++k) {
        mesh.colours[i][k] = std::clamp(static_cast<float>(colours[i * width + k]), 0.0f, 1.0f);
      }
    }
  }

  if (skin) {
    decode_skin(doc, attributes, *skin, count, shape_name, frame, bones, out);
  } else if (attributes.contains("JOINTS_0") || attributes.contains("WEIGHTS_0")) {
    refuse(shape_name, "has joint or weight attributes but its node has no skin");
  }
  if (!attributes.contains("NORMAL")) split_per_face(mesh, shape_name);

  if (attributes.contains("TANGENT") && attributes.contains("NORMAL")) {
    const auto tangents = attribute(doc, attributes, "TANGENT", "VEC4", count, shape_name);
    mesh.tangents.reserve(count);
    mesh.bitangents.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto direction = frame.direction(vec3_at(tangents, i, 4));
      const float length = std::sqrt(dot(direction, direction));
      if (!(length > 0.0f) || !std::isfinite(length)) {
        refuse(shape_name, "has a zero-length or non-finite tangent at vertex " + std::to_string(i));
      }
      const float sign = tangents[i * 4 + 3] < 0.0 ? -1.0f : 1.0f;
      const auto unit_direction = scaled(direction, 1.0f / length);
      mesh.bitangents.push_back(unit_direction);
      mesh.tangents.push_back(scaled(cross(mesh.normals[i], unit_direction), sign));
    }
  } else {
    add_tangent_frame(mesh);
  }
  out.material = resolve_material(doc, primitive, options);
  return out;
}

}
