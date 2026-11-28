#pragma once

#include <nlohmann/json.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gltf_import_test {

constexpr std::uint32_t k_float = 5126;
constexpr std::uint32_t k_u8 = 5121;
constexpr std::uint32_t k_u16 = 5123;

struct Gltf {
  nlohmann::json json;
  std::vector<std::byte> bin;
};

inline Gltf empty_gltf() {
  Gltf g;
  g.json["asset"] = {{"version", "2.0"}};
  g.json["nodes"] = nlohmann::json::array();
  g.json["meshes"] = nlohmann::json::array();
  g.json["accessors"] = nlohmann::json::array();
  g.json["bufferViews"] = nlohmann::json::array();
  return g;
}

inline std::vector<std::byte> float_bytes(const std::vector<float>& values) {
  std::vector<std::byte> out;
  for (const float value : values) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (std::uint32_t shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>(bits >> shift));
  }
  return out;
}

inline std::vector<std::byte> u16_bytes(const std::vector<std::uint16_t>& values) {
  std::vector<std::byte> out;
  for (const auto value : values) {
    out.push_back(static_cast<std::byte>(value & 0xFFU));
    out.push_back(static_cast<std::byte>(value >> 8U));
  }
  return out;
}

inline std::vector<std::byte> u8_bytes(const std::vector<std::uint8_t>& values) {
  std::vector<std::byte> out;
  for (const auto value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}

// Requires type to match the element size of data; the accessor points at a new buffer view appended to the BIN chunk.
inline std::size_t add_accessor(Gltf& g, std::uint32_t component, std::size_t count, const char* type,
                                const std::vector<std::byte>& data, bool normalized = false) {
  while (g.bin.size() % 4 != 0) g.bin.push_back(std::byte{0});
  g.json["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", g.bin.size()}, {"byteLength", data.size()}});
  g.bin.insert(g.bin.end(), data.begin(), data.end());
  nlohmann::json accessor{{"bufferView", g.json["bufferViews"].size() - 1},
                          {"componentType", component},
                          {"count", count},
                          {"type", type}};
  if (normalized) accessor["normalized"] = true;
  g.json["accessors"].push_back(accessor);
  return g.json["accessors"].size() - 1;
}

inline std::size_t add_positions(Gltf& g, const std::vector<float>& xyz) {
  return add_accessor(g, k_float, xyz.size() / 3, "VEC3", float_bytes(xyz));
}

inline std::size_t add_indices(Gltf& g, const std::vector<std::uint16_t>& indices) {
  return add_accessor(g, k_u16, indices.size(), "SCALAR", u16_bytes(indices));
}

inline std::size_t add_node(Gltf& g, nlohmann::json node) {
  g.json["nodes"].push_back(std::move(node));
  return g.json["nodes"].size() - 1;
}

inline std::size_t add_mesh(Gltf& g, const std::string& name, nlohmann::json primitives) {
  g.json["meshes"].push_back({{"name", name}, {"primitives", std::move(primitives)}});
  return g.json["meshes"].size() - 1;
}

inline std::vector<std::byte> to_glb(Gltf& g) {
  if (!g.bin.empty()) g.json["buffers"] = nlohmann::json::array({{{"byteLength", g.bin.size()}}});
  std::string json = g.json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  while (json.size() % 4 != 0) json.push_back(' ');
  std::vector<std::byte> bin = g.bin;
  while (bin.size() % 4 != 0) bin.push_back(std::byte{0});
  const std::size_t total = 12 + 8 + json.size() + (bin.empty() ? 0 : 8 + bin.size());
  std::vector<std::byte> out;
  const auto put = [&out](std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>(value >> shift));
  };
  put(0x46546C67U);
  put(2);
  put(static_cast<std::uint32_t>(total));
  put(static_cast<std::uint32_t>(json.size()));
  put(0x4E4F534AU);
  for (const char ch : json) out.push_back(static_cast<std::byte>(ch));
  if (!bin.empty()) {
    put(static_cast<std::uint32_t>(bin.size()));
    put(0x004E4942U);
    out.insert(out.end(), bin.begin(), bin.end());
  }
  return out;
}

inline std::string base64(const std::vector<std::byte>& bytes) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const std::uint32_t a = std::to_integer<std::uint32_t>(bytes[i]);
    const std::uint32_t b = i + 1 < bytes.size() ? std::to_integer<std::uint32_t>(bytes[i + 1]) : 0;
    const std::uint32_t c = i + 2 < bytes.size() ? std::to_integer<std::uint32_t>(bytes[i + 2]) : 0;
    const std::uint32_t triple = (a << 16U) | (b << 8U) | c;
    out.push_back(alphabet[(triple >> 18U) & 63U]);
    out.push_back(alphabet[(triple >> 12U) & 63U]);
    out.push_back(i + 1 < bytes.size() ? alphabet[(triple >> 6U) & 63U] : '=');
    out.push_back(i + 2 < bytes.size() ? alphabet[triple & 63U] : '=');
  }
  return out;
}

// Requires the buffers of g to be data: URIs, so the returned text is self-contained glTF JSON.
inline std::string to_gltf_text(Gltf& g) {
  g.json["buffers"] = nlohmann::json::array(
      {{{"byteLength", g.bin.size()}, {"uri", "data:application/octet-stream;base64," + base64(g.bin)}}});
  return g.json.dump();
}

inline std::vector<std::byte> bytes_of(const std::string& text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(static_cast<std::byte>(ch));
  return out;
}

}
