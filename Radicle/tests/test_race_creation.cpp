#include "fo4/formid.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr auto k_race = fo4::Signature::from_string("RACE");

std::vector<std::byte> empty_plugin(std::uint32_t tes4_flags = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record,
                                     std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == wanted) return &subrecord;
  }
  return nullptr;
}

std::vector<std::string> signature_order(const fo4::Record& record) {
  std::vector<std::string> out;
  for (const auto& subrecord : record.subrecords) {
    out.push_back(subrecord.signature.to_string());
  }
  return out;
}

// Constraint: the generated RACE rules up to, not including, NAM1; the marker run after it repeats MNAM and FNAM, which one rule list cannot name.
std::span<const fo4::schema::SubrecordAuthoringConstraint> rules_before_nam1() {
  const auto constraint = fo4::schema::record_authoring_constraint(k_race);
  EXPECT_TRUE(constraint.has_value());
  const auto all = fo4::schema::authoring_subrecords(*constraint);
  const auto nam1 = std::find_if(all.begin(), all.end(), [](const auto& rule) {
    return rule.signature == fo4::Signature::from_string("NAM1");
  });
  EXPECT_NE(nam1, all.end());
  return all.first(static_cast<std::size_t>(nam1 - all.begin()));
}

void append_markers(fo4::Plugin& plugin, const fo4::FormKey& key) {
  const std::pair<const char*, std::size_t> markers[] = {
      {"NAM1", 0}, {"MNAM", 0}, {"FNAM", 0}, {"NAM3", 0}, {"MNAM", 1}, {"FNAM", 1}};
  for (const auto& [signature, occurrence] : markers) {
    EXPECT_EQ(fo4::set_record_subrecord_bytes(plugin, key, fo4::Signature::from_string(signature), occurrence, {}),
              fo4::SubrecordBytesStatus::appended);
  }
}

fo4::RecordCreateResult create_race_with_markers(fo4::Plugin& plugin, std::string_view editor_id) {
  const auto created = fo4::create_record(plugin, k_race, editor_id, rules_before_nam1());
  if (created.status == fo4::RecordCreateStatus::created) append_markers(plugin, created.key);
  return created;
}

}  // namespace

TEST(RaceCreation, GenericCreateBuildsTheRequiredCurrentFormatSkeleton) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto created = fo4::create_record(plugin, k_race, "GardenRace", rules_before_nam1());
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto key = created.key;
  EXPECT_EQ(key, fo4::FormKey::parse("000800:RacePatch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);

  fo4::schema::LocalizedString name;
  name.text = "Garden Race";
  EXPECT_EQ(fo4::set_record_full_name(plugin, key, name), fo4::RecordNameStatus::updated);
  append_markers(plugin, key);
  const auto description = fo4::schema::scalar_field_descriptor("RACE\\Description");
  ASSERT_TRUE(description.has_value());
  EXPECT_EQ(fo4::set_record_string(plugin, key, *description, 0, "Native RACE authoring"),
            fo4::ScalarMutationStatus::updated);
  const auto set = [&](const char* path, const char* value) {
    const auto field = fo4::schema::scalar_field_descriptor(path);
    ASSERT_TRUE(field.has_value()) << path;
    EXPECT_EQ(fo4::set_record_scalar(plugin, key, *field, 0, value), fo4::ScalarMutationStatus::updated) << path;
  };
  set("RACE\\MaleHeight", "1.05");
  set("RACE\\FemaleHeight", "0.98");
  set("RACE\\Flags", "1");
  set("RACE\\Flags2", "2");
  set("RACE\\Size", "1");
  set("RACE\\AccelerationRate", "2");
  set("RACE\\DecelerationRate", "1.5");
  set("RACE\\InjuredHealthPercent", "0.25");

  for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, key)) {
    EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path << ": " << issue.message;
  }

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature.to_string(), "RACE");
  EXPECT_EQ(signature_order(*record),
            (std::vector<std::string>{"EDID", "FULL", "DESC", "DATA", "VTCK", "PNAM", "UNAM", "NAM1", "MNAM",
                                      "FNAM", "NAM3", "MNAM", "FNAM"}));
  EXPECT_EQ(find_subrecord(*record, "DATA")->data.size(), 200u);
  EXPECT_EQ(find_subrecord(*record, "VTCK")->data.size(), 8u);
  EXPECT_EQ(find_subrecord(*record, "PNAM")->data.size(), 4u);
  EXPECT_EQ(find_subrecord(*record, "UNAM")->data.size(), 4u);

  const auto read = [&](const char* path) {
    const auto field = fo4::schema::scalar_field_descriptor(path);
    EXPECT_TRUE(field.has_value()) << path;
    return fo4::record_scalar_text(reread, *record, *field, 0).value_or("");
  };
  EXPECT_EQ(read("RACE\\Name"), "Garden Race");
  EXPECT_EQ(read("RACE\\Description"), "Native RACE authoring");
  EXPECT_FLOAT_EQ(std::stof(read("RACE\\MaleHeight")), 1.05f);
  EXPECT_FLOAT_EQ(std::stof(read("RACE\\FemaleHeight")), 0.98f);
  EXPECT_EQ(read("RACE\\Flags"), "1");
  EXPECT_EQ(read("RACE\\Flags2"), "2");
  EXPECT_EQ(read("RACE\\Size"), "1");
  EXPECT_FLOAT_EQ(std::stof(read("RACE\\AccelerationRate")), 2.0f);
  EXPECT_FLOAT_EQ(std::stof(read("RACE\\DecelerationRate")), 1.5f);
  EXPECT_FLOAT_EQ(std::stof(read("RACE\\InjuredHealthPercent")), 0.25f);
}

TEST(RaceCreation, PlainCreateRefusesBecauseTheMarkerRunHasNoDefault) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto created = fo4::create_record(plugin, k_race, "PlainRace");
  EXPECT_EQ(created.status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(RaceCreation, VoiceFormLinksSurviveWriteReloadAndCollection) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto male_voice = fo4::create_local_record(plugin, fo4::Signature::from_string("VTYP"));
  const auto female_voice = fo4::create_local_record(plugin, fo4::Signature::from_string("VTYP"));
  const auto race_key = create_race_with_markers(plugin, "VoiceRace").key;

  const auto male = fo4::schema::formlink_field_descriptor("RACE\\VoiceTypeMale");
  const auto female = fo4::schema::formlink_field_descriptor("RACE\\VoiceTypeFemale");
  ASSERT_TRUE(male.has_value());
  ASSERT_TRUE(female.has_value());
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, race_key, *male, male_voice),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, race_key, *female, female_voice),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto links = fo4::collect_form_links(reread);
  std::size_t voice_links = 0;
  for (const auto& link : links) {
    if (link.record != race_key || link.subrecord.to_string() != "VTCK") continue;
    if (link.key == male_voice || link.key == female_voice) ++voice_links;
  }
  EXPECT_EQ(voice_links, 2u);
}

TEST(RaceCreation, EmptyDescriptionStillHasTheRequiredDescSubrecord) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto key = create_race_with_markers(plugin, "").key;
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* desc = find_subrecord(*record, "DESC");
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->data.size(), 1u);
  EXPECT_EQ(desc->data[0], std::byte{0});
}

TEST(RaceCreation, InlineTextStoresWindows1252AndUnencodableTextIsRefused) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto key = fo4::create_record(plugin, k_race, "", rules_before_nam1()).key;
  fo4::schema::LocalizedString name;
  name.text = fo4::stored_inline_text("Caf\xC3\xA9", "RACE FULL");
  ASSERT_EQ(fo4::set_record_full_name(plugin, key, name), fo4::RecordNameStatus::updated);
  append_markers(plugin, key);
  const auto description = fo4::schema::scalar_field_descriptor("RACE\\Description");
  ASSERT_TRUE(description.has_value());
  ASSERT_EQ(fo4::set_record_string(plugin, key, *description, 0, fo4::stored_inline_text("Caf\xC3\xA9", "RACE DESC")),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const std::vector<std::byte> expected{
      std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(find_subrecord(*record, "FULL")->data, expected);
  EXPECT_EQ(find_subrecord(*record, "DESC")->data, expected);

  EXPECT_THROW(static_cast<void>(fo4::stored_inline_text("Smile \xF0\x9F\x98\x80", "RACE FULL")), fo4::FormIdError);
}

TEST(RaceCreation, LocalizedPluginRefusesBeforeAllocation) {
  auto plugin = fo4::read_plugin(empty_plugin(fo4::RecordFlags::localized), "Localized.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto created = fo4::create_record(plugin, k_race, "", rules_before_nam1());
  EXPECT_EQ(created.status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}
