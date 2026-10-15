#include "fo4/forms/static_collection.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;
using fo4::forms::StaticCollectionError;
using fo4::forms::StaticPlacementView;

using Subrecords = std::vector<std::pair<std::string_view, Bytes>>;

Bytes i16_bytes(std::initializer_list<std::int16_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u16(static_cast<std::uint16_t>(value));
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes placement_bytes(std::initializer_list<float> bases) {
  ByteWriter out;
  for (const float base : bases) {
    for (int field = 0; field < 7; ++field) out.f32(base + static_cast<float>(field));
  }
  return out.bytes;
}

StaticPlacementView placement(float base) {
  StaticPlacementView view;
  view.x = base;
  view.y = base + 1.0f;
  view.z = base + 2.0f;
  view.rx = base + 3.0f;
  view.ry = base + 4.0f;
  view.rz = base + 5.0f;
  view.scale = base + 6.0f;
  return view;
}

void add_record(ByteWriter& out,
                std::string_view signature,
                std::uint32_t local,
                const Subrecords& subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local);
  for (const auto& [name, data] : subrecords) fo4::test::write_subrecord(out, name, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

ByteWriter make_plugin() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(4);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
  add_record(out, "STAT", 0xE10, {{"EDID", zstr("ScolPartA")}});
  add_record(out, "ARMO", 0xE11, {{"EDID", zstr("ScolArmor")}});
  add_record(out, "SCOL", 0xE01,
             {{"EDID", zstr("ScolTwoParts")},
              {"OBND", i16_bytes({-1, -2, -3, 4, 5, 6})},
              {"PTRN", u32_bytes(0x01000E30u)},
              {"MODL", zstr("Scol\\Old.nif")},
              {"FLTR", zstr("OldFilter")},
              {"ONAM", u32_bytes(0x02000E10u)},
              {"DATA", placement_bytes({1.0f, 10.0f})},
              {"ONAM", u32_bytes(0x01000E21u)},
              {"DATA", placement_bytes({20.0f})}});
  add_record(out, "SCOL", 0xE02,
             {{"EDID", zstr("ScolOnePart")},
              {"OBND", i16_bytes({-1, -2, -3, 4, 5, 6})},
              {"ONAM", u32_bytes(0x02000E10u)},
              {"DATA", placement_bytes({30.0f})}});
  add_record(out, "SCOL", 0xE03,
             {{"EDID", zstr("ScolNoData")},
              {"OBND", i16_bytes({-1, -2, -3, 4, 5, 6})},
              {"ONAM", u32_bytes(0x02000E10u)},
              {"ONAM", u32_bytes(0x02000E10u)},
              {"DATA", placement_bytes({40.0f})}});
  return out;
}

Bytes subrecord_data(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  ADD_FAILURE() << subrecord;
  return {};
}

std::vector<std::string> signatures_of(const fo4::Plugin& plugin, std::string_view key) {
  std::vector<std::string> names;
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return names;
  }
  for (const auto& sub : record->subrecords) names.push_back(sub.signature.to_string());
  return names;
}

std::vector<fo4::forms::StaticCollectionPartView> parts_of(const fo4::Plugin& plugin,
                                                           std::string_view key) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  return fo4::forms::decode_static_collection_parts(plugin, *record);
}

fo4::Plugin fixture() {
  const auto out = make_plugin();
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

fo4::Plugin reread(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

}  // namespace

TEST(WP9Scol, BoundsModelFilterAndPreviewRowsReadAndWrite) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E01:Patch.esp");
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("SCOL"),
                                                          "SCOL\\BoundMaxZ");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("OBND"));
  EXPECT_EQ(field->scalar.offset, 10u);
  EXPECT_EQ(field->scalar.payload_size, 12u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, "-7"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "SCOL\\Model", 0, "Scol\\New.nif"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "SCOL\\Filter", 0, "NewFilter"),
            fo4::ScalarMutationStatus::updated);

  const auto preview = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("SCOL"),
                                                              "SCOL\\PreviewTransform");
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->subrecord, fo4::Signature::from_string("PTRN"));
  EXPECT_FALSE(preview->nullable);
  EXPECT_EQ(preview->payload_sizes[0], 4u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *preview,
                                           fo4::FormKey::parse("000E20:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reread(plugin);
  const auto expected_bounds = i16_bytes({-1, -2, -3, 4, 5, -7});
  EXPECT_EQ(subrecord_data(after, "000E01:Patch.esp", "OBND"), expected_bounds);
  EXPECT_EQ(subrecord_data(after, "000E01:Patch.esp", "MODL"), zstr("Scol\\New.nif"));
  EXPECT_EQ(subrecord_data(after, "000E01:Patch.esp", "FLTR"), zstr("NewFilter"));
  EXPECT_EQ(subrecord_data(after, "000E01:Patch.esp", "PTRN"), u32_bytes(0x01000E20u));
}

TEST(WP9Scol, PartStaticRowIsRepeatedNonNullableOnam) {
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("SCOL"),
                                                            "SCOL\\PartStatic");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->subrecord, fo4::Signature::from_string("ONAM"));
  EXPECT_FALSE(field->nullable);
  EXPECT_EQ(field->payload_sizes[0], 4u);
  const auto targets = fo4::schema::formlink_field_targets(*field);
  EXPECT_EQ(targets.size(), 13u);
  const auto has = [&](std::string_view name) {
    return std::any_of(targets.begin(), targets.end(),
                       [&](const fo4::Signature& target) { return target.to_string() == name; });
  };
  EXPECT_TRUE(has("STAT"));
  EXPECT_TRUE(has("FLOR"));
  EXPECT_FALSE(has("ARMO"));
  EXPECT_FALSE(has("TRNS"));
}

TEST(WP9Scol, AddPartAppendsAfterExistingParts) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E01:Patch.esp");
  const auto stat = fo4::FormKey::parse("000E10:Patch.esp");
  std::vector<StaticPlacementView> placements;
  placements.push_back(placement(50.0f));
  placements.push_back(placement(60.0f));
  EXPECT_TRUE(fo4::forms::add_static_collection_part(plugin, key, stat, placements));

  const auto after = reread(plugin);
  const auto parts = parts_of(after, "000E01:Patch.esp");
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[0].static_record.to_string(), "000E10:Patch.esp");
  EXPECT_EQ(parts[1].static_record.to_string(), "000E21:Objects.esp");
  EXPECT_EQ(parts[2].static_record.to_string(), "000E10:Patch.esp");
  ASSERT_EQ(parts[2].placements.size(), 2u);
  EXPECT_FLOAT_EQ(parts[2].placements[0].x, 50.0f);
  EXPECT_FLOAT_EQ(parts[2].placements[0].scale, 56.0f);
  EXPECT_FLOAT_EQ(parts[2].placements[1].rz, 65.0f);
  const auto names = signatures_of(after, "000E01:Patch.esp");
  ASSERT_GE(names.size(), 2u);
  EXPECT_EQ(names[names.size() - 2], "ONAM");
  EXPECT_EQ(names.back(), "DATA");
}

TEST(WP9Scol, AddPartRefusesEmptyPlacements) {
  auto plugin = fixture();
  const std::span<const StaticPlacementView> none;
  EXPECT_THROW(fo4::forms::add_static_collection_part(plugin, fo4::FormKey::parse("000E02:Patch.esp"),
                                                      fo4::FormKey::parse("000E10:Patch.esp"), none),
               StaticCollectionError);
  EXPECT_EQ(parts_of(plugin, "000E02:Patch.esp").size(), 1u);
}

TEST(WP9Scol, AddPartRefusesWrongAndUnresolvedTargets) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E01:Patch.esp");
  std::vector<StaticPlacementView> one;
  one.push_back(placement(10.0f));
  EXPECT_THROW(fo4::forms::add_static_collection_part(plugin, key, fo4::FormKey::parse("000E11:Patch.esp"), one),
               StaticCollectionError);
  EXPECT_THROW(fo4::forms::add_static_collection_part(plugin, key, fo4::FormKey::parse("000E99:Patch.esp"), one),
               StaticCollectionError);
  EXPECT_FALSE(fo4::forms::add_static_collection_part(plugin, fo4::FormKey::parse("000EEE:Patch.esp"),
                                                      fo4::FormKey::parse("000E10:Patch.esp"), one));
  EXPECT_EQ(parts_of(plugin, "000E01:Patch.esp").size(), 2u);
}

TEST(WP9Scol, AddPartEncodesMasterOwnedStaticThroughMasterIndex) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E01:Patch.esp");
  std::vector<StaticPlacementView> one;
  one.push_back(placement(15.0f));
  EXPECT_TRUE(fo4::forms::add_static_collection_part(plugin, key, fo4::FormKey::parse("000E30:Fallout4.esm"), one));

  const auto after = reread(plugin);
  const auto parts = parts_of(after, "000E01:Patch.esp");
  ASSERT_EQ(parts.size(), 3u);
  EXPECT_EQ(parts[2].static_record.to_string(), "000E30:Fallout4.esm");
  const auto* record = fo4::find_record(after, key);
  ASSERT_NE(record, nullptr);
  ASSERT_GE(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[record->subrecords.size() - 2].data, u32_bytes(0x00000E30u));
}

TEST(WP9Scol, AddPartRefusesStaticOwnedByNonMasterPlugin) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E01:Patch.esp");
  const auto before = signatures_of(plugin, "000E01:Patch.esp");
  std::vector<StaticPlacementView> one;
  one.push_back(placement(15.0f));
  EXPECT_THROW(fo4::forms::add_static_collection_part(plugin, key, fo4::FormKey::parse("000E30:Unrelated.esp"), one),
               StaticCollectionError);
  EXPECT_EQ(signatures_of(plugin, "000E01:Patch.esp"), before);
  EXPECT_EQ(parts_of(plugin, "000E01:Patch.esp").size(), 2u);
}

TEST(WP9Scol, SetPlacementsInsertsReplacesAndRemovesDataInPlace) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E03:Patch.esp");
  std::vector<StaticPlacementView> one;
  one.push_back(placement(70.0f));
  EXPECT_TRUE(fo4::forms::set_static_collection_part_placements(plugin, key, 0, one));
  const std::vector<std::string> inserted = {"EDID", "OBND", "ONAM", "DATA", "ONAM", "DATA"};
  EXPECT_EQ(signatures_of(reread(plugin), "000E03:Patch.esp"), inserted);

  std::vector<StaticPlacementView> two;
  two.push_back(placement(80.0f));
  two.push_back(placement(90.0f));
  EXPECT_TRUE(fo4::forms::set_static_collection_part_placements(plugin, key, 1, two));
  const auto after = reread(plugin);
  const auto parts = parts_of(after, "000E03:Patch.esp");
  ASSERT_EQ(parts.size(), 2u);
  ASSERT_EQ(parts[0].placements.size(), 1u);
  EXPECT_FLOAT_EQ(parts[0].placements[0].x, 70.0f);
  ASSERT_EQ(parts[1].placements.size(), 2u);
  EXPECT_FLOAT_EQ(parts[1].placements[1].scale, 96.0f);

  const std::span<const StaticPlacementView> none;
  EXPECT_THROW(fo4::forms::set_static_collection_part_placements(plugin, key, 1, none), StaticCollectionError);
  EXPECT_EQ(signatures_of(reread(plugin), "000E03:Patch.esp"), inserted);
  EXPECT_EQ(parts_of(reread(plugin), "000E03:Patch.esp")[1].placements.size(), 2u);

  EXPECT_THROW(fo4::forms::set_static_collection_part_placements(plugin, key, 2, one), StaticCollectionError);
}

TEST(WP9Scol, RemovePartKeepsOthersAndRefusesTheLastPart) {
  auto plugin = fixture();
  const auto two_parts =fo4::FormKey::parse("000E01:Patch.esp");
  EXPECT_THROW(fo4::forms::remove_static_collection_part(plugin, two_parts, 2), StaticCollectionError);
  EXPECT_TRUE(fo4::forms::remove_static_collection_part(plugin, two_parts, 0));

  const auto after = reread(plugin);
  const auto parts = parts_of(after, "000E01:Patch.esp");
  ASSERT_EQ(parts.size(), 1u);
  EXPECT_EQ(parts[0].static_record.to_string(), "000E21:Objects.esp");
  ASSERT_EQ(parts[0].placements.size(), 1u);
  EXPECT_FLOAT_EQ(parts[0].placements[0].x, 20.0f);
  EXPECT_THROW(fo4::forms::remove_static_collection_part(plugin, two_parts, 0), StaticCollectionError);

  const auto one_part = fo4::FormKey::parse("000E02:Patch.esp");
  EXPECT_THROW(fo4::forms::remove_static_collection_part(plugin, one_part, 0), StaticCollectionError);
  EXPECT_THROW(fo4::forms::remove_static_collection_part(plugin, fo4::FormKey::parse("000E10:Patch.esp"), 0),
               StaticCollectionError);
  EXPECT_FALSE(fo4::forms::remove_static_collection_part(plugin, fo4::FormKey::parse("000EEE:Patch.esp"), 0));
  EXPECT_EQ(parts_of(plugin, "000E02:Patch.esp").size(), 1u);
}

TEST(WP9Scol, RemovePartWithoutDataDropsOnlyItsOnam) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E03:Patch.esp");
  const std::vector<std::string> before = {"EDID", "OBND", "ONAM", "ONAM", "DATA"};
  ASSERT_EQ(signatures_of(plugin, "000E03:Patch.esp"), before);
  EXPECT_TRUE(fo4::forms::remove_static_collection_part(plugin, key, 0));

  const auto after = reread(plugin);
  const std::vector<std::string> expected = {"EDID", "OBND", "ONAM", "DATA"};
  EXPECT_EQ(signatures_of(after, "000E03:Patch.esp"), expected);
  EXPECT_EQ(subrecord_data(after, "000E03:Patch.esp", "ONAM"), u32_bytes(0x02000E10u));
  const auto data = placement_bytes({40.0f});
  EXPECT_EQ(subrecord_data(after, "000E03:Patch.esp", "DATA"), data);
  EXPECT_EQ(parts_of(after, "000E03:Patch.esp").size(), 1u);
}

TEST(WP9Scol, GenericAuthoringRefusesPartSubrecordsAndCreate) {
  auto plugin = fixture();
  const auto key = fo4::FormKey::parse("000E01:Patch.esp");
  EXPECT_EQ(fo4::create_record(plugin, fo4::Signature::from_string("SCOL"), "NewCollection").status,
            fo4::RecordCreateStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("ONAM")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("DATA")),
            fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key, fo4::Signature::from_string("FULL")),
            fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, fo4::Signature::from_string("ONAM"), 0),
            fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, fo4::Signature::from_string("OBND"), 0),
            fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, fo4::Signature::from_string("FLTR"), 0),
            fo4::SubrecordRemoveStatus::removed);
}
