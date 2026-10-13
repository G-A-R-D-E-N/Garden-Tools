#include "fo4/formid.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

struct Sub {
  std::string_view signature;
  Bytes data;
};

Bytes u8(std::uint8_t value) { return Bytes{std::byte{value}}; }

Bytes u32(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes i32(std::int32_t value) {
  fo4::test::ByteWriter out;
  out.i32(value);
  return out.bytes;
}

Bytes f32(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes zeros(std::size_t size) { return Bytes(size, std::byte{0}); }

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

void write_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  const std::vector<Sub>& subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& subrecord : subrecords) {
    fo4::test::write_subrecord(out, subrecord.signature, std::span<const std::byte>{subrecord.data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

// source:xEdit Core/wbDefinitionsFO4.pas:6921-6990 @01a11729 (GLOB, GMST, KYWD, LCRT, AACT); 8778-8824 (DLBR, DLVW); 12294-12387 (CMPO, DFOB, DMGT, KSSM); 9357-9360 (OTFT); 9572-9586 (CLFM); 7950-7955 (VTYP)
std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(2304);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  write_record(out, "GLOB", 2049, {{"EDID", zstr("RadicleGlobal")}, {"FNAM", u8(102)}, {"FLTV", f32(1.5f)}});
  write_record(out, "GMST", 2050, {{"EDID", zstr("fRadicleSetting")}, {"DATA", f32(0.5f)}});
  write_record(out, "GMST", 2051, {{"EDID", zstr("iRadicleCount")}, {"DATA", i32(7)}});
  write_record(out, "GMST", 2052, {{"EDID", zstr("bRadicleFlag")}, {"DATA", u32(1)}});
  write_record(out, "KYWD", 2053, {{"EDID", zstr("RadicleKeyword")}, {"CNAM", zeros(4)}, {"DNAM", zstr("notes")}, {"TNAM", u32(2)}, {"DATA", u32(2054)}, {"NNAM", zstr("legacy")}});
  write_record(out, "AORU", 2054, {{"EDID", zstr("RadicleAttraction")}});
  write_record(out, "KSSM", 2055, {{"EDID", zstr("RadicleSound")}, {"DNAM", u32(2056)}, {"TNAM", f32(1.25f)}, {"KNAM", u32(2053)}});
  write_record(out, "SNDR", 2056, {{"EDID", zstr("RadicleSoundDescriptor")}});
  write_record(out, "CLFM", 2057, {{"EDID", zstr("RadicleColor")}, {"CNAM", u32(16711935)}, {"FNAM", u32(3)}});
  write_record(out, "LCRT", 2058, {{"EDID", zstr("RadicleLocation")}, {"CNAM", zeros(4)}, {"TNAM", u32(1)}});
  write_record(out, "VTYP", 2059, {{"EDID", zstr("RadicleVoice")}, {"DNAM", u8(3)}});
  write_record(out, "AACT", 2060, {{"EDID", zstr("RadicleAction")}, {"CNAM", zeros(4)}, {"DNAM", zstr("notes")}, {"TNAM", u32(0)}, {"DATA", u32(2054)}});
  write_record(out, "DFOB", 2061, {{"EDID", zstr("RadicleDefault")}, {"DATA", u32(2054)}});
  write_record(out, "OTFT", 2062, {{"EDID", zstr("RadicleOutfit")}, {"INAM", u32(2063)}});
  write_record(out, "CMPO", 2063, {{"EDID", zstr("RadicleComponent")}, {"OBND", zeros(12)}, {"CUSD", u32(2056)}, {"DATA", u32(10)}, {"MNAM", u32(2064)}, {"GNAM", u32(2049)}});
  write_record(out, "MISC", 2064, {{"EDID", zstr("RadicleScrap")}});
  write_record(out, "DLBR", 2065, {{"EDID", zstr("RadicleBranch")}, {"QNAM", u32(2066)}, {"TNAM", u32(0)}, {"DNAM", u32(1)}, {"SNAM", u32(2067)}});
  write_record(out, "QUST", 2066, {{"EDID", zstr("RadicleQuest")}});
  write_record(out, "DIAL", 2067, {{"EDID", zstr("RadicleTopic")}});
  write_record(out, "DLVW", 2068, {{"EDID", zstr("RadicleView")}, {"QNAM", u32(2066)}, {"BNAM", u32(2065)}});
  write_record(out, "DMGT", 2069, {{"EDID", zstr("RadicleDamage")}, {"DNAM", zeros(8)}});
  return out.bytes;
}

fo4::Plugin load() { return fo4::read_plugin(make_plugin(), "Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return *field;
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return *field;
}

fo4::schema::FormLinkListDescriptor list(std::string_view record, std::string_view path) {
  const auto descriptor = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(record), path);
  EXPECT_TRUE(descriptor.has_value()) << path;
  return *descriptor;
}

Bytes payload(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) return subrecord.data;
  }
  return {};
}

}  // namespace

TEST(ConfigRecordsSynthetic, GlobalAndGameSettingRowsWriteOnlyTheirOwnPayload) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2049), scalar("GLOB\\Type"), 0, "98"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2049), scalar("GLOB\\Value"), 0, "2.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2050), scalar("GMST\\FloatValue"), 0, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2050), scalar("GMST\\IntegerValue"), 0, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2050), scalar("GMST\\BooleanValue"), 0, "1"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2051), scalar("GMST\\IntegerValue"), 0, "-3"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2052), scalar("GMST\\BooleanValue"), 0, "0"), fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 2049, "FNAM"), u8(98));
  EXPECT_EQ(payload(reread, 2049, "FLTV"), f32(2.5f));
  EXPECT_EQ(payload(reread, 2050, "DATA"), f32(2.0f));
  EXPECT_EQ(payload(reread, 2051, "DATA"), i32(-3));
  EXPECT_EQ(payload(reread, 2052, "DATA"), u32(0));
}

TEST(ConfigRecordsSynthetic, KeywordAndActionRowsWriteStringsEnumsAndLinks) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_string(plugin, key(2053), "KYWD\\Notes", 0, "edited"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2053), scalar("KYWD\\Type"), 0, "6"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2053), link("KYWD\\AttractionRule"), key(2054)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(2060), "AACT\\Notes", 0, "edited"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2060), link("AACT\\AttractionRule"), key(2054)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 2053, "DNAM"), zstr("edited"));
  EXPECT_EQ(payload(reread, 2053, "TNAM"), u32(6));
  EXPECT_EQ(payload(reread, 2053, "DATA"), u32(2054));
  EXPECT_EQ(payload(reread, 2060, "DNAM"), zstr("edited"));
  EXPECT_EQ(payload(reread, 2060, "DATA"), u32(2054));
}

TEST(ConfigRecordsSynthetic, SoundCountAndSmallScalarRowsWriteTheirBytes) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2055), scalar("KSSM\\VATSThreshold"), 0, "2.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2055), link("KSSM\\PrimaryDescriptor"), key(2056)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(2055), list("KSSM", "KSSM\\Keywords"), 0, key(2053)),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2057), scalar("CLFM\\ColorIndex"), 0, "255"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2057), scalar("CLFM\\Flags"), 0, "5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2058), scalar("LCRT\\Type"), 0, "16"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2059), scalar("VTYP\\Flags"), 0, "1"), fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 2055, "TNAM"), f32(2.5f));
  EXPECT_EQ(payload(reread, 2055, "DNAM"), u32(2056));
  EXPECT_EQ(payload(reread, 2055, "KNAM"), u32(2053));
  EXPECT_EQ(payload(reread, 2057, "CNAM"), u32(255));
  EXPECT_EQ(payload(reread, 2057, "FNAM"), u32(5));
  EXPECT_EQ(payload(reread, 2058, "TNAM"), u32(16));
  EXPECT_EQ(payload(reread, 2059, "DNAM"), u8(1));
}

TEST(ConfigRecordsSynthetic, CompositeAndDialogueLinkRowsWriteTheirPayloads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2063), scalar("CMPO\\AutoCalcValue"), 0, "20"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2063), link("CMPO\\ScrapItem"), key(2064)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2065), scalar("DLBR\\Category"), 0, "1"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2065), scalar("DLBR\\Flags"), 0, "6"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2065), link("DLBR\\StartingTopic"), key(2067)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2068), link("DLVW\\Branch"), key(2065)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(2061), list("DFOB", "DFOB\\Object"), 0, key(2054)),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 2063, "DATA"), u32(20));
  EXPECT_EQ(payload(reread, 2063, "MNAM"), u32(2064));
  EXPECT_EQ(payload(reread, 2065, "TNAM"), u32(1));
  EXPECT_EQ(payload(reread, 2065, "DNAM"), u32(6));
  EXPECT_EQ(payload(reread, 2065, "SNAM"), u32(2067));
  EXPECT_EQ(payload(reread, 2068, "BNAM"), u32(2065));
  EXPECT_EQ(payload(reread, 2061, "DATA"), u32(2054));
}

TEST(ConfigRecordsSynthetic, RecordsWithoutFlatRowsExposeNone) {
  EXPECT_TRUE(fo4::schema::scalar_fields(fo4::Signature::from_string("DMGT")).empty());
  EXPECT_TRUE(fo4::schema::formlink_fields(fo4::Signature::from_string("DMGT")).empty());
  EXPECT_TRUE(fo4::schema::scalar_fields(fo4::Signature::from_string("OTFT")).empty());
  EXPECT_TRUE(fo4::schema::formlink_fields(fo4::Signature::from_string("OTFT")).empty());
}
