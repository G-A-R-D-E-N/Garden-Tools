#include "fo4/nif/nif.hpp"
#include "fo4/previs/shared_geom_extra.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<char> chars{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  std::vector<std::byte> out(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    out[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return out;
}

TEST(PrevisSharedGeomExtra, RebuildsCompactExtraForMorePlacements) {
  std::vector<std::byte> donor(152, std::byte{0});
  donor[24] = std::byte{1};
  donor[68] = std::byte{1};
  fo4::previs::SharedCombinedRecordXform seed{};
  seed.rotation[4] = 1.0f;
  ASSERT_TRUE(fo4::previs::write_shared_combined_record_xform(donor, 0, 0, seed));
  fo4::previs::SharedCombinedRecordXform a{};
  fo4::previs::SharedCombinedRecordXform b{};
  a.translation[0] = 1.0f;
  b.translation[1] = 2.0f;
  const std::array records{a, b};
  const auto rebuilt = fo4::previs::rebuild_compact_shared_geom_extra(donor, 2, records);
  ASSERT_TRUE(rebuilt.has_value());
  ASSERT_EQ(rebuilt->size(), fo4::previs::shared_combined_geom_extra_size(2, 1));
  fo4::previs::SharedCombinedRecordXform roundtrip{};
  ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(*rebuilt, 1, 0, roundtrip));
  EXPECT_FLOAT_EQ(roundtrip.translation[1], 2.0f);
}

TEST(PrevisSharedGeomExtra, RewritesCombinedRecordBytes) {
  std::vector<std::byte> block(152, std::byte{0});
  block[24] = std::byte{1};
  block[25] = block[26] = block[27] = std::byte{0};
  block[68] = std::byte{1};
  fo4::previs::SharedCombinedRecordXform xform{};
  xform.translation[0] = 3.5f;
  xform.rotation[4] = 1.0f;
  xform.scale = 2.0f;
  ASSERT_TRUE(fo4::previs::write_shared_combined_record_xform(block, 0, 0, xform));
  fo4::previs::SharedCombinedRecordXform reread{};
  ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(block, 0, 0, reread));
  EXPECT_FLOAT_EQ(reread.translation[0], 3.5f);
  EXPECT_FLOAT_EQ(reread.scale, 2.0f);
}

// Detects: the combined record read or written at the wrong offsets. A vanilla record is grayscale, row-major rotation, translation, scale, bounding sphere; the old writer put translation over the grayscale word and left the sphere at the template's place. Reading a shipped record must give an orthonormal rotation, and writing one instance's transform over its sibling must reproduce that sibling's shipped sphere.
TEST(PrevisSharedGeomExtra, VanillaCombinedRecordLayoutAndBounds) {
  const fs::path oracle = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles" / "00000FEC_01B8656E_OC.NIF";
  if (!fs::exists(oracle)) GTEST_SKIP();
  auto bytes = read_bytes(oracle);
  const auto nif = fo4::nif::read_nif(bytes);
  std::size_t checked = 0;
  for (const auto& block : nif.blocks()) {
    if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
    auto extra = std::span<std::byte>(bytes).subspan(block.offset, block.size);
    const auto objects = fo4::previs::shared_combined_object_slots(extra);
    for (std::size_t object = 0; object < objects; ++object) {
      fo4::previs::SharedCombinedRecordXform first{};
      fo4::previs::SharedCombinedRecordXform second{};
      if (!fo4::previs::read_shared_combined_record_xform(extra, object, 0, first)) continue;
      for (std::size_t row = 0; row < 3; ++row) {
        float length = 0.0f;
        for (std::size_t column = 0; column < 3; ++column) {
          length += first.rotation[3 * row + column] * first.rotation[3 * row + column];
        }
        EXPECT_NEAR(length, 1.0f, 1e-3f);
      }
      EXPECT_GT(first.scale, 0.0f);
      if (!fo4::previs::read_shared_combined_record_xform(extra, object, 1, second)) continue;
      const auto first_at = fo4::previs::shared_combined_record_offset(extra, object, 0);
      const auto second_at = fo4::previs::shared_combined_record_offset(extra, object, 1);
      ASSERT_TRUE(first_at.has_value());
      ASSERT_TRUE(second_at.has_value());
      const auto floats_at = [&](std::size_t at) {
        std::array<float, 18> out{};
        std::memcpy(out.data(), extra.data() + at, sizeof(out));
        return out;
      };
      // Constraint: record offsets are relative to the extra's body, which follows the 4-byte NiExtraData name.
      const std::size_t body = 4;
      const auto expected = floats_at(body + *second_at);
      ASSERT_TRUE(fo4::previs::write_shared_combined_record_xform(extra, object, 0, second));
      const auto written = floats_at(body + *first_at);
      EXPECT_FLOAT_EQ(written[0], expected[0]);
      for (std::size_t i = 1; i < 14; ++i) EXPECT_FLOAT_EQ(written[i], expected[i]) << i;
      for (std::size_t i = 14; i < 17; ++i) EXPECT_NEAR(written[i], expected[i], 0.05f) << i;
      EXPECT_NEAR(written[17], expected[17], 0.01f);
      ++checked;
    }
  }
  EXPECT_GT(checked, 0u);
}

// Detects: a wrong reading of the Shared extra's object table. On every shipped block the shape's vertex and triangle totals are the per-object counts times their instance counts, and two different meshes that follow each other in the CSG stream sit exactly stream_bytes() apart, which is what makes data_offset a stream address of vertices followed by u16 triangles.
TEST(PrevisSharedGeomExtra, VanillaObjectTableTotalsAndStreamSpacing) {
  const fs::path oracle_dir = fs::path(FO4_TESTDATA_DIR) / "previs" / "oracles";
  std::size_t blocks = 0;
  std::size_t adjacent = 0;
  for (const char* name : {"00000FEC_01B8656E_OC.NIF", "00000FEC_0A722D3C_OC.NIF", "00000FEC_0A82376A_OC.NIF"}) {
    const auto path = oracle_dir / name;
    if (!fs::exists(path)) GTEST_SKIP();
    SCOPED_TRACE(name);
    const auto bytes = read_bytes(path);
    const auto nif = fo4::nif::read_nif(bytes);
    for (const auto& block : nif.blocks()) {
      if (block.type != "BSPackedCombinedSharedGeomDataExtra") continue;
      const auto header = fo4::previs::read_shared_geom_header(
          std::span<const std::byte>(bytes).subspan(block.offset, block.size));
      ASSERT_TRUE(header.has_value());
      ++blocks;
      std::uint64_t vertices = 0;
      std::uint64_t triangles = 0;
      for (const auto& object : header->objects) {
        EXPECT_EQ(object.file_hash, 0x2088054Du);
        EXPECT_EQ(object.lod_levels, 3u);
        EXPECT_GT(object.vertex_bytes(), 0u);
        vertices += std::uint64_t{object.vertex_count} * object.combined_count;
        triangles += std::uint64_t{object.triangles()} * object.combined_count;
      }
      EXPECT_EQ(vertices, header->vertex_count);
      EXPECT_EQ(triangles, header->triangle_count);
      for (std::size_t index = 1; index < header->objects.size(); ++index) {
        const auto& before = header->objects[index - 1];
        const auto& after = header->objects[index];
        if (after.data_offset <= before.data_offset) continue;
        const auto gap = std::size_t{after.data_offset} - before.data_offset;
        // Constraint: a larger gap only means another mesh sits between the two, so it must still clear this one.
        EXPECT_GE(gap, before.stream_bytes());
        if (gap == before.stream_bytes()) ++adjacent;
      }
    }
  }
  EXPECT_EQ(blocks, 11u);
  EXPECT_GE(adjacent, 10u);
}

struct ObjectSpec {
  std::uint32_t hash{0};
  std::uint32_t offset{0};
  std::uint32_t vertices{0};
  std::array<std::uint32_t, 3> triangles{};
  std::uint64_t desc{0};
};

void put_u32(std::vector<std::byte>& out, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) out[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
}

void put_f32(std::vector<std::byte>& out, std::size_t at, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  put_u32(out, at, bits);
}

// Constraint: counts are in the hundreds so a table-layout block cannot also walk as the compact layout.
std::vector<std::byte> table_extra(const std::vector<ObjectSpec>& objects, std::size_t prefix) {
  const std::size_t body = 28 + objects.size() * (8 + 32 + 4 + 72 + 8);
  std::vector<std::byte> out(prefix + body, std::byte{0});
  if (prefix != 0) put_u32(out, 0, 0x7E7E7E7Eu);
  put_u32(out, prefix + 8, 1u);
  put_u32(out, prefix + 12, 1u);
  put_u32(out, prefix + 24, static_cast<std::uint32_t>(objects.size()));
  std::size_t pos = prefix + 28 + 8 * objects.size();
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto& object = objects[index];
    put_u32(out, prefix + 28 + 8 * index, object.hash);
    put_u32(out, prefix + 28 + 8 * index + 4, object.offset);
    put_u32(out, pos, object.vertices);
    put_u32(out, pos + 4, 3u);
    std::uint32_t words = 0;
    for (std::size_t level = 0; level < 3; ++level) {
      put_u32(out, pos + 8 + 8 * level, object.triangles[level]);
      put_u32(out, pos + 12 + 8 * level, words);
      words += 3u * object.triangles[level];
    }
    pos += 32;
    put_u32(out, pos, 1u);
    pos += 4;
    put_f32(out, pos + 4, 1.0f);
    put_f32(out, pos + 20, 1.0f);
    put_f32(out, pos + 36, 1.0f);
    put_f32(out, pos + 52, 1.0f);
    pos += 72;
    put_u32(out, pos, static_cast<std::uint32_t>(object.desc & 0xFFFFFFFFu));
    put_u32(out, pos + 4, static_cast<std::uint32_t>(object.desc >> 32));
    pos += 8;
  }
  return out;
}

const ObjectSpec k_object_a{0xA1A1A1A1u, 0x1000u, 300u, {200u, 100u, 50u}, 0x0000000000000005ull};
const ObjectSpec k_object_b{0xB2B2B2B2u, 0x2000u, 400u, {500u, 250u, 125u}, 0x0000000000000006ull};

void expect_object(const fo4::previs::SharedGeomObject& actual, const ObjectSpec& expected) {
  EXPECT_EQ(actual.file_hash, expected.hash);
  EXPECT_EQ(actual.data_offset, expected.offset);
  EXPECT_EQ(actual.vertex_count, expected.vertices);
  for (std::size_t level = 0; level < 3; ++level) EXPECT_EQ(actual.triangle_count[level], expected.triangles[level]);
  EXPECT_EQ(actual.vertex_desc, expected.desc);
}

// Detects: a rebuild that copies object 0 over every object and keeps the template's header totals. Object 1 must keep its own table entry, counts and descriptor, a new object 2 is a copy of object 0, and the header totals are the sums over the rebuilt objects.
TEST(PrevisSharedGeomExtra, RebuildKeepsEachObjectsOwnDataAndRecomputesTotals) {
  const auto donor = table_extra({k_object_a, k_object_b}, 4);
  const auto donor_header = fo4::previs::read_shared_geom_header(donor);
  ASSERT_TRUE(donor_header.has_value());
  ASSERT_EQ(donor_header->objects.size(), 2u);
  expect_object(donor_header->objects[1], k_object_b);

  std::array<fo4::previs::SharedCombinedRecordXform, 3> records{};
  for (std::size_t i = 0; i < records.size(); ++i) records[i].translation[0] = static_cast<float>(i + 1);
  const auto grown = fo4::previs::rebuild_compact_shared_geom_extra(donor, 3, records);
  ASSERT_TRUE(grown.has_value());
  const auto header = fo4::previs::read_shared_geom_header(*grown);
  ASSERT_TRUE(header.has_value());
  ASSERT_EQ(header->objects.size(), 3u);
  expect_object(header->objects[0], k_object_a);
  expect_object(header->objects[1], k_object_b);
  expect_object(header->objects[2], k_object_a);
  EXPECT_EQ(header->vertex_count, 300u + 400u + 300u);
  EXPECT_EQ(header->triangle_count, 350u + 875u + 350u);
  for (std::size_t i = 0; i < records.size(); ++i) {
    fo4::previs::SharedCombinedRecordXform reread{};
    ASSERT_TRUE(fo4::previs::read_shared_combined_record_xform(*grown, i, 0, reread));
    EXPECT_FLOAT_EQ(reread.translation[0], static_cast<float>(i + 1));
  }

  const auto shrunk = fo4::previs::rebuild_compact_shared_geom_extra(
      donor, 1, std::span<const fo4::previs::SharedCombinedRecordXform>(records.data(), 1));
  ASSERT_TRUE(shrunk.has_value());
  const auto shrunk_header = fo4::previs::read_shared_geom_header(*shrunk);
  ASSERT_TRUE(shrunk_header.has_value());
  ASSERT_EQ(shrunk_header->objects.size(), 1u);
  expect_object(shrunk_header->objects[0], k_object_a);
  EXPECT_EQ(shrunk_header->vertex_count, 300u);
  EXPECT_EQ(shrunk_header->triangle_count, 350u);

  // Constraint: detects: a rebuild past the 512-object bound that is written and cannot be re-read.
  const std::vector<fo4::previs::SharedCombinedRecordXform> too_many(fo4::previs::k_shared_geom_max_objects + 1);
  EXPECT_FALSE(fo4::previs::rebuild_compact_shared_geom_extra(donor, too_many.size(), too_many).has_value());
}

fo4::previs::SharedGeomObjectWrite write_object(std::uint32_t vertices, std::size_t records) {
  fo4::previs::SharedGeomObjectWrite object;
  object.data_offset = 0x40u;
  object.vertex_desc = 5u;
  object.vertex_count = vertices;
  object.triangle_count = {10u, 0u, 0u};
  object.records.resize(records);
  return object;
}

// Detects: a writer that accepts an object a u16 triangle index cannot address, or a count past the header's u32 totals, and returns a block the engine format cannot hold.
TEST(PrevisSharedGeomExtra, BuildRefusesWhatTheFormatCannotHold) {
  const auto donor = table_extra({k_object_a}, 4);
  const auto build = [&donor](const std::vector<fo4::previs::SharedGeomObjectWrite>& objects) {
    return fo4::previs::build_shared_geom_extra_checked(donor, 0x01020304u, objects);
  };
  using Error = fo4::previs::SharedGeomBuildError;

  EXPECT_TRUE(build({write_object(fo4::previs::k_shared_geom_max_object_vertices, 1)}).has_value());

  const auto too_many_vertices = build({write_object(fo4::previs::k_shared_geom_max_object_vertices + 1u, 1)});
  ASSERT_FALSE(too_many_vertices.has_value());
  EXPECT_EQ(too_many_vertices.error(), Error::vertex_count_exceeds_u16);
  const std::vector<fo4::previs::SharedGeomObjectWrite> oversized{
      write_object(fo4::previs::k_shared_geom_max_object_vertices + 1u, 1)};
  EXPECT_FALSE(fo4::previs::build_shared_geom_extra(donor, 0x01020304u, oversized).has_value());

  const auto overflow = build({write_object(fo4::previs::k_shared_geom_max_object_vertices, 70000)});
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error(), Error::totals_exceed_u32);

  const auto empty = build({});
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error(), Error::no_objects);

  const auto no_records = build({write_object(10u, 0)});
  ASSERT_FALSE(no_records.has_value());
  EXPECT_EQ(no_records.error(), Error::object_without_records);

  const auto too_many_objects = build(std::vector<fo4::previs::SharedGeomObjectWrite>(
      fo4::previs::k_shared_geom_max_objects + 1, write_object(10u, 1)));
  ASSERT_FALSE(too_many_objects.has_value());
  EXPECT_EQ(too_many_objects.error(), Error::too_many_objects);

  const auto bad_template = fo4::previs::build_shared_geom_extra_checked(
      std::vector<std::byte>(16, std::byte{0}), 1u, std::vector<fo4::previs::SharedGeomObjectWrite>{write_object(10u, 1)});
  ASSERT_FALSE(bad_template.has_value());
  EXPECT_EQ(bad_template.error(), Error::template_unsupported);
}

}  // namespace
