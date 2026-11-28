#pragma once

#include <nlohmann/json.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gltf_test {

struct Glb {
  nlohmann::json json;
  std::vector<std::byte> bin;
};

inline std::uint32_t u32_at(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(at + i))) << (8U * i);
  }
  return value;
}

inline Glb parse_glb(const std::vector<std::byte>& bytes) {
  if (bytes.size() < 20 || u32_at(bytes, 0) != 0x46546C67U || u32_at(bytes, 4) != 2 ||
      u32_at(bytes, 8) != bytes.size() || u32_at(bytes, 16) != 0x4E4F534AU) {
    throw std::runtime_error("not a binary glTF 2.0 container with a JSON chunk first");
  }
  const std::size_t json_length = u32_at(bytes, 12);
  if (20 + json_length > bytes.size()) throw std::runtime_error("JSON chunk runs past the end of the file");
  std::string text;
  for (std::size_t i = 0; i < json_length; ++i) text.push_back(static_cast<char>(bytes[20 + i]));
  Glb glb;
  glb.json = nlohmann::json::parse(text);
  const std::size_t at = 20 + json_length;
  if (at < bytes.size()) {
    const std::size_t length = u32_at(bytes, at);
    if (u32_at(bytes, at + 4) != 0x004E4942U || at + 8 + length > bytes.size()) {
      throw std::runtime_error("second chunk is not a complete BIN chunk");
    }
    glb.bin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at + 8),
                   bytes.begin() + static_cast<std::ptrdiff_t>(at + 8 + length));
  }
  return glb;
}

inline std::size_t components_of(const std::string& type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  throw std::runtime_error("unknown accessor type " + type);
}

inline std::vector<double> read_accessor(const Glb& glb, std::size_t index) {
  const auto& accessor = glb.json.at("accessors").at(index);
  const auto& view = glb.json.at("bufferViews").at(accessor.at("bufferView").get<std::size_t>());
  const auto component = accessor.at("componentType").get<std::uint32_t>();
  const std::size_t width = component == 5126 ? 4 : (component == 5123 ? 2 : 1);
  const std::size_t base = view.value("byteOffset", std::size_t{0}) + accessor.value("byteOffset", std::size_t{0});
  const std::size_t total = accessor.at("count").get<std::size_t>() * components_of(accessor.at("type").get<std::string>());
  std::vector<double> out;
  out.reserve(total);
  for (std::size_t i = 0; i < total; ++i) {
    const std::size_t at = base + i * width;
    if (component == 5126) {
      out.push_back(std::bit_cast<float>(u32_at(glb.bin, at)));
    } else if (component == 5123) {
      out.push_back(static_cast<double>(std::to_integer<std::uint8_t>(glb.bin.at(at)) |
                                        (std::to_integer<std::uint8_t>(glb.bin.at(at + 1)) << 8U)));
    } else {
      out.push_back(static_cast<double>(std::to_integer<std::uint8_t>(glb.bin.at(at))));
    }
  }
  return out;
}

}  // namespace gltf_test
