#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

struct Sub {
  std::string_view signature;
  Bytes data;
};

struct ScalarRow {
  std::string_view record;
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  std::size_t payload_size;
};

struct WholeRow {
  std::string_view record;
  std::string_view path;
};

struct LinkRow {
  std::string_view record;
  std::string_view path;
  std::string_view subrecord;
  std::string_view target;
  bool nullable;
};

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr auto i16 = fo4::schema::ScalarStorage::i16;
constexpr auto i32 = fo4::schema::ScalarStorage::i32;
constexpr auto u8 = fo4::schema::ScalarStorage::u8;
constexpr auto u16 = fo4::schema::ScalarStorage::u16;
constexpr auto u32 = fo4::schema::ScalarStorage::u32;

constexpr ScalarRow k_scalar_rows[] = {
    {"ARMO", "ARMO\\BoundMinX", "OBND", 0, i16, 12},
    {"ARMO", "ARMO\\BoundMinY", "OBND", 2, i16, 12},
    {"ARMO", "ARMO\\BoundMinZ", "OBND", 4, i16, 12},
    {"ARMO", "ARMO\\BoundMaxX", "OBND", 6, i16, 12},
    {"ARMO", "ARMO\\BoundMaxY", "OBND", 8, i16, 12},
    {"ARMO", "ARMO\\BoundMaxZ", "OBND", 10, i16, 12},
    {"ARMO", "ARMO\\Value", "DATA", 0, i32, 12},
    {"ARMO", "ARMO\\Weight", "DATA", 4, f32, 12},
    {"ARMO", "ARMO\\Health", "DATA", 8, u32, 12},
    {"ARMO", "ARMO\\ArmorRating", "FNAM", 0, u16, 8},
    {"ARMO", "ARMO\\BaseAddonIndex", "FNAM", 2, u16, 8},
    {"ARMO", "ARMO\\StaggerRating", "FNAM", 4, u8, 8},
    {"ARMA", "ARMA\\MalePriority", "DNAM", 0, u8, 12},
    {"ARMA", "ARMA\\FemalePriority", "DNAM", 1, u8, 12},
    {"ARMA", "ARMA\\WeightSliderMale", "DNAM", 2, u8, 12},
    {"ARMA", "ARMA\\WeightSliderFemale", "DNAM", 3, u8, 12},
    {"ARMA", "ARMA\\DetectionSoundValue", "DNAM", 6, u8, 12},
    {"ARMA", "ARMA\\WeaponAdjust", "DNAM", 8, f32, 12},
};

constexpr WholeRow k_whole_rows[] = {
    {"ARMO", "ARMO\\FirstPersonFlags"},
    {"ARMA", "ARMA\\FirstPersonFlags"},
};

constexpr LinkRow k_link_rows[] = {
    {"ARMO", "ARMO\\TemplateArmor", "TNAM", "ARMO", false},
    {"ARMO", "ARMO\\Race", "RNAM", "RACE", false},
    {"ARMO", "ARMO\\BlockBashImpactDataSet", "BIDS", "IPDS", true},
    {"ARMO", "ARMO\\AlternateBlockMaterial", "BAMT", "MATT", true},
    {"ARMO", "ARMO\\Enchantment", "EITM", "ENCH", false},
    {"ARMO", "ARMO\\EquipmentType", "ETYP", "EQUP", true},
    {"ARMO", "ARMO\\PickUpSound", "YNAM", "SNDR", false},
    {"ARMO", "ARMO\\PutDownSound", "ZNAM", "SNDR", false},
    {"ARMO", "ARMO\\InstanceNaming", "INRD", "INNR", false},
    {"ARMO", "ARMO\\PreviewTransform", "PTRN", "TRNS", false},
    {"ARMA", "ARMA\\Race", "RNAM", "RACE", false},
    {"ARMA", "ARMA\\MaleSkinTexture", "NAM0", "TXST", true},
    {"ARMA", "ARMA\\FemaleSkinTexture", "NAM1", "TXST", true},
    {"ARMA", "ARMA\\MaleSkinTextureSwapList", "NAM2", "FLST", true},
    {"ARMA", "ARMA\\FemaleSkinTextureSwapList", "NAM3", "FLST", true},
    {"ARMA", "ARMA\\FootstepSound", "SNDD", "FSTS", true},
    {"ARMA", "ARMA\\ArtObject", "ONAM", "ARTO", false},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

Bytes bounds_data() {
  ByteWriter out;
  out.u16(static_cast<std::uint16_t>(-10));
  out.u16(static_cast<std::uint16_t>(-20));
  out.u16(static_cast<std::uint16_t>(-30));
  out.u16(10);
  out.u16(20);
  out.u16(30);
  return out.bytes;
}

Bytes armo_data() {
  ByteWriter out;
  out.u32(static_cast<std::uint32_t>(-7));
  out.f32(1.5f);
  out.u32(500);
  return out.bytes;
}

Bytes armo_fnam() {
  ByteWriter out;
  out.u16(60);
  out.u16(2);
  out.u8(3);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

Bytes arma_dnam() {
  ByteWriter out;
  out.u8(7);
  out.u8(8);
  out.u8(2);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u8(5);
  out.u8(0);
  out.f32(1.5f);
  return out.bytes;
}

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);
  const auto armo_group = fo4::test::begin_group(out, "ARMO", 0);
  const auto armo = fo4::test::begin_record(out, "ARMO", 0, raw_id(0x0D01));
  const std::vector<Sub> armo_subs{
      {"EDID", zstr("RadicleArmor")},
      {"OBND", bounds_data()},
      {"DATA", armo_data()},
      {"FNAM", armo_fnam()},
      {"BOD2", u32_bytes(4)},
      {"RNAM", u32_bytes(0x00000A10)},
      {"EITM", u32_bytes(0x00000A11)},
  };
  for (const auto& sub : armo_subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, armo);
  fo4::test::end_group(out, armo_group);

  const auto arma_group = fo4::test::begin_group(out, "ARMA", 0);
  const auto arma = fo4::test::begin_record(out, "ARMA", 0, raw_id(0x0D02));
  const std::vector<Sub> arma_subs{
      {"EDID", zstr("RadicleArmorAddon")},
      {"DNAM", arma_dnam()},
      {"BOD2", u32_bytes(1)},
      {"RNAM", u32_bytes(0x00000A10)},
      {"NAM0", u32_bytes(0x00000A12)},
      {"SNDD", u32_bytes(0x00000A13)},
  };
  for (const auto& sub : arma_subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, arma);
  fo4::test::end_group(out, arma_group);
  return out.bytes;
}

constexpr auto k_armo_key = "000D01:Patch.esp";
constexpr auto k_arma_key = "000D02:Patch.esp";

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
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

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view record,
                   std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return {};
  }
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view key,
                       std::string_view subrecord) {
  const auto record = fo4::FormKey::parse(key);
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    if (hit.record == record && hit.subrecord.to_string() == subrecord) {
      return hit.raw.value();
    }
  }
  ADD_FAILURE() << key << ' ' << subrecord;
  return 0;
}

}  // namespace

TEST(ARMOARMARows, ScalarRowsHaveXEditOffsetsStorageAndPayloadSize) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(row.record), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.payload_size, row.payload_size);
    EXPECT_TRUE(field->scalar.embedded);
  }
}

TEST(ARMOARMARows, WholePayloadAndLinkRowsNameTheirSubrecordsAndTargets) {
  for (const auto& row : k_whole_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(row.record), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("BOD2"));
    EXPECT_EQ(field->scalar.storage, u32);
    EXPECT_FALSE(field->scalar.embedded);
  }
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field =
        fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(row.record), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_EQ(field->nullable, row.nullable);
    EXPECT_EQ(field->payload_sizes[0], 4u);
    const auto targets = fo4::schema::formlink_field_targets(*field);
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fo4::Signature::from_string(row.target));
  }
}

TEST(ARMOARMARows, ResistanceRowsAreTheWeaponDamageArrayShape) {
  const auto amount = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                           "ARMO\\ResistanceAmount");
  ASSERT_TRUE(amount.has_value());
  EXPECT_EQ(amount->scalar.subrecord, fo4::Signature::from_string("DAMA"));
  EXPECT_EQ(amount->scalar.offset, 4u);
  EXPECT_EQ(amount->scalar.address.stride, 8u);
  EXPECT_EQ(amount->scalar.max_form_version, 152u);
  const auto type = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                           "ARMO\\ResistanceType");
  ASSERT_TRUE(type.has_value());
  EXPECT_EQ(type->max_form_version, 152u);
  const auto targets = fo4::schema::formlink_field_targets(*type);
  ASSERT_EQ(targets.size(), 1u);
  EXPECT_EQ(targets[0], fo4::Signature::from_string("DMGT"));
}

TEST(ARMOARMARows, StaggerRatingEnumRejectsValuesOutsideItsNames) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                          "ARMO\\StaggerRating");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  const auto extra_large = fo4::schema::scalar_name_value(field->scalar, "Extra Large");
  ASSERT_TRUE(extra_large.has_value());
  EXPECT_EQ(*extra_large, 4);
  EXPECT_FALSE(fo4::schema::scalar_value_name(field->scalar, 5).has_value());
  EXPECT_THROW(fo4::schema::encode_scalar("5", field->scalar), fo4::schema::ScalarError);
}

TEST(ARMOARMARows, ReadsArmorSubrecordsThroughTheRows) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\Value"), u32_bytes(0xFFFFFFF9u));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\Weight"), floats({1.5f}));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\Health"), u32_bytes(500));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\BoundMinX"),
            (Bytes{std::byte{0xF6}, std::byte{0xFF}}));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\BoundMaxZ"),
            (Bytes{std::byte{0x1E}, std::byte{0x00}}));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\ArmorRating"),
            (Bytes{std::byte{0x3C}, std::byte{0x00}}));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\StaggerRating"),
            (Bytes{std::byte{0x03}}));
  EXPECT_EQ(scalar_bytes(plugin, k_armo_key, "ARMO", "ARMO\\FirstPersonFlags"), u32_bytes(4));
  EXPECT_EQ(link_raw(plugin, k_armo_key, "RNAM"), 0x00000A10u);
  EXPECT_EQ(link_raw(plugin, k_armo_key, "EITM"), 0x00000A11u);

  EXPECT_EQ(scalar_bytes(plugin, k_arma_key, "ARMA", "ARMA\\MalePriority"), Bytes{std::byte{0x07}});
  EXPECT_EQ(scalar_bytes(plugin, k_arma_key, "ARMA", "ARMA\\WeightSliderMale"),
            Bytes{std::byte{0x02}});
  EXPECT_EQ(scalar_bytes(plugin, k_arma_key, "ARMA", "ARMA\\DetectionSoundValue"),
            Bytes{std::byte{0x05}});
  EXPECT_EQ(scalar_bytes(plugin, k_arma_key, "ARMA", "ARMA\\WeaponAdjust"), floats({1.5f}));
  EXPECT_EQ(scalar_bytes(plugin, k_arma_key, "ARMA", "ARMA\\FirstPersonFlags"), u32_bytes(1));
  EXPECT_EQ(link_raw(plugin, k_arma_key, "NAM0"), 0x00000A12u);
  EXPECT_EQ(link_raw(plugin, k_arma_key, "SNDD"), 0x00000A13u);
}

TEST(ARMOARMARows, WritesRowsThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto target = fo4::FormKey::parse("000A20:Objects.esp");
  const auto value = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                          "ARMO\\Value");
  const auto stagger = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                            "ARMO\\StaggerRating");
  const auto max_z = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                          "ARMO\\BoundMaxZ");
  const auto adjust = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ARMA"),
                                                           "ARMA\\WeaponAdjust");
  const auto female_slider = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("ARMA"), "ARMA\\WeightSliderFemale");
  const auto race = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("ARMO"),
                                                           "ARMO\\Race");
  ASSERT_TRUE(value.has_value());
  ASSERT_TRUE(stagger.has_value());
  ASSERT_TRUE(max_z.has_value());
  ASSERT_TRUE(adjust.has_value());
  ASSERT_TRUE(female_slider.has_value());
  ASSERT_TRUE(race.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_armo_key), *value, "-1024"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_armo_key), *stagger, "4"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_armo_key), *max_z, "100"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse(k_armo_key), *race, target),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_arma_key), *adjust, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(
      fo4::set_record_scalar(plugin, fo4::FormKey::parse(k_arma_key), *female_slider, "1"),
      fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, k_armo_key, "DATA"),
            join({u32_bytes(0xFFFFFC00u), floats({1.5f}), u32_bytes(500)}));
  EXPECT_EQ(sub_bytes(reread, k_armo_key, "FNAM"),
            (Bytes{std::byte{0x3C}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
                   std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}}));
  EXPECT_EQ(sub_bytes(reread, k_armo_key, "OBND"),
            (Bytes{std::byte{0xF6}, std::byte{0xFF}, std::byte{0xEC}, std::byte{0xFF},
                   std::byte{0xE2}, std::byte{0xFF}, std::byte{0x0A}, std::byte{0x00},
                   std::byte{0x14}, std::byte{0x00}, std::byte{0x64}, std::byte{0x00}}));
  EXPECT_EQ(link_raw(reread, k_armo_key, "RNAM"), 0x01000A20u);
  EXPECT_EQ(sub_bytes(reread, k_arma_key, "DNAM"),
            join({Bytes{std::byte{0x07}, std::byte{0x08}, std::byte{0x02}, std::byte{0x01},
                        std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, std::byte{0x00}},
                  floats({2.5f})}));
  EXPECT_EQ(sub_bytes(reread, k_arma_key, "BOD2"), u32_bytes(1));
}
