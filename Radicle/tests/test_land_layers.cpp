#include "fo4/land/layers.hpp"

#include "fo4/error.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

using fo4::land::LandColor;
using fo4::land::LandLayerHeader;
using fo4::land::LandLayerVertex;
using fo4::land::encode_land_colors;
using fo4::land::encode_land_layer_header;
using fo4::land::encode_land_layer_vertices;
using fo4::land::parse_land_colors;
using fo4::land::parse_land_layer_header;
using fo4::land::parse_land_layer_vertices;

namespace {

std::array<std::byte, fo4::land::k_layer_header_size> header_payload(std::uint32_t texture) {
  std::array<std::byte, fo4::land::k_layer_header_size> bytes{};
  bytes[0] = static_cast<std::byte>(texture & 0xFFu);
  bytes[1] = static_cast<std::byte>((texture >> 8u) & 0xFFu);
  bytes[2] = static_cast<std::byte>((texture >> 16u) & 0xFFu);
  bytes[3] = static_cast<std::byte>((texture >> 24u) & 0xFFu);
  bytes[4] = std::byte{0x03};
  bytes[5] = std::byte{0xAB};
  bytes[6] = std::byte{0xFF};
  bytes[7] = std::byte{0xFF};
  return bytes;
}

std::vector<std::byte> vertex_payload(std::uint16_t position, float opacity) {
  std::vector<std::byte> bytes(fo4::land::k_layer_vertex_size, std::byte{0});
  bytes[0] = static_cast<std::byte>(position & 0xFFu);
  bytes[1] = static_cast<std::byte>((position >> 8u) & 0xFFu);
  std::memcpy(bytes.data() + 4, &opacity, sizeof(opacity));
  return bytes;
}

}  // namespace

// Detects: a header reader that must split the measured 8-byte layout and keep the unused byte.
TEST(LandLayers, ParsesTheMeasuredHeader) {
  const auto bytes = header_payload(0x0001A2B3u);
  const auto header = parse_land_layer_header(bytes);
  EXPECT_EQ(header.texture, 0x0001A2B3u);
  EXPECT_EQ(header.quadrant, 3u);
  EXPECT_EQ(header.unused, 0xABu);
  EXPECT_EQ(header.layer, -1);
  EXPECT_EQ(encode_land_layer_header(header), bytes);
}

// Detects: width checks that must refuse anything other than the measured 8-byte header.
TEST(LandLayers, RejectsHeaderPayloadsOfAnotherWidth) {
  for (const std::size_t size : {std::size_t{0}, std::size_t{7}, std::size_t{9}}) {
    std::vector<std::byte> bytes(size, std::byte{0});
    EXPECT_THROW(parse_land_layer_header(bytes), fo4::parse_error) << size;
  }
}

// Detects: a vertex reader that must read the measured position and opacity pairs.
TEST(LandLayers, ParsesLayerVertices) {
  std::vector<std::byte> bytes;
  for (const auto& entry : {vertex_payload(0, 1.0f), vertex_payload(288, 0.003921568859368563f)}) {
    bytes.insert(bytes.end(), entry.begin(), entry.end());
  }
  const auto vertices = parse_land_layer_vertices(bytes);
  ASSERT_EQ(vertices.size(), 2u);
  EXPECT_EQ(vertices[0].position, 0u);
  EXPECT_EQ(vertices[0].opacity, 1.0f);
  EXPECT_EQ(vertices[1].position, 288u);
  EXPECT_FLOAT_EQ(vertices[1].opacity, 0.003921568859368563f);
  EXPECT_EQ(encode_land_layer_vertices(vertices), bytes);
}

// Detects: an empty vertex payload that must be accepted, because a layer may carry no vertices.
TEST(LandLayers, HandlesAnEmptyVertexPayload) {
  EXPECT_TRUE(parse_land_layer_vertices({}).empty());
  EXPECT_TRUE(encode_land_layer_vertices({}).empty());
}

// Detects: a vertex payload that is not a whole number of 8-byte entries, which must be refused.
TEST(LandLayers, RejectsVertexPayloadsThatAreNotWholeEntries) {
  for (const std::size_t size : {std::size_t{1}, std::size_t{7}, std::size_t{15}}) {
    std::vector<std::byte> bytes(size, std::byte{0});
    EXPECT_THROW(parse_land_layer_vertices(bytes), fo4::parse_error) << size;
  }
}

// Detects: a non-finite opacity, which must be refused on both the read and the write path.
TEST(LandLayers, RejectsNonFiniteOpacity) {
  auto bytes = vertex_payload(5, 0.5f);
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(bytes.data() + 4, &nan, sizeof(nan));
  EXPECT_THROW(parse_land_layer_vertices(bytes), fo4::parse_error);
  std::vector<LandLayerVertex> vertices{{5, 0, nan}};
  EXPECT_THROW(encode_land_layer_vertices(vertices), std::invalid_argument);
}

// Detects: colors that must round-trip the measured 3267-byte payload (1089 RGB triples).
TEST(LandLayers, ParsesAndRoundTripsColors) {
  std::array<std::byte, fo4::land::k_land_color_size> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>(index & 0xFFu);
  }
  const auto colors = parse_land_colors(bytes);
  EXPECT_EQ(colors[0], (LandColor{0, 1, 2}));
  EXPECT_EQ(colors[1088], (LandColor{static_cast<std::uint8_t>(3264 & 0xFFu), static_cast<std::uint8_t>(3265 & 0xFFu), static_cast<std::uint8_t>(3266 & 0xFFu)}));
  EXPECT_EQ(encode_land_colors(colors), bytes);
}

// Detects: color payloads of any other width, which must be refused.
TEST(LandLayers, RejectsColorPayloadsOfAnotherWidth) {
  for (const std::size_t size : {std::size_t{0}, std::size_t{3266}, std::size_t{3268}}) {
    std::vector<std::byte> bytes(size, std::byte{0});
    EXPECT_THROW(parse_land_colors(bytes), fo4::parse_error) << size;
  }
}
