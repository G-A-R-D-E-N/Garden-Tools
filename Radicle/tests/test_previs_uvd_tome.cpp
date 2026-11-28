#include "fo4/error.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/uvd_tile_body.hpp"
#include "fo4/previs/uvd_tile_build.hpp"
#include "fo4/previs/uvd_tile_check.hpp"
#include "fo4/previs/uvd_tile_header.hpp"
#include "fo4/previs/uvd_tile_model.hpp"
#include "fo4/previs/uvd_tome.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Regression: a generated tome must validate and read back the geometry written into it.
TEST(PrevisUvdTome, WritesATomeItsOwnReaderAccepts) {
  fo4::previs::UvdTomeRequest request;
  request.world_min[0] = -512.0f; request.world_min[1] = -512.0f; request.world_min[2] = 0.0f;
  request.world_max[0] = 512.0f; request.world_max[1] = 512.0f; request.world_max[2] = 256.0f;
  request.banner = "T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0";
  fo4::previs::UvdPage page;
  for (int axis = 0; axis < 3; ++axis) {
    page.min[axis] = request.world_min[axis];
    page.max[axis] = request.world_max[axis];
  }
  page.positions = {-512.0f, -512.0f, 0.0f, 512.0f, -512.0f, 0.0f,
                    512.0f,  512.0f,  0.0f, -512.0f, 512.0f, 0.0f};
  page.indices = {0, 1, 2, 0, 2, 3};
  page.leaves.push_back(fo4::previs::UvdLeaf{2, 1});
  request.pages.push_back(page);

  const auto bytes = fo4::previs::write_uvd_tome(request);
  EXPECT_TRUE(fo4::previs::validate_uvd(bytes).empty());
  ASSERT_EQ(bytes.size() % 16, 0u);

  const auto u32 = [&bytes](std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, 4);
    return value;
  };
  EXPECT_EQ(u32(8), bytes.size());
  for (const auto field : {0x30u, 0x3Cu, 0x80u, 0x88u, 0x98u, 0x9Cu, 0xA0u, 0xA4u, 0xA8u}) {
    EXPECT_EQ(u32(field) % 16, 0u) << "offset at 0x" << std::hex << field << " is not 16-aligned";
    EXPECT_LT(u32(field), bytes.size()) << "offset at 0x" << std::hex << field << " is outside the file";
  }
  const auto page_at = u32(u32(0xA0));
  std::uint32_t vertex_count = 0;
  std::uint32_t index_count = 0;
  std::memcpy(&vertex_count, bytes.data() + page_at + 0x34, 4);
  std::memcpy(&index_count, bytes.data() + page_at + 0x3C, 4);
  EXPECT_EQ(vertex_count, 4u);
  EXPECT_EQ(index_count, 6u);
  // Constraint: the page payload re-decodes to the bounds it was handed, or the tome describes a different cell.
  float bound_min = 0.0f;
  // Constraint: retail bounds start at the page base, so the first float is the minimum x.
  std::memcpy(&bound_min, bytes.data() + page_at, 4);
  EXPECT_FLOAT_EQ(bound_min, -512.0f);
  std::int16_t first_vertex = 0;
  std::memcpy(&first_vertex, bytes.data() + page_at + 0x50, 2);
  EXPECT_EQ(first_vertex, -16384) << "the first vertex must sit on the page's minimum x";
}

// Regression: a page patch must rewrite exactly one payload.
TEST(PrevisUvdTome, PatchesOnePageAndLeavesTheOthersByteIdentical) {
  fo4::previs::UvdTomeRequest request;
  request.world_min[0] = -1024.0f; request.world_min[1] = -1024.0f; request.world_min[2] = 0.0f;
  request.world_max[0] = 1024.0f; request.world_max[1] = 1024.0f; request.world_max[2] = 512.0f;
  for (int index = 0; index < 3; ++index) {
    fo4::previs::UvdPage page;
    const float base = static_cast<float>(index) * 256.0f - 768.0f;
    for (int axis = 0; axis < 3; ++axis) {
      page.min[axis] = base;
      page.max[axis] = base + 128.0f + static_cast<float>(axis) * 64.0f;
    }
    page.positions = {base, base, base, base + 128.0f, base, base + 64.0f,
                      base + 128.0f, base + 128.0f, base + 128.0f, base, base + 128.0f, base + 192.0f};
    page.indices = {0, 1, 2, 0, 2, 3};
    page.leaves.push_back(fo4::previs::UvdLeaf{6, 1});
    request.pages.push_back(page);
  }
  const auto bytes = fo4::previs::write_uvd_tome(request);
  ASSERT_TRUE(fo4::previs::validate_uvd(bytes).empty());

  fo4::previs::UvdPage replacement;
  replacement.min[0] = 0.0f; replacement.min[1] = 0.0f; replacement.min[2] = 32.0f;
  replacement.max[0] = 64.0f; replacement.max[1] = 64.0f; replacement.max[2] = 48.0f;
  replacement.positions = {0.0f, 0.0f, 32.0f, 64.0f, 0.0f, 32.0f, 0.0f, 64.0f, 48.0f};
  replacement.indices = {0, 1, 2};

  const auto patched = fo4::previs::patch_uvd_page(bytes, 1, replacement);
  ASSERT_TRUE(fo4::previs::validate_uvd(patched).empty());
  ASSERT_EQ(patched.size() % 16, 0u);

  const auto read = fo4::previs::read_uvd_page(patched, 1);
  ASSERT_TRUE(read.has_value());
  ASSERT_EQ(read->indices.size(), replacement.indices.size());
  ASSERT_EQ(read->positions.size(), replacement.positions.size());
  for (std::size_t i = 0; i < replacement.positions.size(); ++i) {
    EXPECT_NEAR(read->positions[i], replacement.positions[i], 0.05f) << "vertex " << i / 3 << " axis " << i % 3;
  }
  EXPECT_EQ(read->indices, replacement.indices);

  const auto u32_of = [](std::span<const std::byte> image, std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, image.data() + at, 4);
    return value;
  };
  const auto offset_of = [&](std::span<const std::byte> image, std::size_t page) {
    return static_cast<std::size_t>(u32_of(image, u32_of(image, 0xA0) + page * 4));
  };
  const auto length_of = [&](std::span<const std::byte> image, std::size_t page) {
    return static_cast<std::size_t>(u32_of(image, offset_of(image, page) + 0x2C) >> 8);
  };
  for (const std::size_t page : {std::size_t{0}, std::size_t{2}}) {
    ASSERT_EQ(length_of(bytes, page), length_of(patched, page)) << "page " << page << " changed length";
    EXPECT_EQ(std::memcmp(bytes.data() + offset_of(bytes, page), patched.data() + offset_of(patched, page),
                          length_of(bytes, page)),
              0) << "page " << page << " payload moved";
  }
  // Constraint: the replacement is smaller than the quad it replaces, so the patched payload is its own aligned size.
  EXPECT_EQ(length_of(patched, 1), 112u);
  EXPECT_LT(patched.size(), bytes.size());
  EXPECT_EQ(u32_of(patched, 8), static_cast<std::uint32_t>(patched.size()));
}


namespace {

using namespace fo4::previs;
using TileBytes = std::vector<std::byte>;

constexpr std::array<std::uint16_t, 2> k_full{0, 0xffff};
constexpr float k_flt_max = std::numeric_limits<float>::max();

std::uint32_t get32(const TileBytes& bytes, std::size_t at) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + at, 4);
  return value;
}

void put32(TileBytes& bytes, std::size_t at, std::uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); }

void reseal(TileBytes& bytes) { put32(bytes, 4, crc32c(std::span<const std::byte>(bytes).subspan(8))); }

UvdCell tile_cell(std::uint32_t start, std::uint32_t count) {
  UvdCell c;
  c.portal_start = start;
  c.portal_count = count;
  c.list_count = 1;
  c.x = k_full;
  c.y = k_full;
  c.z = k_full;
  return c;
}

UvdPortal tile_portal(std::uint32_t target, std::uint32_t face) {
  UvdPortal p;
  p.target = target;
  p.face = face;
  p.plane = (face & 1u) != 0 ? 0xffff : 0;
  p.rect0 = k_full;
  p.rect1 = k_full;
  return p;
}

UvdTileBody synthetic_tile(const std::array<float, 6>& box, bool leaf, std::vector<UvdCell> cells, std::vector<UvdPortal> portals) {
  UvdTileBody t;
  t.box = box;
  t.leaf = leaf;
  t.expand = 128.0f;
  t.cluster_count = 1;
  t.cells = std::move(cells);
  t.portals = std::move(portals);
  if (leaf) {
    UvdKd kd;
    kd.node_count = 1;
    kd.map_width = 1;
    kd.symbols = {3};
    kd.entries = {0};
    t.kd = kd;
  }
  return t;
}

// Constraint: mirrors tools/previs/uvd_tiles_selftest.mjs:syntheticTome, one interior tile and two leaf tiles built through the typed encoders.
TileBytes synthetic_tome(bool extra_object = false) {
  UvdBodies bodies;
  bodies.slots.push_back(synthetic_tile({0, 0, 0, 1024, 1024, 256}, false, {tile_cell(0, 1), tile_cell(1, 1)}, {tile_portal(1, 1), tile_portal(2, 0)}));
  bodies.slots.push_back(synthetic_tile({0, 0, 0, 512, 1024, 256}, true, {tile_cell(0, 1)}, {tile_portal(2, 1)}));
  bodies.slots.push_back(synthetic_tile({512, 0, 0, 1024, 1024, 256}, true, {tile_cell(0, 1)}, {tile_portal(1, 0)}));
  std::vector<std::array<float, 6>> boxes{{0, 0, 0, 100, 100, 50}, {200, 0, 0, 300, 100, 60}};
  if (extra_object) {
    boxes.push_back({400, 0, 0, 500, 100, 70});
  }
  UvdHeader h;
  h.banner = "T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0";
  h.lod_base = 512.0f;
  h.world_max = {1024, 1024, 256};
  h.widths = {3, 2, 0, 0};
  h.top_symbols = {0, 3, 3};
  h.top_splits = {512.0f, k_flt_max, k_flt_max};
  h.lod = {20.0f, 15.0f, 15.0f};
  h.objects.boxes = boxes;
  h.objects.distances.emplace();
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    const auto& b = boxes[i];
    h.objects.distances->push_back({b[0], b[1], b[2], 0.0f, b[3], b[4], b[5], k_flt_max});
    h.objects.ids.push_back(0x01000a74u + static_cast<std::uint32_t>(i));
  }
  h.run_a = {{1, 1}, {2, 0}};
  UvdCluster cluster;
  cluster.x = k_full;
  cluster.y = k_full;
  cluster.z = k_full;
  h.clusters = {cluster};
  UvdKd tree;
  tree.node_count = 3;
  tree.map_width = 2;
  tree.symbols = {0, 3, 3};
  tree.splits = {512.0f};
  tree.entries = {0, 1};
  h.child_trees = {tree};
  UvdLeafRowBytes with_block;
  with_block.words = {3, 1, 1};
  with_block.block.assign(16, std::byte{0x5a});
  with_block.trees_before = 1;
  h.child_rows = {with_block, UvdLeafRowBytes{}};
  return write_uvd_tiles(encode_uvd_header(h, encode_uvd_bodies(bodies, UvdTileModel{})));
}

std::string rejection(TileBytes bytes, const std::function<void(TileBytes&)>& mutate, bool seal) {
  mutate(bytes);
  if (seal) {
    reseal(bytes);
  }
  try {
    const auto model = parse_uvd_tiles(bytes);
    std::string joined;
    for (const auto& v : validate_uvd_tiles(model)) joined += v.claim + "; ";
    return joined;
  } catch (const UvdTileLayoutError& e) {
    return e.reason();
  }
}

}  // namespace

// Regression: a writer that copies stale offsets or mis-derives slot paths, cell starts, leaf counts or the CRC from the model, or a parser that misreads a region, breaks this byte-for-byte round trip.
TEST(PrevisUvdTileModel, SyntheticTomeRoundTripsByteForByte) {
  const auto bytes = synthetic_tome();
  ASSERT_TRUE(validate_uvd(bytes).empty());
  const auto model = parse_uvd_tiles(bytes);
  EXPECT_EQ(write_uvd_tiles(model), bytes);
  const auto stages = uvd_tile_round_trips(bytes, model);
  EXPECT_TRUE(stages.stage1);
  EXPECT_TRUE(stages.stage2);
  EXPECT_TRUE(stages.stage3);
  EXPECT_TRUE(validate_uvd_tiles(model).empty());
  EXPECT_EQ(get32(bytes, 0x8c), 2u);
  ASSERT_EQ(model.slots.size(), 3u);
  EXPECT_FALSE(model.slots[0]->leaf);
  EXPECT_TRUE(model.slots[2]->leaf);
  EXPECT_EQ(model.child.trees.size(), 1u);
  EXPECT_EQ(model.path_bits, 2u);
  ASSERT_EQ(model.slot_paths.size(), 1u);
  EXPECT_EQ(static_cast<int>(model.slot_paths[0]), 0x1e);
  EXPECT_EQ(static_cast<int>(model.top_kd.nodes[0]), 0x3c);
  EXPECT_EQ(get32(model.cell_starts, 0), 0u);
  EXPECT_EQ(get32(model.cell_starts, 4), 2u);
  EXPECT_EQ(get32(model.cell_starts, 8), 3u);
  EXPECT_EQ(get32(model.cell_starts, 12), 4u);
}

// Regression: growing the model must move every later offset and size and still parse, as a writer that kept the old tile table offset would not.
TEST(PrevisUvdTileModel, GrowingTheModelRederivesOffsetsAndCounts) {
  const auto small = synthetic_tome();
  const auto grown = synthetic_tome(true);
  EXPECT_EQ(get32(small, 0x40), 2u);
  EXPECT_EQ(get32(grown, 0x40), 3u);
  EXPECT_GT(get32(grown, 0xa0), get32(small, 0xa0));
  EXPECT_GT(grown.size(), small.size());
  EXPECT_EQ(get32(grown, 8), grown.size());
  EXPECT_TRUE(validate_uvd_tiles(parse_uvd_tiles(grown)).empty());
}

// Regression: a parser or writer that tolerates a gap, wrong size, bad CRC or inconsistent derived field accepts a corrupted tome; each variant must be refused with its own reason.
TEST(PrevisUvdTileModel, RejectsCorruptedTomesWithAReason) {
  const auto bytes = synthetic_tome();
  const std::size_t tile1 = get32(bytes, get32(bytes, 0xa0) + 4);
  struct Case {
    const char* name;
    std::function<void(TileBytes&)> mutate;
    const char* expect;
    bool seal;
  };
  const std::vector<Case> cases{
      {"body bit flip without a CRC update", [&](TileBytes& b) { b[tile1 + 0x60] ^= std::byte{1}; }, "crc mismatch", false},
      {"tile size field moved by 16", [&](TileBytes& b) { put32(b, tile1 + 0x2c, get32(b, tile1 + 0x2c) + 0x1000); }, "tile slot 1", true},
      {"cell-starts offset moved by 16", [](TileBytes& b) { put32(b, 0x88, get32(b, 0x88) + 16); }, "cellStarts", true},
      {"nonzero byte in region padding", [](TileBytes& b) { b[get32(b, 0x9c) + 12] = std::byte{1}; }, "nonzero padding", true},
      {"leaf tile count off by one", [](TileBytes& b) { put32(b, 0x8c, get32(b, 0x8c) + 1); }, "leaf tile count", true},
      {"tile table entries swapped",
       [](TileBytes& b) {
         const auto t = get32(b, 0xa0);
         const auto x = get32(b, t + 4);
         put32(b, t + 4, get32(b, t + 8));
         put32(b, t + 8, x);
       },
       "tile slot 1", true},
      {"file size field wrong", [](TileBytes& b) { put32(b, 8, static_cast<std::uint32_t>(b.size() + 16)); }, "size field", true},
      {"cell portal count off by one", [&](TileBytes& b) { put32(b, tile1 + get32(b, tile1 + 0x38) + 4, 2); }, "portal counts sum", true},
      {"portal target outside the slots",
       [&](TileBytes& b) {
         const auto p = tile1 + get32(b, tile1 + 0x3c);
         put32(b, p, (get32(b, p) & 0xfc000000u) | 9u);
       },
       "target slot holds a tile", true},
      {"tile box moved off its tree node",
       [&](TileBytes& b) {
         const float moved = 3.0f;
         std::memcpy(b.data() + tile1 + 4, &moved, 4);
       },
       "tile box", true},
      {"leaf tile cluster count wrong", [&](TileBytes& b) { b[tile1 + 0x36] = std::byte{2}; }, "cluster count", true},
      {"tile kd node symbol flipped", [&](TileBytes& b) { b[tile1 + get32(b, tile1 + 0x1c)] &= std::byte{0xfc}; }, "kd: node symbols consume", true},
      {"child tree node symbol flipped",
       [](TileBytes& b) {
         const auto k = get32(b, get32(b, 0xa8) + 4);
         b[k] = (b[k] & std::byte{0xf3}) | std::byte{0x08};
       },
       "child: tree node symbols", true},
      {"top kd symbol flipped",
       [](TileBytes& b) {
         const auto k = get32(b, 0x30);
         b[k] = (b[k] & std::byte{0xfc}) | std::byte{1};
       },
       "top kd", true},
      {"slot path byte flipped", [](TileBytes& b) { b[get32(b, 0x98)] ^= std::byte{0xff}; }, "slot paths", true},
      {"cell starts entry changed", [](TileBytes& b) { put32(b, get32(b, 0x88) + 4, 7); }, "cell starts", true},
  };
  for (const auto& c : cases) {
    const auto reason = rejection(bytes, c.mutate, c.seal);
    EXPECT_NE(reason.find(c.expect), std::string::npos) << c.name << ": got '" << reason << "', wanted '" << c.expect << "'";
  }
}

// Regression: promoting a measured statistic (portal reciprocity holds on only 72% of retail links) to an enforced claim would reject real tomes.
TEST(PrevisUvdTileModel, MeasuredClaimsAreCountedButNeverReject) {
  const auto model = parse_uvd_tiles(synthetic_tome());
  UvdTileClaims claims;
  validate_uvd_tiles(model, claims);
  EXPECT_TRUE(claims.violations().empty());
  const auto reciprocity = claims.count(UvdClaim::PortalReciprocity);
  EXPECT_GT(reciprocity.total, reciprocity.pass);
  std::size_t enforced = 0;
  for (std::size_t i = 0; i < k_uvd_claim_count; ++i) {
    const auto& info = uvd_claim_info(static_cast<UvdClaim>(i));
    ASSERT_NE(info.name, nullptr) << "claim " << i;
    if (info.enforced) {
      ++enforced;
    }
  }
  EXPECT_EQ(enforced, k_uvd_enforced_claims);
}

// Regression: the kd rank tables (u16 per 256 nodes, u8 per 16 nodes except every sixteenth) are re-derived from the symbols alone; an off-by-one in either table changes retail bytes.
TEST(PrevisUvdTileModel, KdNodeRegionRanksAreDerivedFromTheSymbols) {
  std::vector<std::uint8_t> small(33, 3);
  std::fill(small.begin(), small.begin() + 16, std::uint8_t{0});
  const auto region = encode_uvd_nodes(small);
  ASSERT_EQ(region.size(), 16u);
  EXPECT_EQ(static_cast<int>(region[0]), 0x00);
  EXPECT_EQ(static_cast<int>(region[4]), 0xff);
  EXPECT_EQ(static_cast<int>(region[8]), 0x03);
  EXPECT_EQ(static_cast<int>(region[12]), 16);
  EXPECT_EQ(static_cast<int>(region[13]), 16);

  std::vector<std::uint8_t> large(513, 3);
  std::fill(large.begin(), large.begin() + 200, std::uint8_t{0});
  std::fill(large.begin() + 288, large.begin() + 304, std::uint8_t{0});
  const auto big = encode_uvd_nodes(large);
  ASSERT_EQ(big.size(), 176u);
  const std::array<int, 4> supers{200, 0, 216, 0};
  for (std::size_t i = 0; i < supers.size(); ++i) EXPECT_EQ(static_cast<int>(big[132 + i]), supers[i]) << "u16 rank byte " << i;
  for (std::size_t k = 1; k <= 12; ++k) EXPECT_EQ(static_cast<std::size_t>(big[135 + k]), 16 * k) << "rank " << k;
  for (std::size_t k = 13; k <= 15; ++k) EXPECT_EQ(static_cast<int>(big[135 + k]), 200) << "rank " << k;
  for (std::size_t k = 17; k <= 18; ++k) EXPECT_EQ(static_cast<int>(big[135 + k - 1]), 0) << "rank " << k;
  for (std::size_t k = 19; k <= 31; ++k) EXPECT_EQ(static_cast<int>(big[135 + k - 1]), 16) << "rank " << k;
}

namespace {

UvdBuildInput build_input(std::size_t count, float spread) {
  UvdBuildInput in;
  in.cell_box = std::array<float, 6>{-2048, -2048, 0, 2048, 2048, 1024};
  for (std::size_t i = 0; i < count; ++i) {
    const float x = spread * (static_cast<float>(i % 17) - 8.0f);
    const float y = spread * (static_cast<float>(i % 5) - 2.0f);
    UvdBuildObject object;
    object.box = {x, y, 10.0f, x + 64.0f, y + 80.0f, 200.0f + static_cast<float>(i)};
    object.id = 0x01000000u + static_cast<std::uint32_t>(i * 3);
    in.objects.push_back(object);
  }
  return in;
}

std::string build_rejection(TileBytes bytes, const std::function<void(TileBytes&)>& mutate, bool seal) {
  mutate(bytes);
  if (seal) {
    reseal(bytes);
  }
  std::string joined;
  for (const auto& issue : validate_uvd_tome_tiled(bytes)) joined += issue + "; ";
  return joined;
}

}  // namespace

// Regression: a builder that mis-sizes a run list, leaves a derived field stale or breaks one invariant for some object count writes a tome the tile model refuses or rewrites differently.
TEST(PrevisUvdTileBuild, BuildsValidByteStableTomesForNoneOneAndManyObjects) {
  const std::vector<std::pair<std::size_t, float>> shapes{{0, 0.0f}, {1, 0.0f}, {9, 700.0f}, {300, 1500.0f}};
  for (const auto& [count, spread] : shapes) {
    const auto in = build_input(count, spread);
    const auto bytes = write_uvd_tome_tiled(in);
    EXPECT_TRUE(validate_uvd_tome_tiled(bytes).empty()) << count << " objects";
    const auto model = parse_uvd_tiles(bytes);
    EXPECT_EQ(write_uvd_tiles(model), bytes) << count << " objects";
    const auto stages = uvd_tile_round_trips(bytes, model);
    EXPECT_TRUE(stages.stage1 && stages.stage2 && stages.stage3) << count << " objects";
    EXPECT_EQ(get32(bytes, 0x40), count);
    EXPECT_EQ(model.object_boxes.size(), 24 * count);
    EXPECT_EQ(model.slots.size(), 1u);
    EXPECT_EQ(get32(bytes, 0x8c), 1u);
    const auto header = decode_uvd_header(model);
    for (std::size_t i = 0; i < count; ++i) {
      EXPECT_EQ(header.objects.ids[i], in.objects[i].id);
      for (std::size_t a = 0; a < 3; ++a) {
        EXPECT_LE(header.world_min[a], in.objects[i].box[a]);
        EXPECT_GE(header.world_max[a], in.objects[i].box[3 + a]);
      }
    }
  }
}

// Regression: the conservative promise (nothing wrongly hidden) breaks if the cell does not cover the tile, an object falls outside the tile, or the cell lists fewer than every object.
TEST(PrevisUvdTileBuild, OneOpenCellListsEveryObject) {
  const auto in = build_input(9, 700.0f);
  const auto model = parse_uvd_tiles(write_uvd_tome_tiled(in));
  const auto bodies = decode_uvd_bodies(model);
  const auto header = decode_uvd_header(model);
  ASSERT_EQ(bodies.slots.size(), 1u);
  const auto& tile = *bodies.slots[0];
  EXPECT_TRUE(tile.leaf);
  ASSERT_EQ(tile.cells.size(), 1u);
  const auto& cell = tile.cells[0];
  EXPECT_EQ(cell.list_start, 0u);
  EXPECT_EQ(cell.list_count, 9u);
  EXPECT_EQ(cell.x, k_full);
  EXPECT_EQ(cell.y, k_full);
  EXPECT_EQ(cell.z, k_full);
  ASSERT_EQ(header.run_a.size(), 1u);
  EXPECT_EQ(header.run_a[0], (std::pair<std::uint32_t, std::uint32_t>{0, 9}));
  EXPECT_EQ(header.widths, (std::array<std::uint32_t, 4>{1, 4, 0, 0}));
  ASSERT_EQ(tile.portals.size(), 6u);
  for (std::uint32_t face = 0; face < 6; ++face) {
    EXPECT_EQ(tile.portals[face].face, face);
    EXPECT_EQ(tile.portals[face].outside, 1u);
    EXPECT_EQ(tile.portals[face].target, k_uvd_outside_target);
  }
  EXPECT_EQ(header.child_trees.size(), 6u);
  for (const auto& tree : header.child_trees) {
    ASSERT_TRUE(tree.has_value());
    EXPECT_EQ(tree->node_count, 1u);
    EXPECT_EQ(tree->entries, std::vector<std::uint32_t>{0});
  }
  for (const auto& object : in.objects) {
    for (std::size_t a = 0; a < 3; ++a) {
      EXPECT_LE(tile.box[a], object.box[a]);
      EXPECT_GE(tile.box[3 + a], object.box[3 + a]);
    }
  }
}

// Regression: a validator or builder that tolerates a bad object table, a stale count or a CRC that is not resealed lets a broken tome reach the game; each variant needs its own reason.
TEST(PrevisUvdTileBuild, RejectsCorruptedBuiltTomesAndBadInputs) {
  const auto bytes = write_uvd_tome_tiled(build_input(9, 700.0f));
  const std::size_t tile = get32(bytes, get32(bytes, 0xa0));
  struct Case {
    const char* name;
    std::function<void(TileBytes&)> mutate;
    const char* expect;
    bool seal;
  };
  const std::vector<Case> cases{
      {"body bit flip without a CRC update", [&](TileBytes& b) { b[tile + 0x60] ^= std::byte{1}; }, "CRC", false},
      {"tile box moved off the world box", [&](TileBytes& b) { b[tile + 4] ^= std::byte{0x10}; }, "tile box", true},
      {"cluster count wrong", [&](TileBytes& b) { b[tile + 0x36] = std::byte{3}; }, "cluster count", true},
      {"cell portal count wrong", [&](TileBytes& b) { put32(b, tile + 0x50 + 4, 5); }, "portal counts sum", true},
      {"object box min above max", [](TileBytes& b) { b[get32(b, 0x44) + 3] = std::byte{0x7f}; }, "objects: boxes", true},
      {"object id repeated", [](TileBytes& b) { put32(b, get32(b, 0x50) + 4, get32(b, get32(b, 0x50))); }, "ids distinct", true},
      {"leaf tile count wrong", [](TileBytes& b) { put32(b, 0x8c, 2); }, "leaf tile count", true},
  };
  for (const auto& c : cases) {
    const auto reason = build_rejection(bytes, c.mutate, c.seal);
    EXPECT_NE(reason.find(c.expect), std::string::npos) << c.name << ": got '" << reason << "', wanted '" << c.expect << "'";
  }
  EXPECT_THROW(build_conservative_uvd_model(UvdBuildInput{}), UvdTileLayoutError);
  auto repeated = build_input(2, 100.0f);
  repeated.objects[1].id = repeated.objects[0].id;
  EXPECT_THROW(build_conservative_uvd_model(repeated), UvdTileLayoutError);
  auto inverted = build_input(1, 100.0f);
  std::swap(inverted.objects[0].box[0], inverted.objects[0].box[3]);
  EXPECT_THROW(build_conservative_uvd_model(inverted), UvdTileLayoutError);
  auto not_finite = build_input(1, 100.0f);
  not_finite.objects[0].box[1] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(build_conservative_uvd_model(not_finite), UvdTileLayoutError);
}

// Regression: the LOD formula and the zero-thickness widening are measured on retail; a drifted formula yields a non-positive or different LOD, and a flat world box would make the LOD zero and the tome invalid.
TEST(PrevisUvdTileBuild, LodExpandAndWorldBoxRules) {
  EXPECT_EQ(uvd_tile_lod({0, 0, 0, 4096, 4096, 4096}, 512.0f), 8.0f);
  EXPECT_EQ(uvd_tile_lod({-4096, 8192, -1664, 8192, 20480, 8192}, 512.0f), 22.2989501953125f);
  EXPECT_EQ(uvd_default_expand({0, 0, 0, 100, 100, 100}), 16.0f);
  EXPECT_EQ(uvd_default_expand({0, 0, 0, 2048, 1536, 1024}), 32.0f);
  EXPECT_EQ(uvd_default_expand({0, 0, 0, 100000, 10, 10}), 128.0f);
  EXPECT_FALSE(uvd_positions_box({}).has_value());
  const std::vector<float> ragged{1, 2, 3, 4};
  EXPECT_FALSE(uvd_positions_box(ragged).has_value());
  const std::vector<float> positions{1, 5, 3, -2, 7, 3, 0, 6, 9};
  EXPECT_EQ(uvd_positions_box(positions), (std::array<float, 6>{-2, 5, 3, 1, 7, 9}));
  UvdBuildInput flat;
  flat.objects.push_back({{0, 0, 0, 4096, 4096, 0}, 7});
  EXPECT_EQ(uvd_build_world_box(flat), (std::array<float, 6>{0, 0, 0, 4096, 4096, 512}));
  EXPECT_TRUE(validate_uvd_tome_tiled(write_uvd_tome_tiled(flat)).empty());
}

namespace {

// Constraint: a closed box of wall triangles encloses, and skip_face drops one wall so a shell can be left open.
std::vector<std::array<float, 9>> shell_triangles(const std::array<float, 6>& box, int skip_face = -1) {
  const float x0 = box[0], y0 = box[1], z0 = box[2], x1 = box[3], y1 = box[4], z1 = box[5];
  const std::vector<std::array<float, 9>> faces{
      {x1, y0, z0, x1, y1, z0, x1, y1, z1}, {x1, y0, z0, x1, y1, z1, x1, y0, z1},
      {x0, y0, z0, x0, y0, z1, x0, y1, z1}, {x0, y0, z0, x0, y1, z1, x0, y1, z0},
      {x0, y1, z0, x0, y1, z1, x1, y1, z1}, {x0, y1, z0, x1, y1, z1, x1, y1, z0},
      {x0, y0, z0, x1, y0, z0, x1, y0, z1}, {x0, y0, z0, x1, y0, z1, x0, y0, z1},
      {x0, y0, z1, x1, y0, z1, x1, y1, z1}, {x0, y0, z1, x1, y1, z1, x0, y1, z1},
      {x0, y0, z0, x0, y1, z0, x1, y1, z0}, {x0, y0, z0, x1, y1, z0, x1, y0, z0}};
  std::vector<std::array<float, 9>> kept;
  for (int face = 0; face < 6; ++face) {
    if (face == skip_face) continue;
    kept.push_back(faces[static_cast<std::size_t>(2 * face)]);
    kept.push_back(faces[static_cast<std::size_t>(2 * face + 1)]);
  }
  return kept;
}

std::vector<std::uint32_t> cull_ids(const UvdBuildInput& in) {
  std::vector<std::uint32_t> ids;
  for (const auto& object : uvd_cull_occluded(in)) ids.push_back(object.id);
  return ids;
}

// Constraint: a wall on the plane x made of cells x cells squares of side step starting at low on y and z, with one square left out when hole_y and hole_z are not negative.
std::vector<std::array<float, 9>> x_wall_grid(float x, float low, float step, int cells, int hole_y, int hole_z) {
  std::vector<std::array<float, 9>> wall;
  for (int iy = 0; iy < cells; ++iy) {
    for (int iz = 0; iz < cells; ++iz) {
      if (iy == hole_y && iz == hole_z) continue;
      const float y0 = low + static_cast<float>(iy) * step, y1 = y0 + step;
      const float z0 = low + static_cast<float>(iz) * step, z1 = z0 + step;
      wall.push_back({x, y0, z0, x, y1, z0, x, y1, z1});
      wall.push_back({x, y0, z0, x, y1, z1, x, y0, z1});
    }
  }
  return wall;
}

}  // namespace

// Regression: a shell assembled from separate wall bodies must hide the body it encloses, while an open side, a shared face or no occluder at all must leave it listed, because over-culling is what puts holes in a precombined cell.
TEST(PrevisUvdTileBuild, AnOccluderCullsOnlyTheObjectsItsShellEncloses) {
  UvdBuildInput in;
  in.cell_box = std::array<float, 6>{-1024, -1024, 0, 1024, 1024, 512};
  // Constraint: 11 sits inside the shell, 12 is well outside it and 13 stands beyond one wall, so only 11 may be culled.
  in.objects.push_back({{0, 0, 0, 64, 64, 64}, 11});
  in.objects.push_back({{400, 0, 0, 464, 64, 64}, 12});
  in.objects.push_back({{100, 0, 0, 164, 64, 64}, 13});
  in.occluders = shell_triangles({-16, -16, -16, 80, 80, 80});

  const auto cull = uvd_cull_occluded(in);
  ASSERT_EQ(cull.size(), 2u);
  EXPECT_EQ(cull[0].id, 12u);
  EXPECT_EQ(cull[1].id, 13u);

  const auto model = parse_uvd_tiles(write_uvd_tome_tiled(in));
  const auto header = decode_uvd_header(model);
  ASSERT_EQ(header.objects.ids.size(), 2u);
  EXPECT_EQ(std::find(header.objects.ids.begin(), header.objects.ids.end(), 11u), header.objects.ids.end());
  const auto bodies = decode_uvd_bodies(model);
  ASSERT_EQ(bodies.slots.size(), 1u);
  EXPECT_EQ(bodies.slots[0]->cells[0].list_count, 2u);
  ASSERT_EQ(header.run_a.size(), 1u);
  EXPECT_EQ(header.run_a[0], (std::pair<std::uint32_t, std::uint32_t>{0, 2}));
  EXPECT_TRUE(validate_uvd_tome_tiled(write_uvd_tome_tiled(in)).empty());
  // Constraint: write_tome culls once and builds from the survivors with no occluders, which must give the same bytes as building from the raw input.
  auto prefiltered = in;
  prefiltered.objects = uvd_cull_occluded(in);
  prefiltered.occluders.clear();
  EXPECT_EQ(write_uvd_tome_tiled(prefiltered), write_uvd_tome_tiled(in));

  // Constraint: without any occluder the same input lists all three, so the difference is the cull and nothing else.
  auto open = in;
  open.occluders.clear();
  EXPECT_EQ(decode_uvd_header(parse_uvd_tiles(write_uvd_tome_tiled(open))).objects.ids.size(), 3u);
  // Constraint: dropping one wall leaves the +X axis clear, so a doorway can never hide what stands behind it.
  auto missing_wall = in;
  missing_wall.occluders = shell_triangles({-16, -16, -16, 80, 80, 80}, 0);
  EXPECT_EQ(cull_ids(missing_wall), (std::vector<std::uint32_t>{11, 12, 13}));
  // Constraint: a single wall is not a shell, so a lone occluder body hides nothing.
  auto lone_wall = in;
  lone_wall.occluders = {{-16, -16, -16, -16, 80, -16, -16, 80, 80}};
  EXPECT_EQ(cull_ids(lone_wall), (std::vector<std::uint32_t>{11, 12, 13}));
  // Constraint: a body's own triangles touch its own box, so a body that occludes still lists itself.
  auto self_shell = in;
  self_shell.occluders = shell_triangles({0, 0, 0, 64, 64, 64});
  EXPECT_EQ(cull_ids(self_shell), (std::vector<std::uint32_t>{11, 12, 13}));
}

// Regression: six plates hit at any distance used to count as a room, so an object in an open field was hidden; the plates must be within one cell edge to enclose.
TEST(PrevisUvdTileBuild, OccludersBeyondOneCellEdgeDoNotEnclose) {
  UvdBuildInput in;
  in.cell_box = std::array<float, 6>{-1024, -1024, 0, 1024, 1024, 512};
  in.objects.push_back({{0, 0, 0, 64, 64, 64}, 11});
  // Constraint: every ray from the object needs 6000 units or more to reach its plate, which is past the 4096 cap, so the object stays listed.
  in.occluders = shell_triangles({-6000, -6000, -6000, 6000, 6000, 6000});
  EXPECT_EQ(cull_ids(in), (std::vector<std::uint32_t>{11}));
  // Constraint: the same shell pulled in to 4000 puts every hit under the cap, so only the distance differs and the object is hidden.
  in.occluders = shell_triangles({-4000, -4000, -4000, 4000, 4000, 4000});
  EXPECT_TRUE(cull_ids(in).empty());
}

// Regression: a wall hole off the centre line must keep the object listed, because six centre rays alone used to hide an object a player can see through it.
TEST(PrevisUvdTileBuild, ARoomOpenOffTheSixCentreAxesKeepsItsObject) {
  UvdBuildInput in;
  in.cell_box = std::array<float, 6>{-1024, -1024, 0, 1024, 1024, 512};
  in.objects.push_back({{0, 0, 0, 64, 64, 64}, 11});
  // Constraint: the +X wall is a 3x3 grid of 32-unit squares over -16..80; the centre ray at y = z = 32 lands in the middle square.
  const std::array<float, 6> room{-16, -16, -16, 80, 80, 80};
  auto closed = shell_triangles(room, 0);
  const auto full_wall = x_wall_grid(80.0f, -16.0f, 32.0f, 3, -1, -1);
  closed.insert(closed.end(), full_wall.begin(), full_wall.end());
  in.occluders = closed;
  EXPECT_TRUE(cull_ids(in).empty()) << "the control room has every wall";

  // Constraint: dropping the square at y and z in 48..80 leaves the centre ray and every other box corner blocked, and only the +X rays from the two corners at y = z = 64 pass through.
  auto holed = shell_triangles(room, 0);
  const auto holed_wall = x_wall_grid(80.0f, -16.0f, 32.0f, 3, 2, 2);
  holed.insert(holed.end(), holed_wall.begin(), holed_wall.end());
  in.occluders = holed;
  EXPECT_EQ(cull_ids(in), (std::vector<std::uint32_t>{11}));
}

// Regression: a cell whose every object is hidden must still build a valid empty tome from its cell box, and without a cell box there is nothing to size it from.
TEST(PrevisUvdTileBuild, ACellWithEveryObjectCulledBuildsAnEmptyTome) {
  UvdBuildInput in;
  in.cell_box = std::array<float, 6>{-1024, -1024, 0, 1024, 1024, 512};
  in.objects.push_back({{0, 0, 0, 64, 64, 64}, 11});
  in.objects.push_back({{8, 8, 8, 40, 40, 40}, 12});
  in.occluders = shell_triangles({-16, -16, -16, 80, 80, 80});
  ASSERT_TRUE(cull_ids(in).empty());

  const auto bytes = write_uvd_tome_tiled(in);
  EXPECT_TRUE(validate_uvd_tome_tiled(bytes).empty());
  // Constraint: 1,056 bytes is the measured size of a retail tome with no objects, and word 0x40 is its object count.
  EXPECT_EQ(bytes.size(), 1056u);
  EXPECT_EQ(get32(bytes, 8), bytes.size());
  EXPECT_EQ(get32(bytes, 0x40), 0u);
  const auto header = decode_uvd_header(parse_uvd_tiles(bytes));
  EXPECT_TRUE(header.objects.ids.empty());
  EXPECT_EQ(header.world_min, (std::array<float, 3>{-1024, -1024, 0}));
  EXPECT_EQ(header.world_max, (std::array<float, 3>{1024, 1024, 512}));
  const auto bodies = decode_uvd_bodies(parse_uvd_tiles(bytes));
  ASSERT_EQ(bodies.slots.size(), 1u);
  EXPECT_EQ(bodies.slots[0]->cells[0].list_count, 0u);

  in.cell_box.reset();
  EXPECT_THROW(write_uvd_tome_tiled(in), UvdTileLayoutError);
}

// Regression: the helper must spell a generated tome's path as the patch plugin folder and an uppercase eight-digit cell, with the plugin byte of the form id dropped.
TEST(PrevisUvdTileBuild, TomePathUsesThePatchPluginFolderAndUppercaseCell) {
  const std::filesystem::path data{"Data"};
  EXPECT_EQ(uvd_tome_path(data, "Patch.esp", 0xFF0000ABu), data / "Vis" / "Patch.esp" / "000000AB.uvd");
  EXPECT_EQ(uvd_tome_path(data, "Patch.esp", 0x00ABCDEFu), data / "Vis" / "Patch.esp" / "00ABCDEF.uvd");
  EXPECT_EQ(uvd_tome_path(data, "Patch.esp", 0x801u), data / "Vis" / "Patch.esp" / "00000801.uvd");
}

// Regression: a generated tome must land where a VISI lookup finds it, and must not reach disk before it validates.
TEST(PrevisUvdTileBuild, WritesTheGeneratedTomeWhereVisiLooksItUp) {
  const auto bytes = write_uvd_tome_tiled(build_input(3, 250.0f));
  const auto root = std::filesystem::temp_directory_path() / "radicle_previs_visi_tome";
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);

  const auto path = write_visi_tome(root, "TestPlugin.esm", 0xABCDEFu, bytes);
  EXPECT_EQ(path, uvd_tome_path(root, "TestPlugin.esm", 0xABCDEFu));
  EXPECT_EQ(path.filename().string(), "00ABCDEF.uvd");
  EXPECT_EQ(path.parent_path().filename().string(), "TestPlugin.esm");
  EXPECT_EQ(path.parent_path().parent_path().filename().string(), "Vis");
  EXPECT_EQ(std::filesystem::file_size(path), bytes.size());
  std::vector<std::byte> read_back(bytes.size());
  {
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(read_back.data()), static_cast<std::streamsize>(read_back.size()));
  }
  EXPECT_TRUE(validate_uvd_tome_tiled(read_back).empty());

  // Constraint: a tome the reader would reject must leave no file behind.
  auto broken = bytes;
  broken[8] ^= std::byte{1};
  EXPECT_THROW(write_visi_tome(root, "TestPlugin.esm", 0x1u, broken), fo4::parse_error);
  EXPECT_FALSE(std::filesystem::exists(uvd_tome_path(root, "TestPlugin.esm", 0x1u)));
  std::filesystem::remove_all(root, ignored);
}

// Regression: a tome whose header words wrap u32 arithmetic or point past the file must be refused, not walked.
TEST(PrevisUvdTome, PatchRefusesATomeWhoseTablesDoNotFit) {
  UvdTomeRequest request;
  const float world_min[3] = {-1024.0f, -1024.0f, 0.0f};
  const float world_max[3] = {1024.0f, 1024.0f, 512.0f};
  for (int axis = 0; axis < 3; ++axis) {
    request.world_min[axis] = world_min[axis];
    request.world_max[axis] = world_max[axis];
  }
  for (int index = 0; index < 3; ++index) {
    UvdPage page;
    const float base = static_cast<float>(index) * 256.0f;
    for (int axis = 0; axis < 3; ++axis) {
      page.min[axis] = base;
      page.max[axis] = base + 128.0f;
    }
    page.positions = {base, base, base, base + 128.0f, base, base, base, base + 128.0f, base};
    page.indices = {0, 1, 2};
    page.leaves.push_back(UvdLeaf{3, 1});
    request.pages.push_back(page);
  }
  const TileBytes good = write_uvd_tome(request);
  const UvdPage replacement = request.pages[1];
  ASSERT_NO_THROW(patch_uvd_page(good, 1, replacement));

  const std::size_t table = get32(good, 0xA0);
  const std::size_t leaf_table = get32(good, 0xA4);
  struct Case {
    const char* name;
    std::function<void(TileBytes&)> mutate;
  };
  // Constraint: each mutation is one header word the walker trusts, and none needs a resealed CRC because the patch never checks it.
  const std::vector<Case> cases{
      {"page offset far past the end", [&](TileBytes& b) { put32(b, table, 0xFFFFFFF0u); }},
      {"page offset inside the page table", [&](TileBytes& b) { put32(b, table + 4, 0); }},
      {"page offset leaves no room for a header", [&](TileBytes& b) { put32(b, table + 8, static_cast<std::uint32_t>(leaf_table - 8)); }},
      {"page table at the top of the u32 range", [](TileBytes& b) { put32(b, 0xA0, 0xFFFFFFFCu); }},
      {"page count that wraps four times itself", [](TileBytes& b) { put32(b, 0x38, 0x40000001u); }},
      {"page count of four billion", [](TileBytes& b) { put32(b, 0x38, 0xFFFFFFFFu); }},
      {"child block past the end", [](TileBytes& b) { put32(b, 0xA8, 0xFFFFFFF0u); }},
      {"tome shorter than its first section", [](TileBytes& b) { b.resize(0x100); }},
  };
  for (const auto& c : cases) {
    TileBytes bytes = good;
    c.mutate(bytes);
    EXPECT_THROW(patch_uvd_page(bytes, 1, replacement), fo4::parse_error) << c.name;
  }
}
