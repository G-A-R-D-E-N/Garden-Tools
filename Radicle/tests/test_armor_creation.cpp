#include "fo4/forms/armor.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

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

const fo4::Group* top_group(const fo4::Plugin& plugin, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& group : plugin.groups) {
    if (group.header.group_type == static_cast<std::int32_t>(fo4::GroupType::top) &&
        group.header.top_label() == wanted) {
      return &group;
    }
  }
  return nullptr;
}

}

TEST(ArmorCreation, CreatesGenderedArmoModelsAndIconsThroughLifecycle) {
  auto plugin = fo4::read_plugin(empty_plugin(), "ArmorPatch.esp");

  fo4::forms::ArmorCreation creation;
  creation.editor_id = "GardenArmor";
  creation.male_world_model = "Meshes\\Armor\\Male.nif";
  creation.male_icon = "Textures\\Interface\\Male.dds";
  creation.male_message_icon = "Textures\\Interface\\MaleMessage.dds";
  creation.female_world_model = "Meshes\\Armor\\Female.nif";
  creation.female_icon = "Textures\\Interface\\Female.dds";
  creation.female_message_icon = "Textures\\Interface\\FemaleMessage.dds";
  creation.value = 125;
  creation.weight = 4.5f;
  creation.health = 900;

  const auto key = fo4::forms::create_armor(plugin, creation);
  EXPECT_EQ(key, fo4::FormKey::parse("000800:ArmorPatch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  ASSERT_NE(top_group(plugin, "ARMO"), nullptr);
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(plugin, key).empty());

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ArmorPatch.esp");
  const auto model = fo4::forms::decode_armor(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "GardenArmor");
  ASSERT_TRUE(model.male_world_model.has_value());
  EXPECT_EQ(*model.male_world_model, "Meshes\\Armor\\Male.nif");
  ASSERT_TRUE(model.male_icon.has_value());
  EXPECT_EQ(*model.male_icon, "Textures\\Interface\\Male.dds");
  ASSERT_TRUE(model.male_message_icon.has_value());
  EXPECT_EQ(*model.male_message_icon, "Textures\\Interface\\MaleMessage.dds");
  ASSERT_TRUE(model.female_world_model.has_value());
  EXPECT_EQ(*model.female_world_model, "Meshes\\Armor\\Female.nif");
  ASSERT_TRUE(model.female_icon.has_value());
  EXPECT_EQ(*model.female_icon, "Textures\\Interface\\Female.dds");
  ASSERT_TRUE(model.female_message_icon.has_value());
  EXPECT_EQ(*model.female_message_icon, "Textures\\Interface\\FemaleMessage.dds");
  EXPECT_EQ(model.value, 125);
  EXPECT_FLOAT_EQ(model.weight, 4.5f);
  EXPECT_EQ(model.health, 900u);
  EXPECT_EQ(reread.header.next_form_id, 0x801u);
  EXPECT_EQ(reread.header.record_count, 2u);  // ARMO + top-level GRUP.
}

TEST(ArmorCreation, CreatesArmaBipedAndFirstPersonGenderedModels) {
  auto plugin = fo4::read_plugin(empty_plugin(), "ArmorPatch.esp");

  fo4::forms::ArmorAddonCreation creation;
  creation.editor_id = "GardenArmorAddon";
  creation.male_biped_model = "Meshes\\Armor\\MaleBiped.nif";
  creation.female_biped_model = "Meshes\\Armor\\FemaleBiped.nif";
  creation.male_first_person_model = "Meshes\\Armor\\Male1st.nif";
  creation.female_first_person_model = "Meshes\\Armor\\Female1st.nif";
  creation.male_priority = 3;
  creation.female_priority = 4;
  creation.male_weight_slider = 2;
  creation.female_weight_slider = 2;
  creation.detection_sound_value = 7;
  creation.weapon_adjust = 1.25f;

  const auto key = fo4::forms::create_armor_addon(plugin, creation);
  EXPECT_EQ(key, fo4::FormKey::parse("000800:ArmorPatch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  ASSERT_NE(top_group(plugin, "ARMA"), nullptr);
  EXPECT_TRUE(fo4::schema::preflight_authoring_record(plugin, key).empty());

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ArmorPatch.esp");
  const auto model = fo4::forms::decode_armor_addon(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "GardenArmorAddon");
  ASSERT_TRUE(model.male_biped_model.has_value());
  EXPECT_EQ(*model.male_biped_model, "Meshes\\Armor\\MaleBiped.nif");
  ASSERT_TRUE(model.female_biped_model.has_value());
  EXPECT_EQ(*model.female_biped_model, "Meshes\\Armor\\FemaleBiped.nif");
  ASSERT_TRUE(model.male_first_person_model.has_value());
  EXPECT_EQ(*model.male_first_person_model, "Meshes\\Armor\\Male1st.nif");
  ASSERT_TRUE(model.female_first_person_model.has_value());
  EXPECT_EQ(*model.female_first_person_model, "Meshes\\Armor\\Female1st.nif");
  EXPECT_EQ(model.male_priority, 3u);
  EXPECT_EQ(model.female_priority, 4u);
  EXPECT_EQ(model.male_weight_slider, 2u);
  EXPECT_EQ(model.female_weight_slider, 2u);
  EXPECT_EQ(model.detection_sound_value, 7u);
  EXPECT_FLOAT_EQ(model.weapon_adjust, 1.25f);
  EXPECT_EQ(reread.header.record_count, 2u);  // ARMA + top-level GRUP.
}

TEST(ArmorCreation, InvalidGenderedStringDoesNotConsumeFormId) {
  auto plugin = fo4::read_plugin(empty_plugin(), "ArmorPatch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  fo4::forms::ArmorCreation creation;
  creation.male_world_model = std::string_view{"Bad\0Model", 9};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_armor(plugin, creation)),
      fo4::forms::ArmorError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}

TEST(ArmorCreation, InvalidArmaWeightSliderFlagsDoNotConsumeFormId) {
  auto plugin = fo4::read_plugin(empty_plugin(), "ArmorPatch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  fo4::forms::ArmorAddonCreation creation;
  creation.male_weight_slider = 0x80;
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_armor_addon(plugin, creation)),
      fo4::forms::ArmorError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}
