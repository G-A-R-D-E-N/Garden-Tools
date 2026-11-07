#include "fo4/forms/static_collection.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> make_scol(bool malformed = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
  fo4::test::ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "SCOL", 0);
  const auto scol = fo4::test::begin_record(out, "SCOL", 0, 0x01000800u);

  fo4::test::ByteWriter first_static;
  first_static.u32(0x00000810u);
  fo4::test::write_subrecord(out, "ONAM", first_static.bytes);
  fo4::test::ByteWriter first_placements;
  for (int placement = 0; placement < 2; ++placement) {
    const auto base = placement == 0 ? 1.0f : 10.0f;
    for (int field = 0; field < 7; ++field) {
      first_placements.f32(base + static_cast<float>(field));
    }
  }
  if (malformed) first_placements.bytes.pop_back();
  fo4::test::write_subrecord(out, "DATA", first_placements.bytes);

  fo4::test::ByteWriter second_static;
  second_static.u32(0x00000811u);
  fo4::test::write_subrecord(out, "ONAM", second_static.bytes);
  fo4::test::ByteWriter second_placements;
  for (int field = 0; field < 7; ++field) {
    second_placements.f32(20.0f + static_cast<float>(field));
  }
  fo4::test::write_subrecord(out, "DATA", second_placements.bytes);

  fo4::test::end_record(out, scol);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> scol_plugin(
    const std::vector<std::pair<std::string, std::vector<std::byte>>>& subrecords,
    std::string_view record_signature = "SCOL") {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
  fo4::test::ByteWriter master_data;
  master_data.u32(0);
  master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, record_signature, 0);
  const auto scol = fo4::test::begin_record(out, record_signature, 0, 0x01000800u);
  for (const auto& [signature, data] : subrecords) fo4::test::write_subrecord(out, signature, data);
  fo4::test::end_record(out, scol);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> placements(int count) {
  fo4::test::ByteWriter out;
  for (int placement = 0; placement < count; ++placement) {
    for (int field = 0; field < 7; ++field) {
      out.f32(static_cast<float>(placement * 10 + field));
    }
  }
  return out.bytes;
}

}  // namespace

TEST(StaticCollection, DecodesPartTargetsAndFixedSizePlacements) {
  auto plugin = fo4::read_plugin(make_scol(), "Collection.esp");
  const auto key = fo4::FormKey::parse("000800:Collection.esp");
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);

  const auto parts = fo4::forms::decode_static_collection_parts(plugin, *record);
  ASSERT_EQ(parts.size(), 2u);
  EXPECT_EQ(parts[0].static_record.to_string(), "000810:Base.esm");
  ASSERT_EQ(parts[0].placements.size(), 2u);
  EXPECT_FLOAT_EQ(parts[0].placements[0].x, 1.0f);
  EXPECT_FLOAT_EQ(parts[0].placements[0].rz, 6.0f);
  EXPECT_FLOAT_EQ(parts[0].placements[0].scale, 7.0f);
  EXPECT_FLOAT_EQ(parts[0].placements[1].x, 10.0f);
  EXPECT_FLOAT_EQ(parts[0].placements[1].scale, 16.0f);

  EXPECT_EQ(parts[1].static_record.to_string(), "000811:Base.esm");
  ASSERT_EQ(parts[1].placements.size(), 1u);
  EXPECT_FLOAT_EQ(parts[1].placements[0].x, 20.0f);
  EXPECT_FLOAT_EQ(parts[1].placements[0].scale, 26.0f);
}

TEST(StaticCollection, RejectsMalformedPlacementPayload) {
  auto plugin = fo4::read_plugin(make_scol(true), "Collection.esp");
  const auto* record = fo4::find_record(
      plugin, fo4::FormKey::parse("000800:Collection.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_THROW(
      fo4::forms::decode_static_collection_parts(plugin, *record),
      fo4::forms::StaticCollectionError);
}

TEST(StaticCollection, RejectsDuplicateDataArraysInOnePart) {
  auto plugin = fo4::read_plugin(
      scol_plugin({{"ONAM", u32_bytes(0x00000810u)}, {"DATA", placements(1)}, {"DATA", placements(1)}}),
      "Collection.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Collection.esp"));
  ASSERT_NE(record, nullptr);
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_static_collection_parts(plugin, *record)),
               fo4::forms::StaticCollectionError);
}

TEST(StaticCollection, DataBeforeAnyOnamIsDropped) {
  auto plugin = fo4::read_plugin(
      scol_plugin({{"DATA", placements(1)}, {"ONAM", u32_bytes(0x00000810u)}, {"DATA", placements(2)}}),
      "Collection.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Collection.esp"));
  ASSERT_NE(record, nullptr);

  const auto parts = fo4::forms::decode_static_collection_parts(plugin, *record);
  ASSERT_EQ(parts.size(), 1u);
  EXPECT_EQ(parts[0].static_record.to_string(), "000810:Base.esm");
  EXPECT_EQ(parts[0].placements.size(), 2u);
}

TEST(StaticCollection, OnamWithoutDataKeepsAnEmptyPart) {
  auto plugin = fo4::read_plugin(
      scol_plugin({{"ONAM", u32_bytes(0x00000810u)}, {"ONAM", u32_bytes(0x00000811u)}, {"DATA", placements(1)}}),
      "Collection.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:Collection.esp"));
  ASSERT_NE(record, nullptr);

  const auto parts = fo4::forms::decode_static_collection_parts(plugin, *record);
  ASSERT_EQ(parts.size(), 2u);
  EXPECT_TRUE(parts[0].placements.empty());
  EXPECT_EQ(parts[1].placements.size(), 1u);
}

TEST(StaticCollection, RejectsBadOnamSizeAndNonScolRecord) {
  const auto bad_onam = fo4::read_plugin(
      scol_plugin({{"ONAM", std::vector<std::byte>(3, std::byte{0})}}), "Collection.esp");
  const auto* bad_record = fo4::find_record(bad_onam, fo4::FormKey::parse("000800:Collection.esp"));
  ASSERT_NE(bad_record, nullptr);
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_static_collection_parts(bad_onam, *bad_record)),
               fo4::forms::StaticCollectionError);

  const auto stat = fo4::read_plugin(
      scol_plugin({{"ONAM", u32_bytes(0x00000810u)}}, "STAT"), "Collection.esp");
  const auto* stat_record = fo4::find_record(stat, fo4::FormKey::parse("000800:Collection.esp"));
  ASSERT_NE(stat_record, nullptr);
  EXPECT_THROW(static_cast<void>(fo4::forms::decode_static_collection_parts(stat, *stat_record)),
               fo4::forms::StaticCollectionError);
}
