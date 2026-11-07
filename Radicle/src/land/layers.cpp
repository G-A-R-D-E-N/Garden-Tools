#include "fo4/land/layers.hpp"

#include "fo4/error.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::land {
namespace {

std::uint16_t read_u16(std::span<const std::byte> payload, std::size_t offset) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(payload[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(payload[offset + 1])) << 8u);
}

std::uint32_t read_u32(std::span<const std::byte> payload, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(payload[offset + index]))
             << (8u * index);
  }
  return value;
}

float read_f32(std::span<const std::byte> payload, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, payload.data() + offset, sizeof(value));
  return value;
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  append_u16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
  append_u16(out, static_cast<std::uint16_t>((value >> 16u) & 0xFFFFu));
}

void append_f32(std::vector<std::byte>& out, float value) {
  std::array<std::byte, sizeof(float)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(value));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

}  // namespace

LandLayerHeader parse_land_layer_header(std::span<const std::byte> payload) {
  if (payload.size() != k_layer_header_size) {
    throw parse_error(0, "land layer header is not 8 bytes");
  }
  LandLayerHeader header;
  header.texture = read_u32(payload, 0);
  header.quadrant = std::to_integer<std::uint8_t>(payload[4]);
  header.unused = std::to_integer<std::uint8_t>(payload[5]);
  header.layer = static_cast<std::int16_t>(read_u16(payload, 6));
  return header;
}

std::array<std::byte, k_layer_header_size> encode_land_layer_header(const LandLayerHeader& header) {
  std::array<std::byte, k_layer_header_size> out{};
  std::vector<std::byte> bytes;
  bytes.reserve(k_layer_header_size);
  append_u32(bytes, header.texture);
  bytes.push_back(static_cast<std::byte>(header.quadrant));
  bytes.push_back(static_cast<std::byte>(header.unused));
  append_u16(bytes, static_cast<std::uint16_t>(header.layer));
  for (std::size_t index = 0; index < k_layer_header_size; ++index) {
    out[index] = bytes[index];
  }
  return out;
}

std::vector<LandLayerVertex> parse_land_layer_vertices(std::span<const std::byte> payload) {
  if ((payload.size() % k_layer_vertex_size) != 0) {
    throw parse_error(0, "land layer vertices are not a whole number of 8-byte entries");
  }
  std::vector<LandLayerVertex> vertices;
  vertices.reserve(payload.size() / k_layer_vertex_size);
  for (std::size_t offset = 0; offset + k_layer_vertex_size <= payload.size();
       offset += k_layer_vertex_size) {
    LandLayerVertex vertex;
    vertex.position = read_u16(payload, offset);
    vertex.unused = read_u16(payload, offset + 2);
    vertex.opacity = read_f32(payload, offset + 4);
    if (!std::isfinite(vertex.opacity)) {
      throw parse_error(offset, "land layer vertex opacity is not finite");
    }
    vertices.push_back(vertex);
  }
  return vertices;
}

std::vector<std::byte> encode_land_layer_vertices(std::span<const LandLayerVertex> vertices) {
  std::vector<std::byte> out;
  out.reserve(vertices.size() * k_layer_vertex_size);
  for (const auto& vertex : vertices) {
    if (!std::isfinite(vertex.opacity)) {
      throw std::invalid_argument("land layer vertex opacity is not finite");
    }
    append_u16(out, vertex.position);
    append_u16(out, vertex.unused);
    append_f32(out, vertex.opacity);
  }
  return out;
}

std::array<LandColor, k_land_color_count> parse_land_colors(std::span<const std::byte> payload) {
  if (payload.size() != k_land_color_size) {
    throw parse_error(0, "land color payload is not 3267 bytes");
  }
  std::array<LandColor, k_land_color_count> colors{};
  for (std::size_t index = 0; index < k_land_color_count; ++index) {
    colors[index].red = std::to_integer<std::uint8_t>(payload[index * 3]);
    colors[index].green = std::to_integer<std::uint8_t>(payload[index * 3 + 1]);
    colors[index].blue = std::to_integer<std::uint8_t>(payload[index * 3 + 2]);
  }
  return colors;
}

std::array<std::byte, k_land_color_size> encode_land_colors(std::span<const LandColor> colors) {
  std::array<std::byte, k_land_color_size> out{};
  for (std::size_t index = 0; index < k_land_color_count && index < colors.size(); ++index) {
    out[index * 3] = static_cast<std::byte>(colors[index].red);
    out[index * 3 + 1] = static_cast<std::byte>(colors[index].green);
    out[index * 3 + 2] = static_cast<std::byte>(colors[index].blue);
  }
  return out;
}

}  // namespace fo4::land
