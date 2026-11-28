#pragma once

#include "gltf_glb_reader.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gltf_test {

inline std::size_t component_bytes(std::uint64_t component) {
  switch (component) {
    case 5120:
    case 5121: return 1;
    case 5122:
    case 5123: return 2;
    case 5125:
    case 5126: return 4;
    default: return 0;
  }
}

inline std::size_t element_count(const std::string& type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4" || type == "MAT2") return 4;
  if (type == "MAT3") return 9;
  if (type == "MAT4") return 16;
  return 0;
}

inline bool valid_index(const nlohmann::json& list, const nlohmann::json& index) {
  return index.is_number_unsigned() && index.get<std::size_t>() < list.size();
}

inline std::vector<std::string> validate_glb(const Glb& glb) {
  std::vector<std::string> faults;
  const auto fault = [&faults](std::string text) { faults.push_back(std::move(text)); };
  const auto& doc = glb.json;
  const auto list = [&doc](const char* key) { return doc.value(key, nlohmann::json::array()); };
  const auto views = list("bufferViews");
  const auto accessors = list("accessors");
  const auto nodes = list("nodes");
  const auto meshes = list("meshes");
  const auto skins = list("skins");
  const auto materials = list("materials");
  const auto textures = list("textures");
  const auto images = list("images");
  const std::size_t bin = glb.bin.size();

  for (const auto& buffer : list("buffers")) {
    if (buffer.value("byteLength", std::size_t{0}) > bin) fault("buffer is longer than the BIN chunk");
  }
  for (std::size_t i = 0; i < views.size(); ++i) {
    const auto& view = views[i];
    const auto end = view.value("byteOffset", std::size_t{0}) + view.value("byteLength", std::size_t{0});
    if (view.value("buffer", std::size_t{0}) != 0 || end > bin) fault("bufferView " + std::to_string(i) + " runs past the BIN chunk");
  }
  for (std::size_t i = 0; i < accessors.size(); ++i) {
    const auto& accessor = accessors[i];
    const std::string where = "accessor " + std::to_string(i);
    if (!valid_index(views, accessor.value("bufferView", nlohmann::json{}))) {
      fault(where + " has no valid bufferView");
      continue;
    }
    const auto& view = views[accessor.at("bufferView").get<std::size_t>()];
    const auto size = component_bytes(accessor.value("componentType", std::uint64_t{0}));
    const auto components = element_count(accessor.value("type", std::string{}));
    const auto count = accessor.value("count", std::size_t{0});
    if (size == 0 || components == 0) fault(where + " has an unknown component type or accessor type");
    if (count == 0) fault(where + " has no elements");
    if (size == 0 || components == 0 || count == 0) continue;
    const auto accessor_offset = accessor.value("byteOffset", std::size_t{0});
    if (accessor_offset % size != 0) fault(where + " is not aligned to its component size");
    const auto view_offset = view.value("byteOffset", std::size_t{0});
    const auto view_end = view_offset + view.value("byteLength", std::size_t{0});
    if (view_offset + accessor_offset + count * size * components > view_end) fault(where + " runs past its bufferView");
  }

  for (std::size_t m = 0; m < meshes.size(); ++m) {
    for (const auto& primitive : meshes[m].at("primitives")) {
      const auto& attributes = primitive.at("attributes");
      if (!attributes.contains("POSITION")) fault("primitive of mesh " + std::to_string(m) + " has no POSITION");
      std::size_t vertices = 0;
      if (attributes.contains("POSITION") && valid_index(accessors, attributes.at("POSITION"))) {
        vertices = accessors[attributes.at("POSITION").get<std::size_t>()].at("count").get<std::size_t>();
      }
      for (const auto& [semantic, index] : attributes.items()) {
        if (!valid_index(accessors, index)) {
          fault(semantic + " of mesh " + std::to_string(m) + " has no valid accessor");
          continue;
        }
        const auto& accessor = accessors[index.get<std::size_t>()];
        if (accessor.at("count").get<std::size_t>() != vertices) fault(semantic + " count differs from POSITION");
        const auto type = accessor.at("type").get<std::string>();
        const auto component = accessor.at("componentType").get<std::uint64_t>();
        const bool component_ok = semantic.starts_with("JOINTS_") ? component == 5121 : component == 5126;
        const std::string expected = semantic == "POSITION" || semantic == "NORMAL" ? "VEC3"
                                     : semantic.starts_with("TEXCOORD_")             ? "VEC2"
                                                                                     : "VEC4";
        if (!component_ok || type != expected) fault(semantic + " of mesh " + std::to_string(m) + " has the wrong type");
      }
      if (primitive.contains("material") && !valid_index(materials, primitive.at("material"))) {
        fault("mesh " + std::to_string(m) + " names a material that does not exist");
      }
      if (!primitive.contains("indices") || !valid_index(accessors, primitive.at("indices"))) {
        fault("mesh " + std::to_string(m) + " has no valid index accessor");
        continue;
      }
      const auto index_accessor = primitive.at("indices").get<std::size_t>();
      const auto& indices = accessors[index_accessor];
      if (indices.at("type").get<std::string>() != "SCALAR" || indices.at("count").get<std::size_t>() % 3 != 0) {
        fault("mesh " + std::to_string(m) + " has indices that are not whole triangles");
      }
      for (const double value : read_accessor(glb, index_accessor)) {
        if (value >= static_cast<double>(vertices)) fault("mesh " + std::to_string(m) + " indexes past its vertices");
      }
    }
  }

  for (std::size_t n = 0; n < nodes.size(); ++n) {
    const auto& node = nodes[n];
    for (const auto& child : node.value("children", nlohmann::json::array())) {
      if (!valid_index(nodes, child)) fault("node " + std::to_string(n) + " has a child that does not exist");
    }
    if (node.contains("mesh") && !valid_index(meshes, node.at("mesh"))) fault("node " + std::to_string(n) + " names a missing mesh");
    if (node.contains("skin") && !valid_index(skins, node.at("skin"))) fault("node " + std::to_string(n) + " names a missing skin");
    if (node.contains("matrix") && node.at("matrix").size() != 16) fault("node " + std::to_string(n) + " matrix is not 16 values");
  }

  for (std::size_t s = 0; s < skins.size(); ++s) {
    const auto& skin = skins[s];
    const auto joints = skin.value("joints", nlohmann::json::array());
    for (const auto& joint : joints) {
      if (!valid_index(nodes, joint)) fault("skin " + std::to_string(s) + " names a joint node that does not exist");
    }
    const auto ibm = skin.value("inverseBindMatrices", nlohmann::json{});
    if (!valid_index(accessors, ibm) || accessors[ibm.get<std::size_t>()].at("count").get<std::size_t>() != joints.size() ||
        accessors[ibm.get<std::size_t>()].at("type").get<std::string>() != "MAT4") {
      fault("skin " + std::to_string(s) + " has inverse binds that do not match its joints");
    }
  }

  for (std::size_t m = 0; m < materials.size(); ++m) {
    const auto& material = materials[m];
    const auto& pbr = material.value("pbrMetallicRoughness", nlohmann::json::object());
    if (pbr.contains("baseColorTexture") && !valid_index(textures, pbr.at("baseColorTexture").at("index"))) {
      fault("material " + std::to_string(m) + " names a missing baseColor texture");
    }
    if (material.contains("normalTexture") && !valid_index(textures, material.at("normalTexture").at("index"))) {
      fault("material " + std::to_string(m) + " names a missing normal texture");
    }
    const auto extras = material.value("extras", nlohmann::json::object());
    if (extras.contains("fo4SmoothSpecTexture") && !valid_index(textures, extras.at("fo4SmoothSpecTexture").at("index"))) {
      fault("material " + std::to_string(m) + " names a missing smoothness texture");
    }
    const auto mode = material.value("alphaMode", std::string{"OPAQUE"});
    if (mode != "OPAQUE" && mode != "MASK" && mode != "BLEND") fault("material " + std::to_string(m) + " has an unknown alphaMode");
    if (material.contains("alphaCutoff") && material.at("alphaCutoff").get<double>() < 0.0) {
      fault("material " + std::to_string(m) + " has a negative alphaCutoff");
    }
  }

  for (std::size_t t = 0; t < textures.size(); ++t) {
    if (!valid_index(images, textures[t].value("source", nlohmann::json{}))) fault("texture " + std::to_string(t) + " names a missing image");
  }

  for (std::size_t i = 0; i < images.size(); ++i) {
    const auto& image = images[i];
    const bool has_uri = image.contains("uri");
    const bool has_view = image.contains("bufferView");
    if (has_uri == has_view) fault("image " + std::to_string(i) + " needs exactly one of uri or bufferView");
    if (has_view) {
      if (!valid_index(views, image.at("bufferView"))) fault("image " + std::to_string(i) + " names a missing bufferView");
      if (!image.contains("mimeType")) fault("image " + std::to_string(i) + " has a bufferView but no mimeType");
    }
  }

  for (const auto& scene : list("scenes")) {
    for (const auto& root : scene.value("nodes", nlohmann::json::array())) {
      if (!valid_index(nodes, root)) fault("scene names a node that does not exist");
    }
  }
  return faults;
}

}  // namespace gltf_test
