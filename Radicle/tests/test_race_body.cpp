#include "fo4/forms/race.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> empty_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

fo4::Subrecord raw_subrecord(std::string_view signature,
                             std::initializer_list<std::uint8_t> bytes) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string(signature);
  for (const auto byte : bytes) subrecord.data.push_back(std::byte{byte});
  return subrecord;
}

fo4::Subrecord string_subrecord(std::string_view signature,
                                std::string_view value) {
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::Signature::from_string(signature);
  for (const char ch : value) {
    subrecord.data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  subrecord.data.push_back(std::byte{0});
  return subrecord;
}

void expect_part_equal(const fo4::forms::RaceBodyPart& actual,
                       const fo4::forms::RaceBodyPart& expected) {
  ASSERT_EQ(actual.model_subrecords.size(), expected.model_subrecords.size());
  for (std::size_t index = 0; index < expected.model_subrecords.size(); ++index) {
    EXPECT_EQ(actual.model_subrecords[index].signature,
              expected.model_subrecords[index].signature);
    EXPECT_EQ(actual.model_subrecords[index].data,
              expected.model_subrecords[index].data);
    EXPECT_EQ(actual.model_subrecords[index].extended_size,
              expected.model_subrecords[index].extended_size);
  }
}

fo4::forms::RaceCreation basic_race() {
  fo4::forms::RaceCreation creation;
  creation.editor_id = "GardenBodyRace";
  creation.description = "BodyData array regression";
  creation.male_height = 1.0f;
  creation.female_height = 1.0f;
  return creation;
}

}

TEST(RaceBodyData, PreservesMultipleMaleAndFemalePartsWithoutCollapsing) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RaceBody.esp");
  auto creation = basic_race();

  fo4::forms::RaceBodyPart male0;
  male0.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\MaleTorso.nif"),
      raw_subrecord("MODT", {0x10, 0x20, 0x30, 0x40})};
  fo4::forms::RaceBodyPart male1;
  male1.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\MaleHands.nif"),
      raw_subrecord("MODC", {0x00, 0x00, 0x80, 0x3f}),
      raw_subrecord("MODF", {0x03})};
  fo4::forms::RaceBodyPart female0;
  // xEdit's generic model is sparse: MODL itself is not required.
  female0.model_subrecords = {raw_subrecord("MODT", {0xaa, 0xbb})};
  fo4::forms::RaceBodyPart female1;
  female1.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\FemaleTorso.nif"),
      raw_subrecord("MODS", {0x01, 0x02, 0x03})};

  creation.body_data.male = {male0, male1};
  creation.body_data.female = {female0, female1};

  const auto key = fo4::forms::create_race(plugin, creation);
  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "RaceBody.esp");
  const auto race = fo4::forms::decode_race(reread, key);

  ASSERT_EQ(race.body_data.male.size(), 2u);
  ASSERT_EQ(race.body_data.female.size(), 2u);
  expect_part_equal(race.body_data.male[0], male0);
  expect_part_equal(race.body_data.male[1], male1);
  expect_part_equal(race.body_data.female[0], female0);
  expect_part_equal(race.body_data.female[1], female1);
}

TEST(RaceBodyData, ReplacesArraysTransactionallyAndKeepsEveryPartSeparate) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RaceBody.esp");
  auto creation = basic_race();
  fo4::forms::RaceBodyPart old_part;
  old_part.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\Old.nif")};
  creation.body_data.male = {old_part};
  const auto key = fo4::forms::create_race(plugin, creation);

  fo4::forms::RaceBodyPart male0;
  male0.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\MaleA.nif")};
  fo4::forms::RaceBodyPart male1;
  male1.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\MaleB.nif")};
  fo4::forms::RaceBodyPart female0;
  female0.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\FemaleA.nif")};

  const std::vector<fo4::forms::RaceBodyPart> male{male0, male1};
  const std::vector<fo4::forms::RaceBodyPart> female{female0};
  ASSERT_TRUE(fo4::forms::set_race_body_data(plugin, key, male, female));

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "RaceBody.esp");
  const auto body = fo4::forms::decode_race_body_data(reread, key);
  ASSERT_EQ(body.male.size(), 2u);
  ASSERT_EQ(body.female.size(), 1u);
  expect_part_equal(body.male[0], male0);
  expect_part_equal(body.male[1], male1);
  expect_part_equal(body.female[0], female0);
}

TEST(RaceBodyData, EmptyMaleAndFemaleArraysStillEmitRequiredMarkers) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RaceBody.esp");
  auto creation = basic_race();
  const auto key = fo4::forms::create_race(plugin, creation);

  const auto reread =
      fo4::read_plugin(fo4::write_plugin(plugin), "RaceBody.esp");
  const auto body = fo4::forms::decode_race_body_data(reread, key);
  EXPECT_TRUE(body.male.empty());
  EXPECT_TRUE(body.female.empty());
}

TEST(RaceBodyData, DuplicateModelMemberFailsBeforeCallerMutation) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RaceBody.esp");
  const auto original = fo4::test::original_bytes(plugin);
  auto creation = basic_race();
  fo4::forms::RaceBodyPart bad;
  bad.model_subrecords = {
      string_subrecord("MODL", "Meshes\\Actors\\A.nif"),
      string_subrecord("MODL", "Meshes\\Actors\\B.nif")};
  creation.body_data.male = {bad};

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(RaceBodyData, UnsupportedModelMemberFailsClosed) {
  auto plugin = fo4::read_plugin(empty_plugin(), "RaceBody.esp");
  auto creation = basic_race();
  fo4::forms::RaceBodyPart bad;
  bad.model_subrecords = {raw_subrecord("BOD2", {0, 0, 0, 0})};
  creation.body_data.female = {bad};

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_race(plugin, creation)),
      fo4::forms::RaceError);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}
