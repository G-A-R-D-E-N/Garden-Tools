#include "fo4/plugin/load_order.hpp"

#include <gtest/gtest.h>

#include <array>

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
