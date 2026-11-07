#include "fo4/forms/race.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> empty_plugin(std::uint32_t tes4_flags = 0,
                                    std::uint16_t form_version = 131) {
  fo4::test::ByteWriter out;
  const auto tes4 =
      fo4::test::begin_record(out, "TES4", tes4_flags, 0, form_version);
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

std::size_t count_subrecords(const fo4::Record& record,
                             std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  std::size_t count = 0;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == wanted) ++count;
  }
  return count;
}

}

TEST(RaceCreation, CreatesRequiredCurrentFormatSkeleton) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  fo4::forms::RaceCreation creation;
  creation.editor_id = "GardenRace";
  creation.full_name = "Garden Race";
  creation.description = "Native RACE authoring";
  creation.male_height = 1.05f;
  creation.female_height = 0.98f;
  creation.male_default_weight = {0.2f, 0.5f, 0.3f};
  creation.female_default_weight = {0.3f, 0.4f, 0.3f};
  creation.flags = 0x00000001u;
  creation.acceleration_rate = 2.0f;
  creation.deceleration_rate = 1.5f;
  creation.size = 1;
  creation.injured_health_pct = 0.25f;
  creation.shield_biped_object = -1;
  creation.beard_biped_object = 18;
  creation.body_biped_object = 3;
  creation.aim_angle_tolerance = 30.0f;
  creation.flight_radius = 128.0f;
  creation.angular_acceleration_rate = 4.0f;
  creation.angular_tolerance = 5.0f;
  creation.flags2 = 0x00000002u;
  creation.pipboy_biped_object = -1;
  creation.xp_value = -25;
  creation.facegen_main_clamp = 0.75f;
  creation.facegen_face_clamp = 0.5f;

  const auto key = fo4::forms::create_race(plugin, creation);
  EXPECT_EQ(key, fo4::FormKey::parse("000800:RacePatch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(plugin, key).empty());

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "RacePatch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature.to_string(), "RACE");
  ASSERT_NE(find_subrecord(*record, "DESC"), nullptr);
  const auto* data = find_subrecord(*record, "DATA");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->data.size(), 200u);
  const auto* voices = find_subrecord(*record, "VTCK");
  ASSERT_NE(voices, nullptr);
  EXPECT_EQ(voices->data.size(), 8u);
  const auto* main_clamp = find_subrecord(*record, "PNAM");
  const auto* face_clamp = find_subrecord(*record, "UNAM");
  ASSERT_NE(main_clamp, nullptr);
  ASSERT_NE(face_clamp, nullptr);
  EXPECT_EQ(main_clamp->data.size(), 4u);
  EXPECT_EQ(face_clamp->data.size(), 4u);
  ASSERT_NE(find_subrecord(*record, "NAM1"), nullptr);
  ASSERT_NE(find_subrecord(*record, "NAM3"), nullptr);
  // BodyData plus the deterministic empty behavior-graph skeleton each carry
  // one male/female gender marker.
  EXPECT_EQ(count_subrecords(*record, "MNAM"), 2u);
  EXPECT_EQ(count_subrecords(*record, "FNAM"), 2u);

  const auto model = fo4::forms::decode_race(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "GardenRace");
  ASSERT_TRUE(model.full_name.has_value());
  // Authored into a non-localized plugin, so both stay inline text.
  EXPECT_FALSE(model.full_name->localized());
  EXPECT_EQ(model.full_name->inline_text(), "Garden Race");
  EXPECT_FALSE(model.description.localized());
  EXPECT_EQ(model.description.inline_text(), "Native RACE authoring");
  EXPECT_FLOAT_EQ(model.male_height, 1.05f);
  EXPECT_FLOAT_EQ(model.female_height, 0.98f);
  EXPECT_EQ(model.male_default_weight, creation.male_default_weight);
  EXPECT_EQ(model.female_default_weight, creation.female_default_weight);
  EXPECT_EQ(model.flags, creation.flags);
  EXPECT_FLOAT_EQ(model.acceleration_rate, 2.0f);
  EXPECT_FLOAT_EQ(model.deceleration_rate, 1.5f);
  EXPECT_EQ(model.size, 1u);
  EXPECT_FLOAT_EQ(model.injured_health_pct, 0.25f);
  EXPECT_EQ(model.shield_biped_object, -1);
  EXPECT_EQ(model.beard_biped_object, 18);
  EXPECT_EQ(model.body_biped_object, 3);
  EXPECT_FLOAT_EQ(model.aim_angle_tolerance, 30.0f);
  EXPECT_FLOAT_EQ(model.flight_radius, 128.0f);
  EXPECT_FLOAT_EQ(model.angular_acceleration_rate, 4.0f);
  EXPECT_FLOAT_EQ(model.angular_tolerance, 5.0f);
  EXPECT_EQ(model.flags2, creation.flags2);
  EXPECT_EQ(model.pipboy_biped_object, -1);
  EXPECT_EQ(model.xp_value, -25);
  EXPECT_FALSE(model.male_voice.has_value());
  EXPECT_FALSE(model.female_voice.has_value());
  EXPECT_FLOAT_EQ(model.facegen_main_clamp, 0.75f);
  EXPECT_FLOAT_EQ(model.facegen_face_clamp, 0.5f);
  EXPECT_TRUE(model.body_data.male.empty());
  EXPECT_TRUE(model.body_data.female.empty());
}

TEST(RaceCreation, VoiceFormLinksSurviveWriteReloadAndCollection) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto male_voice =
      fo4::create_local_record(plugin, fo4::Signature::from_string("VTYP"));
  const auto female_voice =
      fo4::create_local_record(plugin, fo4::Signature::from_string("VTYP"));

  fo4::forms::RaceCreation creation;
  creation.description = "voice links";
  creation.male_height = 1.0f;
  creation.female_height = 1.0f;
  creation.male_voice = male_voice;
  creation.female_voice = female_voice;
  const auto race_key = fo4::forms::create_race(plugin, creation);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto race = fo4::forms::decode_race(reread, race_key);
  ASSERT_TRUE(race.male_voice.has_value());
  ASSERT_TRUE(race.female_voice.has_value());
  EXPECT_EQ(*race.male_voice, male_voice);
  EXPECT_EQ(*race.female_voice, female_voice);

  const auto links = fo4::collect_form_links(reread);
  std::size_t voice_links = 0;
  for (const auto& link : links) {
    if (link.record != race_key || link.subrecord.to_string() != "VTCK") continue;
    if (link.key == male_voice || link.key == female_voice) ++voice_links;
  }
  EXPECT_EQ(voice_links, 2u);
}

TEST(RaceCreation, EmptyDescriptionStillCreatesRequiredDescSubrecord) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  fo4::forms::RaceCreation creation;
  creation.description = "";

  const auto key = fo4::forms::create_race(plugin, creation);
  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* desc = find_subrecord(*record, "DESC");
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->data.size(), 1u);
  EXPECT_EQ(desc->data[0], std::byte{0});
}

TEST(RaceCreation, InlineFullNameStoresWindows1252AndRefusesUnencodableText) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  fo4::forms::RaceCreation creation;
  creation.full_name = std::string_view{"Caf\xC3\xA9"};
  creation.description = "";

  const auto key = fo4::forms::create_race(plugin, creation);
  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "RacePatch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* full = find_subrecord(*record, "FULL");
  ASSERT_NE(full, nullptr);
  const std::vector<std::byte> expected{
      std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(full->data, expected);

  auto refused = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto original = fo4::test::original_bytes(refused);
  creation.full_name = std::string_view{"Smile \xF0\x9F\x98\x80"};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(refused, creation)),
      fo4::FormIdError);
  EXPECT_EQ(fo4::test::original_bytes(refused), original);
}

TEST(RaceCreation, InvalidNumericInputDoesNotConsumeFormId) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  fo4::forms::RaceCreation creation;
  creation.description = "bad";
  creation.male_height = std::numeric_limits<float>::infinity();

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(RaceCreation, LocalizedPluginFailsBeforeAllocation) {
  auto plugin = fo4::read_plugin(
      empty_plugin(fo4::RecordFlags::localized), "Localized.esp");
  const auto original = fo4::test::original_bytes(plugin);
  fo4::forms::RaceCreation creation;
  creation.description = "requires string table";

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(RaceCreation, NonCurrentFormVersionFailsBeforeAllocation) {
  auto plugin = fo4::read_plugin(empty_plugin(0, 123), "Old.esp");
  const auto original = fo4::test::original_bytes(plugin);
  fo4::forms::RaceCreation creation;
  creation.description = "old format";

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}
