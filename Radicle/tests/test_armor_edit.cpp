#include "fo4/forms/armor.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> empty_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> legacy_armor(bool with_male_model) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "ARMO", 0);
  const auto record = fo4::test::begin_record(out, "ARMO", 0, 0x00000800);
  if (with_male_model) {
    fo4::test::write_subrecord(
        out, "MOD2", std::string_view{"Meshes\\Armor\\OldMale.nif\0", 25});
  }
  const std::array<std::byte, 8> bod2{};
  fo4::test::write_subrecord(out, "BOD2", bod2);
  fo4::test::ByteWriter data;
  data.u32(5);
  data.f32(1.0f);
  data.u32(100);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
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

}  // namespace

TEST(ArmorEdit, InsertsMissingArmoGenderedFieldsOnSchemaModeledRecord) {
  auto plugin = fo4::read_plugin(empty_plugin(), "ArmorPatch.esp");
  fo4::forms::ArmorCreation creation;
  const auto key = fo4::forms::create_armor(plugin, creation);

  ASSERT_TRUE(fo4::forms::set_armor_model_fields(
      plugin,
      key,
      "Meshes\\Armor\\Male.nif",
      "Textures\\Male.dds",
      "Textures\\MaleMessage.dds",
      "Meshes\\Armor\\Female.nif",
      "Textures\\Female.dds",
      "Textures\\FemaleMessage.dds"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ArmorPatch.esp");
  const auto model = fo4::forms::decode_armor(reread, key);
  ASSERT_TRUE(model.male_world_model.has_value());
  EXPECT_EQ(*model.male_world_model, "Meshes\\Armor\\Male.nif");
  ASSERT_TRUE(model.male_icon.has_value());
  EXPECT_EQ(*model.male_icon, "Textures\\Male.dds");
  ASSERT_TRUE(model.female_world_model.has_value());
  EXPECT_EQ(*model.female_world_model, "Meshes\\Armor\\Female.nif");
  ASSERT_TRUE(model.female_message_icon.has_value());
  EXPECT_EQ(*model.female_message_icon, "Textures\\FemaleMessage.dds");
}

TEST(ArmorEdit, InsertsMissingArmaBipedAndFirstPersonModels) {
  auto plugin = fo4::read_plugin(empty_plugin(), "ArmorPatch.esp");
  fo4::forms::ArmorAddonCreation creation;
  const auto key = fo4::forms::create_armor_addon(plugin, creation);

  ASSERT_TRUE(fo4::forms::set_armor_addon_model_fields(
      plugin,
      key,
      "Meshes\\Armor\\MaleBiped.nif",
      "Meshes\\Armor\\FemaleBiped.nif",
      "Meshes\\Armor\\MaleFirst.nif",
      "Meshes\\Armor\\FemaleFirst.nif"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ArmorPatch.esp");
  const auto model = fo4::forms::decode_armor_addon(reread, key);
  ASSERT_TRUE(model.male_biped_model.has_value());
  EXPECT_EQ(*model.male_biped_model, "Meshes\\Armor\\MaleBiped.nif");
  ASSERT_TRUE(model.female_biped_model.has_value());
  EXPECT_EQ(*model.female_biped_model, "Meshes\\Armor\\FemaleBiped.nif");
  ASSERT_TRUE(model.male_first_person_model.has_value());
  EXPECT_EQ(*model.male_first_person_model, "Meshes\\Armor\\MaleFirst.nif");
  ASSERT_TRUE(model.female_first_person_model.has_value());
  EXPECT_EQ(*model.female_first_person_model, "Meshes\\Armor\\FemaleFirst.nif");
}

TEST(ArmorEdit, RefusesMissingFieldInsertionAroundUnmodeledLegacySubrecords) {
  auto plugin = fo4::read_plugin(legacy_armor(false), "Legacy.esp");
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::set_armor_model_fields(
          plugin,
          key,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          "Meshes\\Armor\\Female.nif",
          std::nullopt,
          std::nullopt)),
      fo4::forms::ArmorError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(ArmorEdit, ReplacesExistingLegacyFieldWithoutGuessingInsertion) {
  auto plugin = fo4::read_plugin(legacy_armor(true), "Legacy.esp");
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  const auto* before = fo4::find_record(plugin, key);
  ASSERT_NE(before, nullptr);
  const auto* before_bod2 = find_subrecord(*before, "BOD2");
  ASSERT_NE(before_bod2, nullptr);
  const auto bod2 = before_bod2->data;

  ASSERT_TRUE(fo4::forms::set_armor_model_fields(
      plugin,
      key,
      "Meshes\\Armor\\NewMale.nif",
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  const auto model = fo4::forms::decode_armor(reread, key);
  ASSERT_TRUE(model.male_world_model.has_value());
  EXPECT_EQ(*model.male_world_model, "Meshes\\Armor\\NewMale.nif");
  const auto* after = fo4::find_record(reread, key);
  ASSERT_NE(after, nullptr);
  const auto* after_bod2 = find_subrecord(*after, "BOD2");
  ASSERT_NE(after_bod2, nullptr);
  EXPECT_EQ(after_bod2->data, bod2);
}

TEST(ArmorEdit, NonAsciiGenderedModelIsStoredAsWindows1252) {
  auto plugin = fo4::read_plugin(legacy_armor(true), "Legacy.esp");
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");

  ASSERT_TRUE(fo4::forms::set_armor_model_fields(
      plugin,
      key,
      "Meshes\\Armor\\Caf\xC3\xA9.nif",
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Legacy.esp");
  const auto model = fo4::forms::decode_armor(reread, key);
  ASSERT_TRUE(model.male_world_model.has_value());
  EXPECT_EQ(*model.male_world_model, "Meshes\\Armor\\Caf\xE9.nif");
}
