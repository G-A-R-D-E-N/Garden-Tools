#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto f32 = fo4::schema::ScalarStorage::f32;
constexpr auto i32 = fo4::schema::ScalarStorage::i32;
constexpr auto u8 = fo4::schema::ScalarStorage::u8;
constexpr auto u16 = fo4::schema::ScalarStorage::u16;
constexpr auto u32 = fo4::schema::ScalarStorage::u32;
constexpr auto zstring = fo4::schema::ScalarStorage::zstring;

struct ScalarRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  fo4::schema::ScalarStorage storage;
  bool embedded;
  std::size_t payload_size;
  std::size_t minimum_payload_size;
  std::size_t stride;
};

struct LinkRow {
  std::string_view path;
  std::string_view subrecord;
  std::size_t offset;
  std::uint32_t payload_size;
  std::string_view target;
  std::string_view denied_target;
  bool nullable;
};

constexpr std::uint16_t k_lod_payload = 16;
constexpr std::uint16_t k_owner_payload = 12;
constexpr std::uint16_t k_lighting_prefix = 40;

constexpr ScalarRow k_scalar_rows[] = {
    {"CELL\\Flags", "DATA", 0, u16, true, 2, 0, 0},
    {"CELL\\GridX", "XCLC", 0, i32, true, 0, 8, 0},
    {"CELL\\GridY", "XCLC", 4, i32, true, 0, 8, 0},
    {"CELL\\LandFlags", "XCLC", 8, u8, true, 12, 0, 0},
    {"CELL\\LightingAmbientRed", "XCLL", 0, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingAmbientGreen", "XCLL", 1, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingAmbientBlue", "XCLL", 2, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingDirectionalRed", "XCLL", 4, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingDirectionalGreen", "XCLL", 5, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingDirectionalBlue", "XCLL", 6, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogNearColorRed", "XCLL", 8, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogNearColorGreen", "XCLL", 9, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogNearColorBlue", "XCLL", 10, u8, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogNear", "XCLL", 12, f32, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogFar", "XCLL", 16, f32, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingDirectionalRotationXY", "XCLL", 20, i32, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingDirectionalRotationZ", "XCLL", 24, i32, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingDirectionalFade", "XCLL", 28, f32, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogClipDistance", "XCLL", 32, f32, true, 0, k_lighting_prefix, 0},
    {"CELL\\LightingFogPower", "XCLL", 36, f32, true, 0, k_lighting_prefix, 0},
    {"CELL\\PrecombinedObjectLevelXY", "CNAM", 0, u8, false, 0, 0, 0},
    {"CELL\\PrecombinedObjectLevelZ", "ZNAM", 0, u8, false, 0, 0, 0},
    {"CELL\\PreCombinedTimestamp", "PCMB", 0, u16, false, 0, 0, 0},
    {"CELL\\PreVisTimestamp", "VISI", 0, u16, false, 0, 0, 0},
    {"CELL\\WaterHeight", "XCLW", 0, f32, false, 0, 0, 0},
    {"CELL\\OwnerRank", "XRNK", 0, i32, false, 0, 0, 0},
    {"CELL\\ExteriorLodOffsetX", "XILW", 4, f32, true, k_lod_payload, 0, 0},
    {"CELL\\ExteriorLodOffsetY", "XILW", 8, f32, true, k_lod_payload, 0, 0},
    {"CELL\\ExteriorLodOffsetZ", "XILW", 12, f32, true, k_lod_payload, 0, 0},
    {"CELL\\WaterCurrentVelocityCount", "XWCN", 0, u32, false, 0, 0, 0},
    {"CELL\\WaterCurrentVelocityX", "XWCU", 0, f32, true, 0, 0, 16},
    {"CELL\\WaterCurrentVelocityY", "XWCU", 4, f32, true, 0, 0, 16},
    {"CELL\\WaterCurrentVelocityZ", "XWCU", 8, f32, true, 0, 0, 16},
    {"CELL\\WaterCurrentUnnamed", "XWCU", 12, f32, true, 0, 0, 16},
};

constexpr ScalarRow k_string_rows[] = {
    {"CELL\\WaterEnvironmentMap", "XWEM", 0, zstring, false, 0, 0, 0},
};

constexpr LinkRow k_link_rows[] = {
    {"CELL\\LightingTemplate", "LTMP", 0, 4, "LGTM", "WTHR", true},
    {"CELL\\Water", "XCWT", 0, 4, "WATR", "LGTM", false},
    {"CELL\\Location", "XLCN", 0, 4, "LCTN", "CELL", false},
    {"CELL\\EncounterZone", "XEZN", 0, 4, "ECZN", "LVLI", false},
    {"CELL\\ImageSpace", "XCIM", 0, 4, "IMGS", "WATR", false},
    {"CELL\\AcousticSpace", "XCAS", 0, 4, "ASPC", "IMGS", false},
    {"CELL\\MusicType", "XCMO", 0, 4, "MUSC", "ASPC", false},
    {"CELL\\SkyWeatherRegion", "XCCM", 0, 4, "REGN", "MUSC", false},
    {"CELL\\GodRays", "XGDR", 0, 4, "GDRY", "REGN", false},
    {"CELL\\LockList", "XILL", 0, 4, "FLST", "WATR", false},
    {"CELL\\Owner", "XOWN", 0, k_owner_payload, "FACT", "WATR", false},
    {"CELL\\ExteriorLodWorldspace", "XILW", 0, k_lod_payload, "WRLD", "CELL", false},
    {"CELL\\InPreVisFileOf", "RVIS", 0, 4, "CELL", "WRLD", false},
};

constexpr std::uint32_t raw_id(std::uint32_t local_id) { return 0x02000000u | local_id; }

Bytes u32_bytes(std::uint32_t value) {
  ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes link_pair(std::uint32_t first, std::uint32_t second) {
  ByteWriter out;
  out.u32(first);
  out.u32(second);
  return out.bytes;
}

Bytes i32_bytes(std::int32_t value) {
  ByteWriter out;
  out.i32(value);
  return out.bytes;
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes u16_bytes(std::uint16_t value) {
  ByteWriter out;
  out.u16(value);
  return out.bytes;
}

Bytes grid_bytes(bool with_land_flags) {
  ByteWriter out;
  out.i32(3);
  out.i32(-4);
  if (with_land_flags) {
    out.u8(0x05);
    out.u8(0);
    out.u8(0);
    out.u8(0);
  }
  return out.bytes;
}

// Every XCLL offset past the 40-byte prefix depends on the form version.
Bytes xcll_bytes(std::size_t size) {
  ByteWriter out;
  out.u32(0x00302010);
  out.u32(0x00050403);
  out.u32(0x00090807);
  out.f32(64.0f);
  out.f32(4096.0f);
  out.i32(45);
  out.i32(-30);
  out.f32(1.0f);
  out.f32(8192.0f);
  out.f32(2.0f);
  while (out.size() < size) out.u8(0);
  out.bytes.resize(size);
  return out.bytes;
}

Bytes owner_bytes(std::uint32_t owner, std::uint8_t no_crime) {
  ByteWriter out;
  out.u32(owner);
  out.u32(0);
  out.u8(no_crime);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

Bytes lod_bytes(std::uint32_t worldspace) {
  ByteWriter out;
  out.u32(worldspace);
  out.f32(1.5f);
  out.f32(-2.5f);
  out.f32(3.5f);
  return out.bytes;
}

Bytes currents_bytes() {
  ByteWriter out;
  out.f32(1.0f);
  out.f32(2.0f);
  out.f32(3.0f);
  out.f32(0.5f);
  out.f32(4.0f);
  out.f32(5.0f);
  out.f32(6.0f);
  out.f32(0.25f);
  return out.bytes;
}

Bytes text_bytes(std::string_view text) {
  Bytes bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
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

void write_grouped_record(ByteWriter& out, std::string_view signature, std::uint32_t local_id,
                          std::initializer_list<std::pair<std::string_view, Bytes>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, raw_id(local_id));
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  write_header(out);
  write_grouped_record(
      out, "CELL", 0x0C10,
      {{"DATA", u16_bytes(0x0002)},
       {"XCLC", grid_bytes(true)},
       {"XCLL", xcll_bytes(92)},
       {"CNAM", Bytes{std::byte{0x02}}},
       {"ZNAM", Bytes{std::byte{0x01}}},
       {"XCLW", floats({12.5f})},
       {"XRNK", i32_bytes(-1)},
       {"XILW", lod_bytes(0x00000A00)},
       {"XWEM", text_bytes("water\\envmap.dds")},
       {"XWCN", u32_bytes(2)},
       {"XWCU", currents_bytes()},
       {"XOWN", owner_bytes(0x00000A10, 1)},
       {"LTMP", u32_bytes(0x00000A20)},
       {"XCWT", u32_bytes(0x00000A21)},
       {"XLCN", u32_bytes(0x00000A22)},
       {"XEZN", u32_bytes(0x00000A23)},
       {"XCIM", u32_bytes(0x00000A24)},
       {"XCAS", u32_bytes(0x00000A25)},
       {"XCMO", u32_bytes(0x00000A26)},
       {"XCCM", u32_bytes(0x00000A27)},
       {"XGDR", u32_bytes(0x00000A28)},
       {"XILL", u32_bytes(0x00000A29)},
       {"RVIS", u32_bytes(0x00000A2A)},
       {"VISI", u16_bytes(0x1234)},
       {"PCMB", u16_bytes(0xABCD)},
       {"XCLR", link_pair(0x00000A30, 0x00000A31)},
       {"XPRI", link_pair(0x00000A50, 0x00000A51)}});
  return out.bytes;
}

std::vector<std::byte> make_short_payload_plugin(bool truncated_flags = false) {
  ByteWriter out;
  write_header(out);
  const Bytes flags = truncated_flags ? Bytes{std::byte{0x02}} : u16_bytes(0x0002);
  write_grouped_record(out, "CELL", 0x0C20,
                       {{"DATA", flags},
                        {"XCLC", grid_bytes(false)},
                        {"XCLL", xcll_bytes(36)}});
  return out.bytes;
}

Bytes sub_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view subrecord) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
  if (record == nullptr) {
    ADD_FAILURE() << key;
    return {};
  }
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() == subrecord) return sub.data;
  }
  return {};
}

std::optional<fo4::schema::ScalarFieldDescriptor> scalar_row(std::string_view path) {
  const auto found = fo4::schema::scalar_field_descriptor(
      fo4::Signature::from_string("CELL"), path);
  if (!found.has_value()) ADD_FAILURE() << path;
  return found;
}

Bytes scalar_bytes(const fo4::Plugin& plugin, std::string_view key, std::string_view path) {
  const auto field = scalar_row(path);
  if (!field.has_value()) return {};
  const auto data = sub_bytes(plugin, key, field->scalar.subrecord.to_string());
  const auto bytes = fo4::schema::scalar_field_bytes(data, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::uint32_t link_raw(const fo4::Plugin& plugin, std::string_view key, std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                            path);
  if (!field.has_value()) {
    ADD_FAILURE() << path;
    return 0;
  }
  const auto data = sub_bytes(plugin, key, field->subrecord.to_string());
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4 && field->offset + i < data.size(); ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[field->offset + i]))
             << (8 * i);
  }
  return value;
}

}  // namespace

TEST(CellRows, ScalarRowsMatchXEditOffsetsStorageAndGates) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("CELL"),
                                                            row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.embedded, row.embedded);
    EXPECT_EQ(field->scalar.offset, row.offset);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->scalar.payload_size, row.payload_size);
    EXPECT_EQ(field->scalar.minimum_payload_size, row.minimum_payload_size);
    EXPECT_EQ(field->scalar.address.stride, row.stride);
  }
}

TEST(CellRows, StringRowsMatchXEditSubrecords) {
  for (const auto& row : k_string_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("CELL"),
                                                            row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_FALSE(field->scalar.embedded);
  }
}

TEST(CellRows, FlagRowsCarryTheXEditBitNames) {
  const auto flags = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("CELL"),
                                                          "CELL\\Flags");
  ASSERT_TRUE(flags.has_value());
  EXPECT_EQ(flags->scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(fo4::schema::scalar_name_value(flags->scalar, "Is Interior Cell"),
            std::optional<std::int64_t>{1});
  EXPECT_EQ(fo4::schema::scalar_name_value(flags->scalar, "Can't Wait"), std::nullopt);
  EXPECT_EQ(fo4::schema::scalar_value_name(flags->scalar, 1 << 15),
            std::optional<std::string_view>{"Unknown 15"});

  const auto land = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("CELL"),
                                                         "CELL\\LandFlags");
  ASSERT_TRUE(land.has_value());
  EXPECT_EQ(fo4::schema::scalar_name_value(land->scalar, "No Collision - Quad 4"),
            std::optional<std::int64_t>{1 << 7});
  EXPECT_EQ(fo4::schema::scalar_name_value(land->scalar, "Can't Wait"), std::nullopt);
}

TEST(CellRows, LinkRowsNameTheirSubrecordsOffsetsPayloadsAndTargets) {
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                              row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, fo4::Signature::from_string(row.subrecord));
    EXPECT_EQ(field->offset, row.offset);
    EXPECT_EQ(field->payload_sizes[0], row.payload_size);
    EXPECT_EQ(field->nullable, row.nullable);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.target)));
    EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
        *field, fo4::Signature::from_string(row.denied_target)));
  }
  const auto lock_list = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("CELL"), "CELL\\LockList");
  ASSERT_TRUE(lock_list.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(
      *lock_list, fo4::Signature::from_string("NPC_")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(
      *lock_list, fo4::Signature::from_string("KEYM")));

  const auto owner = fo4::schema::formlink_field_descriptor(
      fo4::Signature::from_string("CELL"), "CELL\\Owner");
  ASSERT_TRUE(owner.has_value());
  EXPECT_FALSE(owner->nullable);
}

TEST(CellRows, LinkWritesRefuseNullExceptOnTheLightingTemplate) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000C10:Patch.esp";
  const auto record = fo4::FormKey::parse(key);
  const auto water = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                           "CELL\\Water");
  const auto light = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                            "CELL\\LightingTemplate");
  ASSERT_TRUE(water.has_value());
  ASSERT_TRUE(light.has_value());

  EXPECT_THROW(fo4::set_record_formlink_field(plugin, record, *water, fo4::FormKey{}),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(sub_bytes(plugin, key, "XCWT"), u32_bytes(0x00000A21));

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, *light, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, key, "XCWT"), u32_bytes(0x00000A21));
  EXPECT_EQ(sub_bytes(reread, key, "LTMP"), u32_bytes(0));

  auto cleared = plugin;
  EXPECT_EQ(fo4::remove_record_subrecord(cleared, record, fo4::Signature::from_string("XCWT"), 0),
            fo4::SubrecordRemoveStatus::removed);
  EXPECT_TRUE(sub_bytes(cleared, key, "XCWT").empty());
  EXPECT_TRUE(sub_bytes(fo4::read_plugin(fo4::write_plugin(cleared), "Patch.esp"), key, "XCWT").empty());

  EXPECT_EQ(fo4::create_record(cleared, fo4::Signature::from_string("CELL"), "NewCell").status,
            fo4::RecordCreateStatus::not_authorable);
}

TEST(CellRows, RegionsAndPreVisReferenceListsArePackedAndAppendable) {
  const auto list = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("CELL"),
                                                          "CELL\\Regions");
  ASSERT_TRUE(list.has_value());
  EXPECT_EQ(list->subrecord, fo4::Signature::from_string("XCLR"));
  EXPECT_EQ(list->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_TRUE(list->can_insert);
  EXPECT_FALSE(list->nullable);
  EXPECT_FALSE(list->wildcard_target);
  EXPECT_EQ(fo4::schema::formlink_list_targets(*list).size(), 1u);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(*list,
                                                        fo4::Signature::from_string("REGN")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(*list,
                                                         fo4::Signature::from_string("WTHR")));
  const auto by_field = fo4::schema::formlink_list_descriptor_by_field(
      fo4::Signature::from_string("CELL"), "Regions");
  ASSERT_TRUE(by_field.has_value());
  EXPECT_EQ(by_field->path, list->path);

  const auto previs = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("CELL"),
                                                            "CELL\\PreVisReferences");
  ASSERT_TRUE(previs.has_value());
  EXPECT_EQ(previs->subrecord, fo4::Signature::from_string("XPRI"));
  EXPECT_EQ(previs->storage, fo4::schema::FormLinkListStorage::packed_array);
  EXPECT_TRUE(previs->can_insert);
  EXPECT_FALSE(previs->nullable);
  EXPECT_EQ(fo4::schema::formlink_list_targets(*previs).size(), 9u);
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(*previs,
                                                        fo4::Signature::from_string("PCON")));
  EXPECT_TRUE(fo4::schema::formlink_list_target_allowed(*previs,
                                                        fo4::Signature::from_string("PGRE")));
  EXPECT_FALSE(fo4::schema::formlink_list_target_allowed(*previs,
                                                         fo4::Signature::from_string("REGN")));
}

TEST(CellRows, ReadsScalarsLinksAndRegionsFromThePlugin) {
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = "000C10:Patch.esp";

  EXPECT_EQ(sub_bytes(plugin, key, "DATA"), u16_bytes(0x0002));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\Flags"), u16_bytes(0x0002));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\GridX"), i32_bytes(3));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\GridY"), i32_bytes(-4));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LandFlags"), Bytes{std::byte{0x05}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingAmbientRed"), Bytes{std::byte{0x10}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingAmbientGreen"), Bytes{std::byte{0x20}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingAmbientBlue"), Bytes{std::byte{0x30}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingDirectionalBlue"), Bytes{std::byte{0x05}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingFogNearColorBlue"), Bytes{std::byte{0x09}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingFogNear"), floats({64.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingFogFar"), floats({4096.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingDirectionalRotationXY"), i32_bytes(45));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingDirectionalRotationZ"), i32_bytes(-30));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingDirectionalFade"), floats({1.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingFogClipDistance"), floats({8192.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\LightingFogPower"), floats({2.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\PrecombinedObjectLevelXY"), Bytes{std::byte{0x02}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\PrecombinedObjectLevelZ"), Bytes{std::byte{0x01}});
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\PreVisTimestamp"), u16_bytes(0x1234));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\PreCombinedTimestamp"), u16_bytes(0xABCD));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\WaterHeight"), floats({12.5f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\OwnerRank"), i32_bytes(-1));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\ExteriorLodOffsetX"), floats({1.5f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\ExteriorLodOffsetY"), floats({-2.5f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\ExteriorLodOffsetZ"), floats({3.5f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\WaterCurrentVelocityCount"), u32_bytes(2));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\WaterCurrentVelocityY"), floats({2.0f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\WaterCurrentUnnamed"), floats({0.5f}));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\WaterEnvironmentMap"),
            text_bytes("water\\envmap.dds"));

  EXPECT_EQ(link_raw(plugin, key, "CELL\\LightingTemplate"), 0x00000A20u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\Water"), 0x00000A21u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\Location"), 0x00000A22u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\EncounterZone"), 0x00000A23u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\ImageSpace"), 0x00000A24u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\AcousticSpace"), 0x00000A25u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\MusicType"), 0x00000A26u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\SkyWeatherRegion"), 0x00000A27u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\GodRays"), 0x00000A28u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\LockList"), 0x00000A29u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\Owner"), 0x00000A10u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\ExteriorLodWorldspace"), 0x00000A00u);
  EXPECT_EQ(link_raw(plugin, key, "CELL\\InPreVisFileOf"), 0x00000A2Au);

  const auto list = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("CELL"),
                                                          "CELL\\Regions");
  ASSERT_TRUE(list.has_value());
  const auto size = fo4::schema::record_formlink_list_size(plugin, fo4::FormKey::parse(key),
                                                           *list);
  ASSERT_TRUE(size.has_value());
  EXPECT_EQ(*size, 2u);
  const auto values = fo4::schema::record_formlink_list_values(plugin, fo4::FormKey::parse(key),
                                                               *list);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("000A30:Fallout4.esm"));
  EXPECT_EQ((*values)[1], fo4::FormKey::parse("000A31:Fallout4.esm"));

  const auto previs = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("CELL"),
                                                            "CELL\\PreVisReferences");
  ASSERT_TRUE(previs.has_value());
  const auto previs_size = fo4::schema::record_formlink_list_size(plugin, fo4::FormKey::parse(key),
                                                                  *previs);
  ASSERT_TRUE(previs_size.has_value());
  EXPECT_EQ(*previs_size, 2u);
  const auto previs_values = fo4::schema::record_formlink_list_values(
      plugin, fo4::FormKey::parse(key), *previs);
  ASSERT_TRUE(previs_values.has_value());
  ASSERT_EQ(previs_values->size(), 2u);
  EXPECT_EQ((*previs_values)[0], fo4::FormKey::parse("000A50:Fallout4.esm"));
  EXPECT_EQ((*previs_values)[1], fo4::FormKey::parse("000A51:Fallout4.esm"));
}

TEST(CellRows, ShortPayloadsHideRowsInsteadOfReportingThem) {
  const auto plugin = fo4::read_plugin(make_short_payload_plugin(), "Patch.esp");
  const auto key = "000C20:Patch.esp";

  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\Flags"), u16_bytes(0x0002));
  EXPECT_TRUE(scalar_bytes(plugin, key, "CELL\\LandFlags").empty());
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\GridX"), i32_bytes(3));
  EXPECT_EQ(scalar_bytes(plugin, key, "CELL\\GridY"), i32_bytes(-4));
  EXPECT_TRUE(scalar_bytes(plugin, key, "CELL\\LightingFogPower").empty());
  EXPECT_TRUE(scalar_bytes(plugin, key, "CELL\\LightingAmbientRed").empty());
  EXPECT_TRUE(scalar_bytes(plugin, key, "CELL\\WaterCurrentVelocityX").empty());
  EXPECT_THROW(scalar_bytes(plugin, key, "CELL\\PreVisTimestamp"), fo4::schema::ScalarError);

  auto mutable_plugin = fo4::read_plugin(make_short_payload_plugin(), "Patch.esp");
  const auto land = scalar_row("CELL\\LandFlags");
  ASSERT_TRUE(land.has_value());
  EXPECT_EQ(fo4::set_record_scalar(mutable_plugin, fo4::FormKey::parse(key), *land, "1"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto malformed = fo4::read_plugin(make_short_payload_plugin(true), "Patch.esp");
  const auto flags = scalar_row("CELL\\Flags");
  ASSERT_TRUE(flags.has_value());
  EXPECT_TRUE(scalar_bytes(malformed, key, "CELL\\Flags").empty());
  EXPECT_EQ(fo4::set_record_scalar(malformed, fo4::FormKey::parse(key), *flags, "2"),
            fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(CellRows, WritesScalarsAndLinksThenRereads) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto land = scalar_row("CELL\\LandFlags");
  const auto flags = scalar_row("CELL\\Flags");
  const auto light = scalar_row("CELL\\LightingFogNear");
  const auto height = scalar_row("CELL\\WaterHeight");
  const auto env = scalar_row("CELL\\WaterEnvironmentMap");
  const auto rank = scalar_row("CELL\\OwnerRank");
  const auto velocity = scalar_row("CELL\\WaterCurrentVelocityZ");
  const auto count = scalar_row("CELL\\WaterCurrentVelocityCount");
  ASSERT_TRUE(land.has_value());
  ASSERT_TRUE(flags.has_value());
  ASSERT_TRUE(light.has_value());
  ASSERT_TRUE(height.has_value());
  ASSERT_TRUE(env.has_value());
  ASSERT_TRUE(rank.has_value());
  ASSERT_TRUE(velocity.has_value());
  ASSERT_TRUE(count.has_value());

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *flags, "3"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *land, "129"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *light, "128"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *height, "64.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *rank, "7"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, *env, 0, "other.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *velocity, 1, "9.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_scalar(plugin, key, *count, "3"), fo4::schema::ScalarError);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *flags, "3"),
            fo4::ScalarMutationStatus::unchanged);

  const auto zone = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                           "CELL\\EncounterZone");
  const auto locked = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                             "CELL\\LockList");
  ASSERT_TRUE(zone.has_value());
  ASSERT_TRUE(locked.has_value());
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *zone,
                                           fo4::FormKey::parse("000A40:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *locked,
                                           fo4::FormKey::parse("000A41:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto owner = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("CELL"),
                                                            "CELL\\Owner");
  ASSERT_TRUE(owner.has_value());
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key, *owner,
                                           fo4::FormKey::parse("000A43:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto regions = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("CELL"),
                                                             "CELL\\Regions");
  ASSERT_TRUE(regions.has_value());
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key, *regions, 1,
                                                          fo4::FormKey::parse("000A42:Objects.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto previs = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("CELL"),
                                                            "CELL\\PreVisReferences");
  ASSERT_TRUE(previs.has_value());
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key, *previs, 2,
                                                          fo4::FormKey::parse("000A52:Objects.esp")),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key, *previs, 0,
                                                          fo4::FormKey::parse("000A52:Objects.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key, *previs, 2,
                                                             fo4::FormKey::parse("000A53:Objects.esp")),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto reread_key = "000C10:Patch.esp";
  EXPECT_EQ(sub_bytes(reread, reread_key, "DATA"), u16_bytes(0x0003));
  EXPECT_EQ(sub_bytes(reread, reread_key, "VISI"), u16_bytes(0x1234));
  EXPECT_EQ(sub_bytes(reread, reread_key, "PCMB"), u16_bytes(0xABCD));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XCLW"), floats({64.5f}));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XRNK"), i32_bytes(7));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XWEM"), text_bytes("other.dds"));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XEZN"), u32_bytes(0x01000A40));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XILL"), u32_bytes(0x01000A41));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XOWN"), owner_bytes(0x01000A43, 1));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XCLR"), link_pair(0x00000A30, 0x01000A42));
  ByteWriter expected_previs;
  expected_previs.u32(0x01000A52);
  expected_previs.u32(0x00000A51);
  expected_previs.u32(0x01000A53);
  EXPECT_EQ(sub_bytes(reread, reread_key, "XPRI"), expected_previs.bytes);
  auto expected_grid = grid_bytes(true);
  expected_grid[8] = std::byte{0x81};
  EXPECT_EQ(sub_bytes(reread, reread_key, "XCLC"), expected_grid);
  EXPECT_EQ(scalar_bytes(reread, reread_key, "CELL\\LightingFogNear"), floats({128.0f}));
  EXPECT_EQ(sub_bytes(reread, reread_key, "XWCU"),
            floats({1.0f, 2.0f, 3.0f, 0.5f, 4.0f, 5.0f, 9.5f, 0.25f}));
  EXPECT_EQ(scalar_bytes(reread, reread_key, "CELL\\WaterCurrentVelocityZ"), floats({3.0f}));
  EXPECT_EQ(scalar_bytes(reread, reread_key, "CELL\\WaterCurrentVelocityY"), floats({2.0f}));
  EXPECT_EQ(link_raw(reread, reread_key, "CELL\\LightingTemplate"), 0x00000A20u);

  const auto values = fo4::schema::record_formlink_list_values(reread, key, *regions);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ((*values)[0], fo4::FormKey::parse("000A30:Fallout4.esm"));
  EXPECT_EQ((*values)[1], fo4::FormKey::parse("000A42:Objects.esp"));

  const auto reread_previs = fo4::schema::record_formlink_list_values(reread, key, *previs);
  ASSERT_TRUE(reread_previs.has_value());
  ASSERT_EQ(reread_previs->size(), 3u);
  EXPECT_EQ((*reread_previs)[0], fo4::FormKey::parse("000A52:Objects.esp"));
  EXPECT_EQ((*reread_previs)[1], fo4::FormKey::parse("000A51:Fallout4.esm"));
  EXPECT_EQ((*reread_previs)[2], fo4::FormKey::parse("000A53:Objects.esp"));
}

TEST(CellRows, WritesLightingBytesWithoutTouchingTheirNeighbors) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000C10:Patch.esp");
  const auto green = scalar_row("CELL\\LightingFogNearColorGreen");
  const auto rotation = scalar_row("CELL\\LightingDirectionalRotationZ");
  ASSERT_TRUE(green.has_value());
  ASSERT_TRUE(rotation.has_value());
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *green, "200"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, *rotation, "-31"),
            fo4::ScalarMutationStatus::updated);

  auto expected = xcll_bytes(92);
  expected[9] = std::byte{200};
  expected[24] = std::byte{0xE1};
  expected[25] = std::byte{0xFF};
  expected[26] = std::byte{0xFF};
  expected[27] = std::byte{0xFF};

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(sub_bytes(reread, "000C10:Patch.esp", "XCLL"), expected);
  EXPECT_EQ(scalar_bytes(reread, "000C10:Patch.esp", "CELL\\LightingFogNearColorGreen"),
            Bytes{std::byte{200}});
  EXPECT_EQ(scalar_bytes(reread, "000C10:Patch.esp", "CELL\\LightingDirectionalRotationZ"),
            i32_bytes(-31));
  EXPECT_EQ(scalar_bytes(reread, "000C10:Patch.esp", "CELL\\LightingFogNearColorRed"),
            Bytes{std::byte{0x07}});
  EXPECT_EQ(scalar_bytes(reread, "000C10:Patch.esp", "CELL\\LightingFogClipDistance"),
            floats({8192.0f}));
}
