#include "fo4/lod/tile_summary.hpp"

#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using fo4::lod::LodTileSummary;
using fo4::lod::summarize_lod_tile;
using fo4::nif::Transform;
using namespace nif_build;

namespace {

Block texture_set_block() {
  Writer writer;
  writer.u32(10);
  for (int slot = 0; slot < 10; ++slot) writer.sized("textures\\lod\\tile.dds");
  return Block{"BSShaderTextureSet", writer.bytes()};
}

Block tile_shape(std::string_view type) {
  return shape_block(std::string{type}, 0, Transform{}, k_full, 16, triangle_vertices(), {0, 1, 2});
}

LodTileSummary summarize(std::vector<std::byte> bytes) {
  return summarize_lod_tile(fo4::nif::read_nif(std::span<const std::byte>(bytes)));
}

}  // namespace

// Detects: a summary that must count both shape families and the texture set of a shipped-style tile.
TEST(LodTileSummary, CountsShapesAndTextureSets) {
  const auto bytes = build_nif({"root"},
                               {node_block("NiNode", Transform{}, {1, 2, 3}),
                                tile_shape("BSTriShape"), tile_shape("BSSubIndexTriShape"),
                                texture_set_block()},
                               {0});
  const auto summary = summarize(bytes);
  EXPECT_EQ(summary.shapes, 2u);
  EXPECT_EQ(summary.subindex_shapes, 1u);
  EXPECT_EQ(summary.texture_sets, 1u);
}

// Detects: blocks that are not geometry or texture sets, which must not be counted.
TEST(LodTileSummary, IgnoresOtherBlocks) {
  const auto bytes = build_nif({"root"}, {node_block("NiNode", Transform{}, {})}, {0});
  const auto summary = summarize(bytes);
  EXPECT_EQ(summary, (LodTileSummary{0, 0, 0}));
}

// Detects: the measured maximum of twelve shapes in one tile, which must count all of them.
TEST(LodTileSummary, CountsTheMeasuredTileMaximum) {
  std::vector<Block> blocks;
  blocks.push_back(node_block("BSMultiBoundNode", Transform{}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
  for (int index = 0; index < 12; ++index) blocks.push_back(tile_shape("BSSubIndexTriShape"));
  const auto bytes = build_nif({"root"}, blocks, {0});
  const auto summary = summarize(bytes);
  EXPECT_EQ(summary.shapes, 12u);
  EXPECT_EQ(summary.subindex_shapes, 12u);
  EXPECT_EQ(summary.texture_sets, 0u);
}
