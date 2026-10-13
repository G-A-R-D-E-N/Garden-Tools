#include "fo4/formid.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/record_constraints.hpp"
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

struct Row {
  std::string_view signature;
  std::uint32_t local;
};

constexpr Row k_rows[] = {{"SMBN", 2049}, {"SMQN", 2050}, {"SMEN", 2051},
                          {"SCCO", 2052}, {"TRNS", 2054}, {"CPTH", 2056}};

Bytes u8(std::uint8_t value) { return Bytes{std::byte{value}}; }

Bytes u32(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::uint32_t four_cc(std::string_view text) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[i])) << (8 * i);
  return value;
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

Bytes anam(std::uint32_t parent, std::uint32_t previous) {
  fo4::test::ByteWriter out;
  out.u32(parent);
  out.u32(previous);
  return out.bytes;
}

Bytes transform(bool with_zoom) {
  fo4::test::ByteWriter out;
  for (int i = 0; i < 6; ++i) out.f32(0.0f);
  out.f32(1.0f);
  if (with_zoom) {
    out.f32(-1.0f);
    out.f32(1.0f);
  }
  return out.bytes;
}

Bytes slice(const Bytes& data, std::size_t offset, std::size_t size) {
  return Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
               data.begin() + static_cast<std::ptrdiff_t>(offset + size));
}

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

// source:xEdit Core/wbDefinitionsFO4.pas:7930-7945 (CPTH), 8720-8775 (SMBN, SMQN, SMEN), 12593-12612 (SCCO), 12774-12786 (TRNS) @01a11729; ENAM names: Core/wbDefinitionsCommon.pas:7845-7897
std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(2304);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  write_record(out, "SMBN", 2049, {{"EDID", zstr("RadicleBranch")}, {"PNAM", u32(2050)}, {"SNAM", u32(0)}, {"CITC", u32(0)}, {"DNAM", u32(1)}, {"XNAM", u32(3)}});
  write_record(out, "SMQN", 2050, {{"EDID", zstr("RadicleQuestNode")}, {"PNAM", u32(2049)}, {"SNAM", u32(0)}, {"CITC", u32(0)}, {"DNAM", u32(0)}, {"XNAM", u32(2)}, {"MNAM", u32(1)}, {"HNAM", f32(0.5f)}, {"QNAM", u32(0)}});
  write_record(out, "SMEN", 2051, {{"EDID", zstr("RadicleEvent")}, {"PNAM", u32(2050)}, {"SNAM", u32(0)}, {"CITC", u32(0)}, {"DNAM", u32(2)}, {"XNAM", u32(0)}, {"ENAM", u32(four_cc("ADCR"))}});
  write_record(out, "QUST", 2053, {{"EDID", zstr("RadicleQuest")}});
  write_record(out, "QUST", 2058, {{"EDID", zstr("RadicleQuestTwo")}});
  write_record(out, "SCCO", 2052, {{"EDID", zstr("RadicleCollection")}, {"QNAM", u32(2053)}, {"VNAM", zeros(4)}, {"VNAM", zeros(4)}});
  write_record(out, "TRNS", 2054, {{"EDID", zstr("RadicleTransform")}, {"DATA", transform(true)}});
  write_record(out, "TRNS", 2055, {{"EDID", zstr("RadicleShortTransform")}, {"DATA", transform(false)}});
  write_record(out, "CPTH", 2056, {{"EDID", zstr("RadicleCameraPath")}, {"ANAM", anam(2057, 0)}, {"DATA", u8(0x40)}});
  write_record(out, "CPTH", 2057, {{"EDID", zstr("RadicleCameraPathTwo")}, {"ANAM", anam(0, 0)}, {"DATA", u8(5)}});
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

TEST(StoryManagerRecordsSynthetic, EveryNewRowReadsItsSubrecordOnFixtureRecords) {
  const auto plugin = load();
  for (const auto& row : k_rows) {
    const auto signature = fo4::Signature::from_string(row.signature);
    EXPECT_TRUE(fo4::schema::record_authoring_constraint(signature).has_value()) << row.signature;
    const auto* record = fo4::find_record(plugin, key(row.local));
    ASSERT_NE(record, nullptr) << row.signature;
    for (const auto& field : fo4::schema::scalar_fields(signature)) {
      for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature != field.scalar.subrecord) continue;
        const auto bytes = fo4::schema::scalar_field_bytes(subrecord.data, field.scalar);
        if (!bytes.empty()) static_cast<void>(fo4::schema::decode_scalar(bytes, field.scalar));
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(signature)) {
      EXPECT_TRUE(fo4::record_field_count(plugin, key(row.local), field).has_value()) << field.path;
    }
  }
  EXPECT_TRUE(fo4::schema::scalar_fields(fo4::Signature::from_string("SCCO")).empty());
}

TEST(StoryManagerRecordsSynthetic, NodeLinksFlagsAndEventTypeWriteTheirBytes) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2049), link("SMBN\\ParentNode"), key(2051)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2049), scalar("SMBN\\Flags"), 0, "3"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2049), scalar("SMBN\\MaxConcurrentQuests"), 0, "7"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2050), link("SMQN\\PreviousNode"), key(2049)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2050), scalar("SMQN\\Flags"), 0, "65537"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2050), scalar("SMQN\\NumQuestsToRun"), 0, "4"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2050), scalar("SMQN\\HoursUntilReset"), 0, "1.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2051), scalar("SMEN\\Type"), 0, "1279608897"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2052), link("SCCO\\Quest"), key(2058)),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 2049, "PNAM"), u32(2051));
  EXPECT_EQ(payload(reread, 2049, "DNAM"), u32(3));
  EXPECT_EQ(payload(reread, 2049, "XNAM"), u32(7));
  EXPECT_EQ(payload(reread, 2050, "SNAM"), u32(2049));
  EXPECT_EQ(payload(reread, 2050, "DNAM"), u32(65537));
  EXPECT_EQ(payload(reread, 2050, "MNAM"), u32(4));
  EXPECT_EQ(payload(reread, 2050, "HNAM"), f32(1.5f));
  EXPECT_EQ(payload(reread, 2051, "ENAM"), u32(four_cc("AHEL")));
  EXPECT_EQ(payload(reread, 2052, "QNAM"), u32(2058));
}

TEST(StoryManagerRecordsSynthetic, TransformRowsRespectTheOptionalZoomTail) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2054), scalar("TRNS\\PositionX"), 0, "2.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2054), scalar("TRNS\\ZoomMax"), 0, "4"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2055), scalar("TRNS\\ZoomMin"), 0, "-2"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2055), scalar("TRNS\\Scale"), 0, "3"), fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto full = payload(reread, 2054, "DATA");
  EXPECT_EQ(full.size(), 36u);
  EXPECT_EQ(slice(full, 0, 4), f32(2.5f));
  EXPECT_EQ(slice(full, 32, 4), f32(4.0f));
  const auto short_data = payload(reread, 2055, "DATA");
  EXPECT_EQ(short_data.size(), 28u);
  EXPECT_EQ(slice(short_data, 24, 4), f32(3.0f));
}

TEST(StoryManagerRecordsSynthetic, CameraPathLinksAndFlagsWriteTheirPayloads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2057), link("CPTH\\Previous"), key(2056)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2056), scalar("CPTH\\Flags"), 0, "1"), fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 2057, "ANAM"), anam(0, 2056));
  EXPECT_EQ(payload(reread, 2056, "DATA"), u8(1));
}
