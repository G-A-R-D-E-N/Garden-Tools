#include "fo4/plugin/load_order.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

TEST(LoadOrder, BuildsSeparateRuntimeFullAndLightTables) {
  fo4::Plugin fallout4;
  fallout4.key = fo4::PluginKey::from_filename("Fallout4.esm");

  fo4::Plugin light_a;
  light_a.key = fo4::PluginKey::from_filename("LightA.esp");
  light_a.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::small_file};

  fo4::Plugin full_b;
  full_b.key = fo4::PluginKey::from_filename("FullB.esp");

  fo4::Plugin light_b;
  light_b.key = fo4::PluginKey::from_filename("LightB.esp");
  light_b.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::small_file};

  const std::array<const fo4::Plugin*, 5> order{
      &fallout4,
      &light_a,
      nullptr,
      &full_b,
      &light_b,
  };

  const auto context = fo4::build_runtime_form_id_context(order);
  ASSERT_EQ(context.full_plugins.size(), 2u);
  ASSERT_EQ(context.light_plugins.size(), 2u);

  EXPECT_EQ(context.full_plugins[0].key.filename(), "Fallout4.esm");
  EXPECT_EQ(context.full_plugins[1].key.filename(), "FullB.esp");
  EXPECT_EQ(context.light_plugins[0].key.filename(), "LightA.esp");
  EXPECT_EQ(context.light_plugins[1].key.filename(), "LightB.esp");
}

TEST(LoadOrder, ResolvesRuntimeFullAndFeFormIdsWithoutChangingPluginEncodingRules) {
  fo4::Plugin fallout4;
  fallout4.key = fo4::PluginKey::from_filename("Fallout4.esm");

  fo4::Plugin light_a;
  light_a.key = fo4::PluginKey::from_filename("LightA.esp");
  light_a.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::small_file};

  fo4::Plugin full_b;
  full_b.key = fo4::PluginKey::from_filename("FullB.esp");

  fo4::Plugin light_b;
  light_b.key = fo4::PluginKey::from_filename("LightB.esp");
  light_b.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::small_file};

  const std::array<const fo4::Plugin*, 4> order{
      &fallout4,
      &light_a,
      &full_b,
      &light_b,
  };
  const auto context = fo4::build_runtime_form_id_context(order);

  EXPECT_EQ(
      fo4::resolve_runtime_form_id(fo4::RawFormId{0x01001234}, context),
      fo4::FormKey::parse("001234:FullB.esp"));
  EXPECT_EQ(
      fo4::resolve_runtime_form_id(fo4::RawFormId{0xFE001ABC}, context),
      fo4::FormKey::parse("000ABC:LightB.esp"));
}

TEST(LoadOrder, RejectsDuplicatePluginKeysBeforeAssigningRuntimeSlots) {
  fo4::Plugin first;
  first.key = fo4::PluginKey::from_filename("Duplicate.esp");
  fo4::Plugin second;
  second.key = fo4::PluginKey::from_filename("duplicate.ESP");

  const std::array<const fo4::Plugin*, 2> order{&first, &second};
  EXPECT_THROW(fo4::build_runtime_form_id_context(order), fo4::FormIdError);
}

TEST(LoadOrder, EslExtensionAssignsALightSlotWithoutChangingHeaderFlags) {
  fo4::Plugin extension;
  extension.key = fo4::PluginKey::from_filename("Extension.ESL");
  fo4::Plugin flagged;
  flagged.key = fo4::PluginKey::from_filename("Flagged.esm");
  flagged.header.tes4_flags = fo4::RecordFlags{fo4::RecordFlags::small_file};
  fo4::Plugin full;
  full.key = fo4::PluginKey::from_filename("Full.esp");
  const std::array<const fo4::Plugin*, 3> order{&extension, &full, &flagged};

  const auto context = fo4::build_runtime_form_id_context(order);
  ASSERT_EQ(context.light_plugins.size(), 2u);
  ASSERT_EQ(context.full_plugins.size(), 1u);
  EXPECT_EQ(context.light_plugins[0].key, extension.key);
  EXPECT_EQ(context.light_plugins[1].key, flagged.key);
  EXPECT_EQ(context.full_plugins[0].key, full.key);
  EXPECT_EQ(fo4::resolve_runtime_form_id(fo4::RawFormId{0xFE000ABC}, context),
            fo4::FormKey(extension.key, fo4::LocalFormId{0xABC}));
  EXPECT_EQ(fo4::resolve_runtime_form_id(fo4::RawFormId{0xFE001ABC}, context),
            fo4::FormKey(flagged.key, fo4::LocalFormId{0xABC}));
  EXPECT_FALSE(extension.header.tes4_flags.is_small_file());
}

TEST(LoadOrder, RejectsMoreThan254FullPluginsBecauseFeAndFfAreReserved) {
  std::vector<fo4::Plugin> plugins(0xFFu);
  std::vector<const fo4::Plugin*> order;
  order.reserve(plugins.size());
  for (std::size_t index = 0; index < plugins.size(); ++index) {
    plugins[index].key = fo4::PluginKey::from_filename(
        "Full" + std::to_string(index) + ".esp");
    order.push_back(&plugins[index]);
  }

  EXPECT_THROW(fo4::build_runtime_form_id_context(order), fo4::FormIdError);
}

TEST(LoadOrder, RejectsMoreThan4096RuntimeLightPlugins) {
  std::vector<fo4::Plugin> plugins(0x1001u);
  std::vector<const fo4::Plugin*> order;
  order.reserve(plugins.size());
  for (std::size_t index = 0; index < plugins.size(); ++index) {
    plugins[index].key = fo4::PluginKey::from_filename(
        "Light" + std::to_string(index) + ".esp");
    plugins[index].header.tes4_flags =
        fo4::RecordFlags{fo4::RecordFlags::small_file};
    order.push_back(&plugins[index]);
  }

  EXPECT_THROW(fo4::build_runtime_form_id_context(order), fo4::FormIdError);
}
