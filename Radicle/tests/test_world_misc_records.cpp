#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/signature.hpp"
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

Bytes u32(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes rotation(int min, int max) {
  fo4::test::ByteWriter out;
  out.u16(static_cast<std::uint16_t>(min));
  out.u16(static_cast<std::uint16_t>(max));
  return out.bytes;
}

Bytes zoom(float min, float max) {
  fo4::test::ByteWriter out;
  out.f32(min);
  out.f32(max);
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

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(2304);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  write_record(out, "STAT", 3001, {{"EDID", zstr("RadicleStatic")}});
  write_record(out, "STAT", 3002, {{"EDID", zstr("RadicleStaticTwo")}});
  write_record(out, "TRNS", 3003, {{"EDID", zstr("RadicleTransform")}});
  write_record(out, "QUST", 3004, {{"EDID", zstr("RadicleQuest")}});
  write_record(out, "PKIN", 3005, {{"EDID", zstr("RadiclePackIn")}});
  write_record(out, "REFR", 3006, {{"EDID", zstr("RadicleRef")}});
  write_record(out, "REFR", 3007, {{"EDID", zstr("RadicleRefTwo")}});
  write_record(out, "QUST", 3016, {{"EDID", zstr("RadicleQuestTwo")}});
  write_record(out, "LSCR", 3010, {{"EDID", zstr("RadicleLoad")}, {"DESC", u32(1)}, {"NNAM", u32(3001)}, {"TNAM", u32(3003)}, {"ONAM", rotation(-180, 180)}, {"ZNAM", zoom(-1.0f, 1.0f)}, {"MOD2", zstr("old")}});
  write_record(out, "OVIS", 3011, {{"INDX", u32(3001)}, {"DATA", zeros(24)}});
  write_record(out, "NOCM", 3012, {{"INDX", u32(0)}, {"DATA", zeros(8)}, {"INTV", u32(0)}, {"NAM1", zstr("Model.nif")}});
  write_record(out, "DEBR", 3013, {{"EDID", zstr("RadicleDebris")}, {"DATA", zeros(33)}, {"MODT", zeros(8)}});
  write_record(out, "RFGP", 3014, {{"EDID", zstr("RadicleGroup")}, {"NNAM", zstr("old")}, {"RNAM", u32(3006)}, {"PNAM", u32(3005)}});
  write_record(out, "MESG", 3015, {{"EDID", zstr("RadicleMessage")}, {"DESC", u32(2)}, {"FULL", u32(3)}, {"INAM", u32(0)}, {"QNAM", u32(3004)}, {"DNAM", u32(1)}, {"TNAM", u32(2)}, {"SNAM", zstr("old")}, {"NNAM", u32(4)}, {"ITXT", u32(5)}});
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

Bytes payload(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) return subrecord.data;
  }
  return {};
}

}  // namespace

TEST(WorldMiscRecordsSynthetic, LoadingScreenRowsWriteTheirPayloads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3010), scalar("LSCR\\RotationMin"), 0, "-90"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3010), scalar("LSCR\\ZoomMax"), 0, "1.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3010), "LSCR\\CameraPath", 0, "edited"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3010), link("LSCR\\LoadingScreenNif"), key(3002)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3010, "ONAM"), rotation(-90, 180));
  EXPECT_EQ(payload(reread, 3010, "ZNAM"), zoom(-1.0f, 1.5f));
  EXPECT_EQ(payload(reread, 3010, "MOD2"), zstr("edited"));
  EXPECT_EQ(payload(reread, 3010, "NNAM"), u32(3002));
  EXPECT_EQ(payload(reread, 3010, "TNAM"), u32(3003));
}

TEST(WorldMiscRecordsSynthetic, ReferenceGroupAndMessageRowsWriteTheirPayloads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_string(plugin, key(3014), "RFGP\\Name", 0, "edited"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3014), link("RFGP\\Reference"), key(3007)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3015), scalar("MESG\\Flags"), 0, "3"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3015), scalar("MESG\\DisplayTime"), 0, "5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3015), "MESG\\SWF", 0, "edited"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3015), link("MESG\\OwnerQuest"), key(3016)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3014, "NNAM"), zstr("edited"));
  EXPECT_EQ(payload(reread, 3014, "RNAM"), u32(3007));
  EXPECT_EQ(payload(reread, 3014, "PNAM"), u32(3005));
  EXPECT_EQ(payload(reread, 3015, "DNAM"), u32(3));
  EXPECT_EQ(payload(reread, 3015, "TNAM"), u32(5));
  EXPECT_EQ(payload(reread, 3015, "SNAM"), zstr("edited"));
  EXPECT_EQ(payload(reread, 3015, "QNAM"), u32(3016));
  EXPECT_EQ(payload(reread, 3015, "INAM"), u32(0));
}

TEST(WorldMiscRecordsSynthetic, ArrayPayloadRecordsExposeNoRows) {
  for (const auto* record : {"OVIS", "DEBR"}) {
    EXPECT_TRUE(fo4::schema::scalar_fields(fo4::Signature::from_string(record)).empty()) << record;
    EXPECT_TRUE(fo4::schema::formlink_fields(fo4::Signature::from_string(record)).empty()) << record;
  }
}

TEST(WorldMiscRecordsSynthetic, NavmeshObstacleIndexAndModelRowsWriteTheirPayloads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3012), scalar("NOCM\\Index"), 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3012), "NOCM\\Model", 0, "edited.nif"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3012), scalar("NOCM\\Index"), 1, "0"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3012, "INDX"), u32(7));
  EXPECT_EQ(payload(reread, 3012, "NAM1"), zstr("edited.nif"));
  EXPECT_EQ(payload(reread, 3012, "DATA"), zeros(8));
  EXPECT_EQ(payload(reread, 3012, "INTV"), u32(0));
}
