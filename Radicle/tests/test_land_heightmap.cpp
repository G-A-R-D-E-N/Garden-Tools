#include "fo4/land/heightmap.hpp"

#include "fo4/environment/environment.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/error.hpp"
#include "fo4/forms/land_edit.hpp"
#include "../src/forms/land_normals.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "env_node_fixture.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

using fo4::land::LandHeightmap;
using fo4::land::decode_land_heightmap;
using fo4::land::encode_land_heightmap;
using fo4::land::land_heights;
using fo4::land::make_land_heightmap;

namespace {

std::array<std::byte, fo4::land::k_vhgt_size> payload(float offset,
                                                      const std::vector<int>& leading_deltas,
                                                      std::array<std::byte, 3> tail = {}) {
  std::array<std::byte, fo4::land::k_vhgt_size> bytes{};
  std::memcpy(bytes.data(), &offset, sizeof(offset));
  for (std::size_t index = 0; index < leading_deltas.size(); ++index) {
    bytes[4 + index] = static_cast<std::byte>(static_cast<std::uint8_t>(leading_deltas[index]));
  }
  for (std::size_t index = 0; index < tail.size(); ++index) {
    bytes[4 + fo4::land::k_heightmap_vertices + index] = tail[index];
  }
  return bytes;
}

float float_at(std::span<const std::byte> bytes, std::size_t offset) {
  float value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

}  // namespace

// Detects: a decoder that must read the measured 1096-byte layout, offset first and 1089 signed deltas after.
TEST(LandHeightmap, DecodesTheMeasuredLayout) {
  const auto bytes = payload(85.0f, {0, 2, 3, 3, 2, 2, 1, 0, -1, -1});
  const auto heightmap = decode_land_heightmap(bytes);
  EXPECT_EQ(heightmap.offset, 85.0f);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[0]), 0);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[2]), 3);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[8]), -1);
  EXPECT_EQ(static_cast<int>(heightmap.deltas[10]), 0);
  EXPECT_EQ(encode_land_heightmap(heightmap), bytes);
}

// Detects: a row start that must chain from the previous row's first height, not from its last height.
TEST(LandHeightmap, RowStartsChainFromThePreviousRowStart) {
  std::vector<int> deltas(fo4::land::k_heightmap_vertices, 1);
  const auto heightmap = decode_land_heightmap(payload(10.0f, deltas));
  const auto heights = land_heights(heightmap);
  EXPECT_EQ(heights[0], 88.0f);
  EXPECT_EQ(heights[32], 344.0f);
  EXPECT_EQ(heights[33], 96.0f);
  EXPECT_EQ(heights[34], 104.0f);
  EXPECT_EQ(heights[fo4::land::k_heightmap_vertices - 1], 600.0f);
}

// Detects: a writer that must reproduce every shipped payload, including the unused trailing bytes.
TEST(LandHeightmap, RoundTripsEveryPayloadByte) {
  const std::array<std::byte, 3> measured_tail{std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFC}};
  const auto bytes = payload(-1003.0f, {5, -6, 7, 8}, measured_tail);
  const auto heightmap = decode_land_heightmap(bytes);
  EXPECT_EQ(heightmap.trailing_bytes, measured_tail);
  EXPECT_EQ(encode_land_heightmap(heightmap), bytes);
}

// Detects: widths other than the measured 1096 bytes, which must be refused.
TEST(LandHeightmap, RejectsPayloadsOfAnotherWidth) {
  for (const std::size_t size : {std::size_t{0}, std::size_t{1095}, std::size_t{1097}}) {
    std::vector<std::byte> bytes(size, std::byte{0});
    EXPECT_THROW(decode_land_heightmap(bytes), fo4::parse_error) << size;
  }
}

// Detects: a non-finite offset, which must be refused instead of poisoning the height grid.
TEST(LandHeightmap, RejectsNonFiniteOffset) {
  auto bytes = payload(0.0f, {});
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(bytes.data(), &nan, sizeof(nan));
  EXPECT_THROW(decode_land_heightmap(bytes), fo4::parse_error);
}

TEST(LandHeightmap, RebuildingFromTheOriginalKeepsTheTrailingBytes) {
  const std::array<std::byte, 3> measured_tail{std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFC}};
  const auto bytes = payload(120.0f, {1, 2, -3, 4}, measured_tail);
  const auto original = decode_land_heightmap(bytes);
  const auto rebuilt = make_land_heightmap(land_heights(original), original);
  ASSERT_TRUE(rebuilt.has_value());
  EXPECT_EQ(rebuilt->trailing_bytes, measured_tail);
  EXPECT_EQ(encode_land_heightmap(*rebuilt), bytes);
}

// Detects: an encoder that must recover the exact deltas from a decoded grid.
TEST(LandHeightmap, BuildsDeltasFromHeights) {
  const auto bytes = payload(5597.0f, {0, 4, -3, 1});
  const auto original = decode_land_heightmap(bytes);
  const auto heights = land_heights(original);
  const auto rebuilt = make_land_heightmap(heights, original.offset);
  ASSERT_TRUE(rebuilt.has_value());
  EXPECT_EQ(rebuilt->deltas, original.deltas);
  EXPECT_EQ(*rebuilt, original);
}

// Detects: a step that must round to the nearest signed byte, because heights are integers plus an offset.
TEST(LandHeightmap, RoundsFractionalStepsToTheNearestByte) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[0] = 8.0f * 0.4f;
  heights[1] = 8.0f * 1.6f;
  const auto heightmap = make_land_heightmap(heights, 0.0f);
  ASSERT_TRUE(heightmap.has_value());
  EXPECT_EQ(static_cast<int>(heightmap->deltas[0]), 0);
  EXPECT_EQ(static_cast<int>(heightmap->deltas[1]), 2);
  EXPECT_EQ(land_heights(*heightmap)[1], 16.0f);
}

// Detects: a height grid whose step cannot fit a signed byte, which must be refused instead of wrapping.
TEST(LandHeightmap, RejectsHeightsWithTooLargeStep) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[5] = 8.0f * 200.0f;
  EXPECT_FALSE(make_land_heightmap(heights, 0.0f).has_value());
  std::vector<float> short_grid(10, 0.0f);
  EXPECT_FALSE(make_land_heightmap(short_grid, 0.0f).has_value());
}

// Detects: a non-finite height, which must be refused.
TEST(LandHeightmap, RejectsNonFiniteHeights) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[7] = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(make_land_heightmap(heights, 0.0f).has_value());
  heights[7] = 1.0f;
  EXPECT_FALSE(make_land_heightmap(heights, std::numeric_limits<float>::quiet_NaN()).has_value());
}

// Detects: a flat grid that must be representable with zero deltas and keep its offset.
TEST(LandHeightmap, EncodesAFlatGridWithoutDeltas) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 8.0f * 42.0f);
  const auto heightmap = make_land_heightmap(heights, 42.0f);
  ASSERT_TRUE(heightmap.has_value());
  for (const auto delta : heightmap->deltas) EXPECT_EQ(delta, 0);
  const auto bytes = encode_land_heightmap(*heightmap);
  EXPECT_EQ(float_at(bytes, 0), 42.0f);
  EXPECT_EQ(land_heights(*heightmap)[0], 336.0f);
}

namespace {

std::vector<std::byte> measured_vhgt(float offset) {
  LandHeightmap heightmap;
  heightmap.offset = offset;
  heightmap.trailing_bytes = {std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFC}};
  const auto bytes = encode_land_heightmap(heightmap);
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::vector<std::byte> land_base() {
  using namespace fo4::test::envnode;
  const auto with_land = rec("CELL", 0x806, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({2, 3, 0})}});
  const auto without_land = rec("CELL", 0x807, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({3, 3, 0})}});
  const auto land = rec("LAND", 0x8806, {{"VHGT", measured_vhgt(10.0f)}});
  return make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                       grp(0x800, 1, {grp(0, 4, {grp(0, 5, {with_land,
                                                                            grp(0x806, 6, {grp(0x806, 9, {land})}),
                                                                            without_land})})})})});
}

std::vector<std::byte> empty_patch() {
  return fo4::test::envnode::make_plugin({"Base.esm"}, {});
}

}  // namespace

class LandEditTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_.write("Base.esm", land_base(), 0);
    dir_.write("Patch.esp", empty_patch(), 1);
    paths_ = {dir_.path() / "Base.esm", dir_.path() / "Patch.esp"};
  }

  fo4::test::TempDataDir dir_;
  std::vector<std::filesystem::path> paths_;
};

// Detects: an edit that must copy the winning LAND into the patch and survive write and reread.
TEST_F(LandEditTest, HeightEditCopiesTheWinningLandAndRoundTripsThroughWriteAndReread) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000806:Base.esm");
  auto patch = fo4::read_plugin(empty_patch(), "Edit.esp");

  const std::vector<float> heights(fo4::land::k_heightmap_vertices, 120.0f);
  const auto result = fo4::forms::edit_land_heights(patch, env, cell, heights);
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.copied_from, "Base.esm");

  dir_.write("Readback.esp", fo4::write_plugin(patch), 2);
  const std::vector<std::filesystem::path> order{paths_[0], paths_[1], dir_.path() / "Readback.esp"};
  const auto reread = fo4::env::Environment::load(order);
  const auto* land = reread.cell_landscape(cell);
  ASSERT_NE(land, nullptr);
  const auto grid = fo4::env::decode_land_heights(fo4::env::decode_landscape(reread, *land));
  ASSERT_TRUE(grid.has_value());
  EXPECT_FLOAT_EQ(grid->base, 10.0f);
  EXPECT_FLOAT_EQ(grid->heights.front(), 120.0f);
  EXPECT_FLOAT_EQ(grid->heights.back(), 120.0f);
}

// Detects: a second edit that must rewrite the patch LAND instead of copying it again or writing the CELL.
TEST_F(LandEditTest, SecondEditRewritesThePatchLandWithoutCopyingAgain) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000806:Base.esm");
  auto patch = fo4::read_plugin(empty_patch(), "Edit.esp");

  ASSERT_EQ(fo4::forms::edit_land_heights(patch, env, cell, std::vector<float>(fo4::land::k_heightmap_vertices, 40.0f)).status,
            fo4::forms::LandEditStatus::edited);
  std::vector<float> raised(fo4::land::k_heightmap_vertices, 40.0f);
  raised[fo4::land::k_heightmap_side] = 48.0f;
  const auto second = fo4::forms::edit_land_heights(patch, env, cell, raised);
  ASSERT_EQ(second.status, fo4::forms::LandEditStatus::edited);
  EXPECT_TRUE(second.copied_from.empty());

  dir_.write("Readback.esp", fo4::write_plugin(patch), 2);
  const std::vector<std::filesystem::path> order{paths_[0], paths_[1], dir_.path() / "Readback.esp"};
  const auto reread = fo4::env::Environment::load(order);
  const auto* land = reread.cell_landscape(cell);
  ASSERT_NE(land, nullptr);
  const auto grid = fo4::env::decode_land_heights(fo4::env::decode_landscape(reread, *land));
  ASSERT_TRUE(grid.has_value());
  EXPECT_FLOAT_EQ(grid->heights[fo4::land::k_heightmap_side], 48.0f);
  EXPECT_FLOAT_EQ(grid->heights[0], 40.0f);
}

// Detects: refusals that must leave the patch byte-identical, with no half-copied LAND or CELL.
TEST_F(LandEditTest, RefusalsLeaveThePatchUnchanged) {
  const auto env = fo4::env::Environment::load(paths_);
  auto patch = fo4::read_plugin(empty_patch(), "Edit.esp");
  const auto before = fo4::write_plugin(patch);
  std::vector<float> heights(fo4::land::k_heightmap_vertices, 50.0f);

  EXPECT_EQ(fo4::forms::edit_land_heights(patch, env, fo4::FormKey::parse("000999:Base.esm"), heights).status,
            fo4::forms::LandEditStatus::cell_missing);
  EXPECT_EQ(fo4::forms::edit_land_heights(patch, env, fo4::FormKey::parse("000800:Base.esm"), heights).status,
            fo4::forms::LandEditStatus::not_a_cell);
  EXPECT_EQ(fo4::forms::edit_land_heights(patch, env, fo4::FormKey::parse("000807:Base.esm"), heights).status,
            fo4::forms::LandEditStatus::land_missing);
  EXPECT_EQ(fo4::forms::edit_land_heights(patch, env, fo4::FormKey::parse("000806:Base.esm"), std::vector<float>(4, 1.0f)).status,
            fo4::forms::LandEditStatus::invalid_value);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

// Detects: a step too steep for the signed byte deltas, which must be refused rather than clamped.
TEST_F(LandEditTest, SteepStepIsRefusedWithoutWritingTheLand) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = fo4::FormKey::parse("000806:Base.esm");
  auto patch = fo4::read_plugin(empty_patch(), "Edit.esp");
  const auto before = fo4::write_plugin(patch);

  std::vector<float> heights(fo4::land::k_heightmap_vertices, 0.0f);
  heights[5] = fo4::land::k_land_height_scale * 200.0f;
  EXPECT_EQ(fo4::forms::edit_land_heights(patch, env, cell, heights).status,
            fo4::forms::LandEditStatus::invalid_value);
  EXPECT_TRUE(fo4::write_plugin(patch) == before);
}

namespace {

std::vector<std::byte> texture_layer(std::uint32_t texture) {
  fo4::test::ByteWriter out;
  out.u32(texture);
  out.u8(0);
  out.u8(0);
  out.u16(0xFFFF);
  return out.bytes;
}

fo4::test::envnode::Node cell_with_optional_land(std::uint32_t world,
                                                 std::uint32_t cell,
                                                 const char* editor_id,
                                                 std::vector<fo4::test::envnode::Node> land) {
  using namespace fo4::test::envnode;
  auto node = rec("CELL", cell, {{"EDID", zstring(editor_id)}, {"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({2, 3, 0})}});
  std::vector<Node> block{std::move(node)};
  if (!land.empty()) block.push_back(grp(cell, 6, {grp(cell, 9, std::move(land))}));
  return top("WRLD", {rec("WRLD", world, {{"EDID", zstring("TestWorld")}}),
                      grp(world, 1, {grp(0, 4, {grp(0, 5, std::move(block))})})});
}

}  // namespace

// Detects: a patch CELL copied from the plugin that holds the winning LAND instead of the plugin that holds the winning CELL.
TEST(LandEdit, ThePatchCellComesFromTheWinningCellPluginNotTheLandPlugin) {
  using namespace fo4::test::envnode;
  fo4::test::TempDataDir dir;
  dir.write("A.esm", make_plugin({}, {cell_with_optional_land(0x800, 0x806, "FromA", {rec("LAND", 0x8806, {{"VHGT", measured_vhgt(10.0f)}})})}), 0);
  dir.write("C.esp", make_plugin({"A.esm"}, {cell_with_optional_land(0x800, 0x806, "FromC", {rec("LAND", 0x8806, {{"VHGT", measured_vhgt(77.0f)}})})}), 1);
  dir.write("B.esp", make_plugin({"A.esm"}, {cell_with_optional_land(0x800, 0x806, "FromB", {})}), 2);
  const std::vector<std::filesystem::path> order{dir.path() / "A.esm", dir.path() / "C.esp", dir.path() / "B.esp"};
  const auto env = fo4::env::Environment::load(order);
  const auto cell = fo4::FormKey::parse("000806:A.esm");
  auto patch = fo4::read_plugin(make_plugin({}, {}), "Edit.esp");

  const auto result = fo4::forms::edit_land_heights(patch, env, cell, std::vector<float>(fo4::land::k_heightmap_vertices, 120.0f));
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(result.copied_from, "C.esp");

  const auto* copied = fo4::find_record(patch, cell);
  ASSERT_NE(copied, nullptr);
  bool found = false;
  for (const auto& sub : copied->subrecords) {
    if (sub.signature != fo4::Signature::from_string("EDID")) continue;
    found = true;
    EXPECT_EQ(sub.data, zstring("FromB"));
  }
  EXPECT_TRUE(found);
}

// Detects: a copied LAND whose texture master is missing from the patch, which made the encode throw and surfaced as malformed.
TEST(LandEdit, ALandTextureOwnerBecomesAMasterOfThePatch) {
  using namespace fo4::test::envnode;
  fo4::test::TempDataDir dir;
  dir.write("Tex.esm", make_plugin({}, {top("LTEX", {rec("LTEX", 0x800, {{"EDID", zstring("Dirt")}})})}), 0);
  dir.write("Base.esm",
            make_plugin({"Tex.esm"},
                        {cell_with_optional_land(0x01000800, 0x01000806, "Textured",
                                                 {rec("LAND", 0x01008806, {{"VHGT", measured_vhgt(10.0f)}, {"BTXT", texture_layer(0x800)}})})}),
            1);
  const std::vector<std::filesystem::path> order{dir.path() / "Tex.esm", dir.path() / "Base.esm"};
  const auto env = fo4::env::Environment::load(order);
  const auto cell = fo4::FormKey::parse("000806:Base.esm");
  auto patch = fo4::read_plugin(make_plugin({}, {}), "Edit.esp");

  const auto result = fo4::forms::edit_land_heights(patch, env, cell, std::vector<float>(fo4::land::k_heightmap_vertices, 120.0f));
  ASSERT_EQ(result.status, fo4::forms::LandEditStatus::edited);

  const auto* land = fo4::find_record(patch, fo4::FormKey::parse("008806:Base.esm"));
  ASSERT_NE(land, nullptr);
  const auto btxt = std::find_if(land->subrecords.begin(), land->subrecords.end(), [](const fo4::Subrecord& sub) {
    return sub.signature == fo4::Signature::from_string("BTXT");
  });
  ASSERT_NE(btxt, land->subrecords.end());
  std::uint32_t raw = 0;
  std::memcpy(&raw, btxt->data.data(), sizeof(raw));
  ASSERT_LT(raw >> 24, patch.header.masters.size());
  EXPECT_EQ(patch.header.masters[raw >> 24].key, fo4::PluginKey::from_filename("Tex.esm"));
  EXPECT_EQ(raw & 0x00FFFFFFu, 0x800u);
}

namespace {

std::vector<std::byte> pseudo_random_vnml() {
  std::vector<std::byte> vnml(3267);
  for (std::size_t index = 0; index < vnml.size(); ++index) {
    vnml[index] = static_cast<std::byte>((index * 7 + 3) % 251);
  }
  return vnml;
}

std::vector<fo4::test::envnode::Sub> shipped_land_subrecords() {
  using namespace fo4::test::envnode;
  std::vector<std::byte> vclr(3267);
  for (std::size_t index = 0; index < vclr.size(); ++index) {
    vclr[index] = static_cast<std::byte>((index * 11 + 5) % 241);
  }
  return {{"DATA", bytes_of({0x0D})},
          {"VNML", pseudo_random_vnml()},
          {"VHGT", measured_vhgt(10.0f)},
          {"VCLR", vclr},
          {"BTXT", std::vector<std::byte>(8, std::byte{0})}};
}

const fo4::Record* edited_land(const fo4::Plugin& patch) {
  return fo4::find_record(patch, fo4::FormKey::parse("008806:Base.esm"));
}

const fo4::Subrecord* subrecord_named(const fo4::Record& record, std::string_view name) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == name) return &sub;
  }
  return nullptr;
}

std::int8_t signed_byte(const fo4::Subrecord& sub, std::size_t row, std::size_t column, std::size_t axis) {
  return static_cast<std::int8_t>(std::to_integer<std::uint8_t>(sub.data[(row * 33 + column) * 3 + axis]));
}

fo4::Plugin shipped_land_patch(fo4::test::TempDataDir& dir, std::optional<fo4::env::Environment>& env) {
  using namespace fo4::test::envnode;
  dir.write("Base.esm",
            make_plugin({}, {cell_with_optional_land(0x800, 0x806, "Shipped", {rec("LAND", 0x8806, shipped_land_subrecords())})}),
            0);
  env.emplace(fo4::env::Environment::load(std::vector<std::filesystem::path>{dir.path() / "Base.esm"}));
  return fo4::read_plugin(make_plugin({}, {}), "Edit.esp");
}

}  // namespace

// Detects: a heights edit that rewrites or reorders more than VNML and VHGT, or that changes the VNML length.
TEST(LandEdit, AHeightEditRewritesOnlyVnmlAndVhgtAndKeepsTheVnmlLength) {
  fo4::test::TempDataDir dir;
  std::optional<fo4::env::Environment> env;
  auto patch = shipped_land_patch(dir, env);
  const auto shipped = shipped_land_subrecords();

  std::vector<float> heights(fo4::land::k_heightmap_vertices, 120.0f);
  heights[fo4::land::k_heightmap_side + 1] = 160.0f;
  ASSERT_EQ(fo4::forms::edit_land_heights(patch, *env, fo4::FormKey::parse("000806:Base.esm"), heights).status,
            fo4::forms::LandEditStatus::edited);

  const auto* land = edited_land(patch);
  ASSERT_NE(land, nullptr);
  ASSERT_EQ(land->subrecords.size(), shipped.size());
  for (std::size_t index = 0; index < shipped.size(); ++index) {
    const auto& sub = land->subrecords[index];
    EXPECT_EQ(sub.signature.to_string(), shipped[index].signature);
    if (shipped[index].signature == "VHGT" || shipped[index].signature == "VNML") {
      EXPECT_NE(sub.data, shipped[index].data) << shipped[index].signature;
    } else {
      EXPECT_EQ(sub.data, shipped[index].data) << shipped[index].signature;
    }
  }
  EXPECT_EQ(subrecord_named(*land, "VNML")->data.size(), 3267u);
}

// Detects: the raw heightmap entry rewriting VNML, which it has no float heights to derive from.
TEST(LandEdit, TheRawHeightmapEditLeavesVnmlAsShipped) {
  fo4::test::TempDataDir dir;
  std::optional<fo4::env::Environment> env;
  auto patch = shipped_land_patch(dir, env);

  LandHeightmap heightmap;
  heightmap.offset = 33.0f;
  ASSERT_EQ(fo4::forms::edit_land_heightmap(patch, *env, fo4::FormKey::parse("000806:Base.esm"), heightmap).status,
            fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(subrecord_named(*edited_land(patch), "VNML")->data, pseudo_random_vnml());
}

// Detects: a recompute that overwrites shipped normals far from the edit, which keep the exact CK bytes.
TEST(LandEdit, OnlyVerticesNextToAChangedHeightGetNewNormals) {
  fo4::test::TempDataDir dir;
  std::optional<fo4::env::Environment> env;
  auto patch = shipped_land_patch(dir, env);
  const auto shipped = pseudo_random_vnml();

  std::vector<float> heights(fo4::land::k_heightmap_vertices, 80.0f);
  heights[16 * 33 + 16] += 64.0f;
  ASSERT_EQ(fo4::forms::edit_land_heights(patch, *env, fo4::FormKey::parse("000806:Base.esm"), heights).status,
            fo4::forms::LandEditStatus::edited);

  const auto& vnml = *subrecord_named(*edited_land(patch), "VNML");
  for (std::size_t row = 0; row < 33; ++row) {
    for (std::size_t column = 0; column < 33; ++column) {
      const bool near_spike = row >= 15 && row <= 17 && column >= 15 && column <= 17;
      const auto offset = static_cast<std::ptrdiff_t>((row * 33 + column) * 3);
      const bool same = std::equal(vnml.data.begin() + offset, vnml.data.begin() + offset + 3, shipped.begin() + offset);
      EXPECT_EQ(same, !near_spike) << row << "," << column;
    }
  }
  EXPECT_EQ(signed_byte(vnml, 16, 16, 0), 0);
  EXPECT_EQ(signed_byte(vnml, 16, 16, 1), 0);
  EXPECT_EQ(signed_byte(vnml, 16, 16, 2), 127);
  EXPECT_EQ(signed_byte(vnml, 16, 15, 0), -signed_byte(vnml, 16, 17, 0));
  EXPECT_GT(signed_byte(vnml, 16, 17, 0), 0);
}

// Detects: an edit whose heights equal the shipped ones rewriting VNML with the approximation instead of keeping the CK bytes.
TEST(LandEdit, AnUnchangedHeightGridKeepsEveryShippedNormal) {
  fo4::test::TempDataDir dir;
  std::optional<fo4::env::Environment> env;
  auto patch = shipped_land_patch(dir, env);

  ASSERT_EQ(fo4::forms::edit_land_heights(patch, *env, fo4::FormKey::parse("000806:Base.esm"),
                                          std::vector<float>(fo4::land::k_heightmap_vertices, 80.0f))
                .status,
            fo4::forms::LandEditStatus::edited);
  EXPECT_EQ(subrecord_named(*edited_land(patch), "VNML")->data, pseudo_random_vnml());
}

// Detects: a border normal computed one-sided although the east neighbour's LAND is available in the environment.
TEST(LandEdit, ABorderNormalUsesTheNeighbourLandWhenTheEnvironmentHasIt) {
  using namespace fo4::test::envnode;
  LandHeightmap east_map;
  east_map.offset = 10.0f;
  for (std::size_t row = 0; row < 33; ++row) east_map.deltas[row * 33 + 1] = 16;
  const auto east_vhgt = encode_land_heightmap(east_map);
  const auto cell = [&](std::uint32_t id, std::uint32_t x, std::uint32_t land, std::vector<std::byte> vhgt) {
    return std::vector<Node>{rec("CELL", id, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({x, 3, 0})}}),
                             grp(id, 6, {grp(id, 9, {rec("LAND", land, {{"VNML", pseudo_random_vnml()}, {"VHGT", std::move(vhgt)}})})})};
  };
  auto block = cell(0x806, 2, 0x8806, measured_vhgt(10.0f));
  for (auto& node : cell(0x807, 3, 0x8807, std::vector<std::byte>(east_vhgt.begin(), east_vhgt.end()))) block.push_back(std::move(node));
  fo4::test::TempDataDir dir;
  dir.write("Base.esm",
            make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                          grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(block))})})})}),
            0);
  const auto env = fo4::env::Environment::load(std::vector<std::filesystem::path>{dir.path() / "Base.esm"});
  auto patch = fo4::read_plugin(make_plugin({}, {}), "Edit.esp");

  ASSERT_EQ(fo4::forms::edit_land_heights(patch, env, fo4::FormKey::parse("000806:Base.esm"),
                                          std::vector<float>(fo4::land::k_heightmap_vertices, 120.0f))
                .status,
            fo4::forms::LandEditStatus::edited);

  const auto& vnml = *subrecord_named(*edited_land(patch), "VNML");
  EXPECT_LT(signed_byte(vnml, 16, 32, 0), 0);
  EXPECT_EQ(signed_byte(vnml, 16, 0, 0), 0);
  EXPECT_EQ(signed_byte(vnml, 16, 31, 0), 0);
}

namespace {

constexpr std::size_t k_side = fo4::land::k_heightmap_side;

std::vector<float> grid_of(float (*height)(float, float)) {
  std::vector<float> heights(fo4::land::k_heightmap_vertices);
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      heights[row * k_side + column] = height(static_cast<float>(row), static_cast<float>(column));
    }
  }
  return heights;
}

std::array<int, 3> normal_at(const std::array<std::int8_t, fo4::land::k_normals_size>& normals,
                             std::size_t row,
                             std::size_t column) {
  const auto base = (row * k_side + column) * 3;
  return {normals[base], normals[base + 1], normals[base + 2]};
}

}  // namespace

// Detects: a flat grid that must give straight-up bytes (0, 0, 127), the shipped value for flat terrain.
TEST(LandNormals, FlatHeightsGiveStraightUpBytesEverywhere) {
  const auto normals = fo4::land::compute_land_normals(std::vector<float>(fo4::land::k_heightmap_vertices, 512.0f));
  for (std::size_t index = 0; index < normals.size(); index += 3) {
    ASSERT_EQ(normals[index], 0) << index;
    ASSERT_EQ(normals[index + 1], 0) << index;
    ASSERT_EQ(normals[index + 2], 127) << index;
  }
}

// Detects: a wrong axis, sign or vertex spacing; a plane rising 32 units per column has dh/dx = 0.25 per world unit.
TEST(LandNormals, AUniformTiltGivesTheAnalyticNormalOnEveryVertexIncludingBorders) {
  const auto along_x = fo4::land::compute_land_normals(grid_of([](float, float column) { return 32.0f * column; }));
  const auto along_y = fo4::land::compute_land_normals(grid_of([](float row, float) { return 32.0f * row; }));
  const double length = std::sqrt(1.0 + 0.25 * 0.25);
  const int tilted = static_cast<int>(std::trunc(0.25 / length * 127.0));
  const int up = static_cast<int>(std::trunc(1.0 / length * 127.0));
  ASSERT_EQ(tilted, 30);
  ASSERT_EQ(up, 123);
  for (std::size_t row = 0; row < k_side; ++row) {
    for (std::size_t column = 0; column < k_side; ++column) {
      ASSERT_EQ(normal_at(along_x, row, column), (std::array<int, 3>{-tilted, 0, up})) << row << "," << column;
      ASSERT_EQ(normal_at(along_y, row, column), (std::array<int, 3>{0, -tilted, up})) << row << "," << column;
    }
  }
}

// Detects: a formula drift from the CK's triangle list (a quad average, a fixed diagonal, raw instead of normalised faces, quadrant-only seams, a lost corner pad).
// Expected bytes come from a separate numpy float32 implementation of the CK arithmetic, including its sqrt table.
TEST(LandNormals, MatchesIndependentlyComputedBytesOnASyntheticTerrain) {
  const auto own = grid_of([](float r, float c) { return 4.0f * static_cast<float>((static_cast<int>(r) * 7 + static_cast<int>(c) * 3) % 11) + r * c; });
  const auto east = grid_of([](float r, float c) { return 100.0f + 5.0f * r + 3.0f * c; });
  const auto west = grid_of([](float r, float c) { return 7.0f * r - 2.0f * c; });
  const auto north = grid_of([](float r, float c) { return 3.0f * c + r * r; });
  const auto south = grid_of([](float r, float c) { return 2.0f * r + static_cast<float>((static_cast<int>(c) * static_cast<int>(c)) % 5) * 16.0f; });
  const auto north_east = grid_of([](float r, float c) { return 60.0f + 2.0f * r - 4.0f * c + static_cast<float>((static_cast<int>(r) * static_cast<int>(c)) % 5) * 8.0f; });
  const auto north_west = grid_of([](float r, float c) { return 20.0f - r + 6.0f * c; });
  const auto south_east = grid_of([](float r, float c) { return 90.0f + static_cast<float>((static_cast<int>(r) + static_cast<int>(c)) % 4) * 24.0f; });
  const auto south_west = grid_of([](float r, float c) { return 2.5f * r - 3.0f * c; });
  fo4::land::HeightGrid east_grid, west_grid, north_grid, south_grid, ne_grid, nw_grid, se_grid, sw_grid;
  std::copy(east.begin(), east.end(), east_grid.begin());
  std::copy(west.begin(), west.end(), west_grid.begin());
  std::copy(north.begin(), north.end(), north_grid.begin());
  std::copy(south.begin(), south.end(), south_grid.begin());
  std::copy(north_east.begin(), north_east.end(), ne_grid.begin());
  std::copy(north_west.begin(), north_west.end(), nw_grid.begin());
  std::copy(south_east.begin(), south_east.end(), se_grid.begin());
  std::copy(south_west.begin(), south_west.end(), sw_grid.begin());

  const auto one_sided = fo4::land::compute_land_normals(own);
  const auto bordered = fo4::land::compute_land_normals(own, {&east_grid, &west_grid, &north_grid, &south_grid});
  const auto full = fo4::land::compute_land_normals(own, {&east_grid, &west_grid, &north_grid, &south_grid, &ne_grid, &nw_grid, &se_grid, &sw_grid});

  using Bytes = std::array<int, 3>;
  EXPECT_EQ(normal_at(bordered, 0, 0), (Bytes{-40, 12, 119}));
  EXPECT_EQ(normal_at(bordered, 0, 32), (Bytes{-24, 11, 124}));
  EXPECT_EQ(normal_at(bordered, 32, 0), (Bytes{13, 26, 123}));
  EXPECT_EQ(normal_at(bordered, 32, 32), (Bytes{61, 52, 97}));
  EXPECT_EQ(normal_at(bordered, 16, 32), (Bytes{62, -27, 107}));
  EXPECT_EQ(normal_at(bordered, 16, 0), (Bytes{6, 0, 126}));
  EXPECT_EQ(normal_at(bordered, 0, 16), (Bytes{-10, 25, 124}));
  EXPECT_EQ(normal_at(bordered, 32, 16), (Bytes{-15, 70, 104}));
  EXPECT_EQ(normal_at(bordered, 10, 10), (Bytes{-5, -9, 126}));
  EXPECT_EQ(normal_at(bordered, 10, 11), (Bytes{-21, 4, 125}));
  EXPECT_EQ(normal_at(bordered, 11, 10), (Bytes{0, -15, 126}));
  EXPECT_EQ(normal_at(bordered, 16, 16), (Bytes{-16, -10, 125}));
  EXPECT_EQ(normal_at(bordered, 16, 17), (Bytes{-5, -21, 125}));
  EXPECT_EQ(normal_at(bordered, 17, 16), (Bytes{-6, -20, 125}));
  EXPECT_EQ(normal_at(bordered, 20, 16), (Bytes{-15, -15, 125}));
  EXPECT_EQ(normal_at(bordered, 16, 21), (Bytes{-5, -25, 124}));
  EXPECT_EQ(normal_at(one_sided, 0, 0), (Bytes{-11, -27, 123}));
  EXPECT_EQ(normal_at(one_sided, 0, 32), (Bytes{-11, -15, 125}));
  EXPECT_EQ(normal_at(one_sided, 32, 0), (Bytes{-39, 14, 120}));
  EXPECT_EQ(normal_at(one_sided, 32, 32), (Bytes{0, -14, 126}));
  EXPECT_EQ(normal_at(one_sided, 16, 32), (Bytes{-26, -34, 119}));
  EXPECT_EQ(normal_at(one_sided, 16, 0), (Bytes{-16, -4, 126}));
  EXPECT_EQ(normal_at(one_sided, 0, 16), (Bytes{-11, -10, 126}));
  EXPECT_EQ(normal_at(one_sided, 32, 16), (Bytes{-19, -20, 123}));
  EXPECT_EQ(normal_at(one_sided, 10, 10), (Bytes{-5, -9, 126}));
  EXPECT_EQ(normal_at(full, 0, 0), (Bytes{-38, 15, 120}));
  EXPECT_EQ(normal_at(full, 0, 32), (Bytes{-29, 18, 122}));
  EXPECT_EQ(normal_at(full, 32, 0), (Bytes{44, 5, 118}));
  EXPECT_EQ(normal_at(full, 32, 32), (Bytes{72, 72, 75}));
  EXPECT_EQ(normal_at(full, 16, 32), (Bytes{62, -27, 107}));
}

// Detects: a changed-vertex mask that over-flags (replacing shipped CK bytes with a recompute) or under-flags (leaving stale bytes);
// a vertex belongs to 8 triangles when (row ^ column) is even and to 4 when it is odd, so the footprint is a 3 x 3 block or a plus.
TEST(LandNormals, AChangedHeightFlagsExactlyTheVerticesSharingATriangleWithIt) {
  const std::vector<float> before(fo4::land::k_heightmap_vertices, 80.0f);
  const auto flagged = [&](std::size_t row, std::size_t column) {
    auto after = before;
    after[row * k_side + column] += 8.0f;
    return fo4::land::changed_normal_vertices(before, {}, after, {});
  };
  for (const auto& [row, column] : {std::pair<std::size_t, std::size_t>{10, 10}, {10, 11}, {16, 16}, {17, 16}}) {
    const auto changed = flagged(row, column);
    const bool even = ((row ^ column) & 1) == 0;
    for (std::size_t r = 0; r < k_side; ++r) {
      for (std::size_t c = 0; c < k_side; ++c) {
        const auto dr = r > row ? r - row : row - r;
        const auto dc = c > column ? c - column : column - c;
        const bool expected = dr <= 1 && dc <= 1 && (even || dr + dc <= 1);
        EXPECT_EQ(changed[r * k_side + c], expected) << row << "," << column << " -> " << r << "," << c;
      }
    }
  }
}
