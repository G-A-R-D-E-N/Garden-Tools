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

Bytes coords(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.u32(static_cast<std::uint32_t>(x));
  out.u32(static_cast<std::uint32_t>(y));
  return out.bytes;
}

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
  write_record(out, "SMQN", 2050, {{"EDID", zstr("RadicleQuestNode")}, {"PNAM", u32(2049)}, {"SNAM", u32(0)}, {"CITC", u32(0)}, {"DNAM", u32(0)}, {"XNAM", u32(2)}, {"MNAM", u32(1)}, {"HNAM", f32(0.5f)}, {"QNAM", u32(2)}, {"NNAM", u32(2053)}, {"RNAM", f32(1.5f)}, {"NNAM", u32(2058)}, {"RNAM", f32(2.0f)}});
  write_record(out, "SMEN", 2051, {{"EDID", zstr("RadicleEvent")}, {"PNAM", u32(2050)}, {"SNAM", u32(0)}, {"CITC", u32(0)}, {"DNAM", u32(2)}, {"XNAM", u32(0)}, {"ENAM", u32(four_cc("ADCR"))}});
  write_record(out, "QUST", 2053, {{"EDID", zstr("RadicleQuest")}});
  write_record(out, "QUST", 2058, {{"EDID", zstr("RadicleQuestTwo")}});
  write_record(out, "SCCO", 2052, {{"EDID", zstr("RadicleCollection")}, {"QNAM", u32(2053)},
                                   {"SNAM", u32(2060)}, {"XNAM", coords(10, 20)},
                                   {"SNAM", u32(2061)}, {"XNAM", coords(-30, 40)},
                                   {"VNAM", zeros(4)}, {"XNAM", coords(50, 60)},
                                   {"VNAM", zeros(4)}});
  write_record(out, "SCEN", 2060, {{"EDID", zstr("RadicleSceneOne")}});
  write_record(out, "SCEN", 2061, {{"EDID", zstr("RadicleSceneTwo")}});
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

std::vector<Bytes> payloads(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  std::vector<Bytes> out;
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) out.push_back(subrecord.data);
  }
  return out;
}

}  // namespace

TEST(StoryManagerRecordsSynthetic, EveryNewRowReadsItsSubrecordOnFixtureRecords) {
  auto plugin = load();
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
  EXPECT_EQ(fo4::schema::decode_scalar(payload(plugin, 2050, "HNAM"), scalar("SMQN\\HoursUntilReset").scalar), "0.5");
  EXPECT_EQ(fo4::schema::decode_scalar(payload(plugin, 2049, "XNAM"), scalar("SMBN\\MaxConcurrentQuests").scalar), "3");
  EXPECT_EQ(fo4::schema::decode_scalar(payload(plugin, 2050, "MNAM"), scalar("SMQN\\NumQuestsToRun").scalar), "1");
  EXPECT_EQ(fo4::schema::scalar_fields(fo4::Signature::from_string("SCCO")).size(), 2u);
}

TEST(StoryManagerRecordsSynthetic, QuestListRowsReadAndRetargetEveryQuestNodeEntry) {
  // Regression: the node's quest references require a list row, so without it a retarget writes into the wrong NNAM payload and the QUST target type goes unchecked.
  auto plugin = load();
  const auto quests = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("SMQN"), "SMQN\\Quests");
  ASSERT_TRUE(quests.has_value());
  EXPECT_EQ(quests->subrecord, fo4::Signature::from_string("NNAM"));
  EXPECT_EQ(quests->storage, fo4::schema::FormLinkListStorage::per_subrecord);
  EXPECT_FALSE(quests->nullable);
  EXPECT_FALSE(quests->can_insert);
  const auto targets = fo4::schema::formlink_list_targets(*quests);
  ASSERT_EQ(targets.size(), 1u);
  EXPECT_EQ(targets[0], fo4::Signature::from_string("QUST"));
  const auto values = fo4::schema::record_formlink_list_values(plugin, key(2050), *quests);
  ASSERT_TRUE(values.has_value());
  ASSERT_EQ(values->size(), 2u);
  EXPECT_EQ(values->at(0), key(2053));
  EXPECT_EQ(values->at(1), key(2058));

  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(2050), *quests, 1, key(2053)),
            fo4::schema::FormLinkListMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(2050), *quests, 1, key(2049)),
            fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(2050), *quests, 2, key(2053)),
            fo4::schema::FormLinkListMutationStatus::index_out_of_range);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payloads(reread, 2050, "NNAM"), (std::vector<Bytes>{u32(2053), u32(2053)}));
  EXPECT_EQ(payload(reread, 2050, "QNAM"), u32(2));
}

TEST(StoryManagerRecordsSynthetic, QuestListRemovalAndClearKeepTheDeclaredCount) {
  // Regression: QNAM requires a rewrite and each quest's RNAM travels with its NNAM, or removal leaves a stale count and an orphan RNAM that the next quest inherits.
  const auto quests = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("SMQN"), "SMQN\\Quests");
  ASSERT_TRUE(quests.has_value());

  auto removed = load();
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(removed, key(2050), *quests, 0),
            fo4::schema::FormLinkListMutationStatus::updated);
  auto reread = fo4::read_plugin(fo4::write_plugin(removed), "Patch.esp");
  EXPECT_EQ(payloads(reread, 2050, "NNAM"), (std::vector<Bytes>{u32(2058)}));
  EXPECT_EQ(payloads(reread, 2050, "RNAM"), (std::vector<Bytes>{f32(2.0f)}));
  EXPECT_EQ(payload(reread, 2050, "QNAM"), u32(1));

  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(removed, key(2050), *quests, 0),
            fo4::schema::FormLinkListMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(removed), "Patch.esp");
  EXPECT_TRUE(payloads(reread, 2050, "NNAM").empty());
  EXPECT_TRUE(payloads(reread, 2050, "RNAM").empty());
  EXPECT_EQ(payload(reread, 2050, "QNAM"), u32(0));

  auto matched = load();
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_elements_matching(matched, key(2050), *quests, key(2053)), 1u);
  reread = fo4::read_plugin(fo4::write_plugin(matched), "Patch.esp");
  EXPECT_EQ(payloads(reread, 2050, "NNAM"), (std::vector<Bytes>{u32(2058)}));
  EXPECT_EQ(payloads(reread, 2050, "RNAM"), (std::vector<Bytes>{f32(2.0f)}));
  EXPECT_EQ(payload(reread, 2050, "QNAM"), u32(1));

  auto moved = load();
  EXPECT_EQ(fo4::schema::move_record_formlink_list_element(moved, key(2050), *quests, 0, 1),
            fo4::schema::FormLinkListMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(moved), "Patch.esp");
  EXPECT_EQ(payloads(reread, 2050, "NNAM"), (std::vector<Bytes>{u32(2058), u32(2053)}));
  EXPECT_EQ(payloads(reread, 2050, "RNAM"), (std::vector<Bytes>{f32(2.0f), f32(1.5f)}));
  EXPECT_EQ(payload(reread, 2050, "QNAM"), u32(2));

  auto cleared = load();
  EXPECT_EQ(fo4::schema::clear_record_formlink_list(cleared, key(2050), *quests),
            fo4::schema::FormLinkListMutationStatus::updated);
  reread = fo4::read_plugin(fo4::write_plugin(cleared), "Patch.esp");
  EXPECT_TRUE(payloads(reread, 2050, "NNAM").empty());
  EXPECT_TRUE(payloads(reread, 2050, "RNAM").empty());
  EXPECT_EQ(payload(reread, 2050, "QNAM"), u32(0));
}

TEST(StoryManagerRecordsSynthetic, SceneCoordinatesUseTheLayoutBeforeTheFirstVnam) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2052), scalar("SCCO\\CoordinateX"), 1, "-31"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2052), scalar("SCCO\\CoordinateY"), 0, "21"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(2052), link("SCCO\\Scene"), 1, key(2060)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(2052), scalar("SCCO\\CoordinateX"), 2, "7"),
            fo4::ScalarMutationStatus::subrecord_missing);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key(2052));
  ASSERT_NE(record, nullptr);
  std::vector<Bytes> xnam;
  std::vector<Bytes> snam;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string("XNAM")) xnam.push_back(subrecord.data);
    if (subrecord.signature == fo4::Signature::from_string("SNAM")) snam.push_back(subrecord.data);
  }
  ASSERT_EQ(xnam.size(), 3u);
  EXPECT_EQ(xnam[0], coords(10, 21));
  EXPECT_EQ(xnam[1], coords(-31, 40));
  EXPECT_EQ(xnam[2], coords(50, 60));
  ASSERT_EQ(snam.size(), 2u);
  EXPECT_EQ(snam[0], u32(2060));
  EXPECT_EQ(snam[1], u32(2060));
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
