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
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_bounds_records[] = {
    "ADDN", "ASPC", "BNDS", "CMPO", "DUAL", "ENCH", "EXPL", "FLOR", "GRAS", "HAZD", "LIGH",
    "LVLI", "LVLN", "LVSP", "NOTE", "PKIN", "PROJ", "SOUN", "SPEL", "STAT", "TREE", "TXST"};

constexpr std::string_view k_bound_names[] = {"BoundMinX", "BoundMinY", "BoundMinZ",
                                              "BoundMaxX", "BoundMaxY", "BoundMaxZ"};
constexpr std::size_t k_bound_offsets[] = {0, 2, 4, 6, 8, 10};

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

void add_record(ByteWriter& out, std::string_view signature, std::uint32_t local,
                std::string_view subrecord, const Bytes& data) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Radicle\0", 8});
  fo4::test::write_subrecord(out, subrecord, data);
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
  add_record(out, "ADDN", 0xE01, "OBND", i16_bytes({-1, -2, -3, 4, 5, 6}));
  add_record(out, "SOUN", 0xE02, "OBND", i16_bytes({-1, -2, -3, 4, 5, 6}));
  add_record(out, "ANIO", 0xE03, "MODL", zstr("Meshes\\Old.nif"));
  add_record(out, "FLOR", 0xE04, "MODS", u32_bytes(0x00000E10));
  return out;
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

}  // namespace

TEST(WP9BoundsModelRows, OBNDBoundReadsAndWritesOnTwoNewRecords) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const std::string_view paths[] = {"ADDN\\BoundMaxY", "SOUN\\BoundMinZ"};
  const std::string_view keys[] = {"000E01:Patch.esp", "000E02:Patch.esp"};
  const std::string_view initial[] = {"5", "-3"};
  for (std::size_t i = 0; i < 2; ++i) {
    SCOPED_TRACE(paths[i]);
    const auto field = fo4::schema::scalar_field_descriptor(paths[i]);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("OBND"));
    EXPECT_TRUE(field->scalar.embedded);
    EXPECT_EQ(field->scalar.payload_size, 12u);
    const auto data = subrecord_data(plugin, keys[i], "OBND");
    EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field->scalar),
                                         field->scalar),
              initial[i]);
    EXPECT_EQ(fo4::set_record_scalar(plugin, fo4::FormKey::parse(keys[i]), *field, "-7"),
              fo4::ScalarMutationStatus::updated);
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000E01:Patch.esp", "OBND"), i16_bytes({-1, -2, -3, 4, -7, 6}));
  EXPECT_EQ(subrecord_data(reread, "000E02:Patch.esp", "OBND"), i16_bytes({-1, -2, -7, 4, 5, 6}));
}

TEST(WP9BoundsModelRows, ModelPathReadsAndWritesOnOneRecord) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto field =
      fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("ANIO"), "ANIO\\Model");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("MODL"));
  const auto data = subrecord_data(plugin, "000E03:Patch.esp", "MODL");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, field->scalar),
                                       field->scalar),
            "Meshes\\Old.nif");
  EXPECT_EQ(fo4::set_record_string(plugin, fo4::FormKey::parse("000E03:Patch.esp"), "ANIO\\Model",
                                   0, "Meshes\\New.nif"),
            fo4::ScalarMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(subrecord_data(reread, "000E03:Patch.esp", "MODL"), zstr("Meshes\\New.nif"));
}

TEST(WP9BoundsModelRows, MaterialSwapRetargetsOnOneRecord) {
  const auto out = make_plugin();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto field = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("FLOR"),
                                                            "FLOR\\MaterialSwap");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->subrecord, fo4::Signature::from_string("MODS"));
  EXPECT_EQ(field->offset, 0u);
  EXPECT_FALSE(field->nullable);
  EXPECT_EQ(field->payload_sizes[0], 4u);
  EXPECT_EQ(link_raw(plugin, "000E04:Patch.esp", "MODS"), 0x00000E10u);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, fo4::FormKey::parse("000E04:Patch.esp"), *field,
                                           fo4::FormKey::parse("000E20:Objects.esp")),
            fo4::FormLinkFieldMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(link_raw(reread, "000E04:Patch.esp", "MODS"), 0x01000E20u);
}

TEST(WP9BoundsModelRows, EveryListedRecordHasSixBoundPaths) {
  for (const auto record : k_bounds_records) {
    for (std::size_t i = 0; i < 6; ++i) {
      const std::string path = std::string(record) + "\\" + std::string(k_bound_names[i]);
      SCOPED_TRACE(path);
      const auto field =
          fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
      ASSERT_TRUE(field.has_value());
      EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("OBND"));
      EXPECT_EQ(field->scalar.offset, k_bound_offsets[i]);
      EXPECT_EQ(field->scalar.payload_size, 12u);
    }
  }
}
