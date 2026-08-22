#include "fo4/forms/race.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
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

}

TEST(RaceCreation, CreatesRequiredDescAndCurrentTwoHundredByteData) {
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
  creation.beard_biped_object = 45;
  creation.body_biped_object = 33;
  creation.aim_angle_tolerance = 30.0f;
  creation.flight_radius = 128.0f;
  creation.angular_acceleration_rate = 4.0f;
  creation.angular_tolerance = 5.0f;
  creation.flags2 = 0x00000002u;
  creation.pipboy_biped_object = -1;
  creation.xp_value = -25;

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

  const auto model = fo4::forms::decode_race(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "GardenRace");
  ASSERT_TRUE(model.full_name.has_value());
  EXPECT_EQ(*model.full_name, "Garden Race");
  EXPECT_EQ(model.description, "Native RACE authoring");
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
  EXPECT_EQ(model.beard_biped_object, 45);
  EXPECT_EQ(model.body_biped_object, 33);
  EXPECT_FLOAT_EQ(model.aim_angle_tolerance, 30.0f);
  EXPECT_FLOAT_EQ(model.flight_radius, 128.0f);
  EXPECT_FLOAT_EQ(model.angular_acceleration_rate, 4.0f);
  EXPECT_FLOAT_EQ(model.angular_tolerance, 5.0f);
  EXPECT_EQ(model.flags2, creation.flags2);
  EXPECT_EQ(model.pipboy_biped_object, -1);
  EXPECT_EQ(model.xp_value, -25);
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

TEST(RaceCreation, InvalidNumericInputDoesNotConsumeFormId) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RacePatch.esp");
  const auto original = plugin.original_bytes;
  fo4::forms::RaceCreation creation;
  creation.description = "bad";
  creation.male_height = std::numeric_limits<float>::infinity();

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(RaceCreation, LocalizedPluginFailsBeforeAllocation) {
  auto plugin = fo4::read_plugin(
      empty_plugin(fo4::RecordFlags::localized), "Localized.esp");
  const auto original = plugin.original_bytes;
  fo4::forms::RaceCreation creation;
  creation.description = "requires string table";

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(RaceCreation, NonCurrentFormVersionFailsBeforeAllocation) {
  auto plugin = fo4::read_plugin(empty_plugin(0, 123), "Old.esp");
  const auto original = plugin.original_bytes;
  fo4::forms::RaceCreation creation;
  creation.description = "old format";

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}
