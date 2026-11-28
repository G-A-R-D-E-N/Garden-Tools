#include "fo4/plugin/loose_plugins.hpp"
#include "fo4/c_api_env.h"
#include "fo4/c_api_loose_plugins.h"

#include <gtest/gtest.h>

#include <memory>

namespace {
using fo4::LoosePlugins;
using fo4::Plugin;

TEST(LoosePlugins, ReplaceReleasesDisplacedPlugin) {
  LoosePlugins plugins;
  auto previous = std::make_shared<Plugin>();
  const std::weak_ptr<Plugin> displaced = previous;
  plugins.replace("A.esp", previous);
  previous.reset();
  auto current = std::make_shared<Plugin>();
  plugins.replace("a.ESP", current);
  EXPECT_TRUE(displaced.expired());
  EXPECT_EQ(plugins.get("A.esp"), current);
}

TEST(LoosePlugins, ReplacingSamePluginKeepsItAlive) {
  LoosePlugins plugins;
  auto plugin = std::make_shared<Plugin>();
  plugins.replace("A.esp", plugin);
  plugins.replace("A.esp", plugin);
  EXPECT_EQ(plugins.get("A.esp"), plugin);
}

TEST(LoosePlugins, ReleaseRemovesAndReleasesPlugin) {
  LoosePlugins plugins;
  auto plugin = std::make_shared<Plugin>();
  const std::weak_ptr<Plugin> weak = plugin;
  plugins.replace("A.esp", plugin);
  plugin.reset();
  plugins.release("A.esp");
  EXPECT_FALSE(plugins.get("A.esp"));
  EXPECT_TRUE(weak.expired());
}

TEST(LoosePlugins, ReleaseKeepsEditablePluginAlive) {
  LoosePlugins plugins;
  auto plugin = std::make_shared<Plugin>();
  const std::weak_ptr<Plugin> weak = plugin;
  plugins.set_editable("A.esp", plugin);
  plugins.replace("A.esp", plugin);
  plugin.reset();
  plugins.release("A.esp");
  EXPECT_FALSE(weak.expired());
  EXPECT_FALSE(plugins.get("A.esp"));
  plugins.set_editable("A.esp", {});
  EXPECT_TRUE(weak.expired());
}

TEST(LoosePlugins, EditableHandoffReleasesOldOverlay) {
  LoosePlugins plugins;
  auto overlay = std::make_shared<Plugin>();
  const std::weak_ptr<Plugin> weak = overlay;
  plugins.replace("A.esp", overlay);
  overlay.reset();
  auto editable = std::make_shared<Plugin>();
  plugins.set_editable("A.esp", editable);
  plugins.replace("A.esp", editable);
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(plugins.get("A.esp"), editable);
}

TEST(LoosePlugins, ReleaseInvalidatesIndexBeforeReleasingPlugin) {
  bool destroyed_without_index = false;
  bool observe_destruction = false;
  LoosePlugins plugins;
  auto plugin = std::shared_ptr<Plugin>(new Plugin, [&](Plugin* value) {
    if (observe_destruction) destroyed_without_index = !plugins.has_index("A.esp");
    delete value;
  });
  const std::weak_ptr<Plugin> weak = plugin;
  plugins.replace("A.esp", plugin);
  plugins.index("A.esp");
  EXPECT_TRUE(plugins.has_index("A.esp"));
  plugin.reset();
  observe_destruction = true;
  EXPECT_NO_THROW(plugins.release("A.esp"));
  observe_destruction = false;
  EXPECT_FALSE(plugins.has_index("A.esp"));
  EXPECT_TRUE(weak.expired());
  EXPECT_TRUE(destroyed_without_index);
}

TEST(LoosePlugins, UnknownReleaseIsHarmless) {
  LoosePlugins plugins;
  EXPECT_NO_THROW(plugins.release("missing.esp"));
}

TEST(LoosePlugins, ReplacingPluginInvalidatesItsIndex) {
  LoosePlugins plugins;
  plugins.replace("A.esp", std::make_shared<Plugin>());
  plugins.index("A.esp");
  ASSERT_TRUE(plugins.has_index("A.esp"));
  plugins.replace("A.esp", std::make_shared<Plugin>());
  EXPECT_FALSE(plugins.has_index("A.esp"));
}

TEST(LoosePlugins, IndexSnapshotRemainsValidAfterRelease) {
  LoosePlugins plugins;
  plugins.replace("A.esp", std::make_shared<Plugin>());
  const auto index = plugins.index("A.esp");
  ASSERT_TRUE(index);
  plugins.release("A.esp");
  EXPECT_FALSE(plugins.has_index("A.esp"));
  EXPECT_TRUE(index->plugin().groups.empty());
}

TEST(LoosePluginsAbi, ReleaseRetainsEditableHandleAndDropsIndex) {
  radicle_loose_plugins* registry = nullptr;
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_loose_plugins_create(&registry), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_create("A.esp", &plugin), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_loose_plugins_set_editable(registry, "A.esp", plugin), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_loose_plugins_replace(registry, "A.esp", plugin), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_loose_plugins_index(registry, "A.esp"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_loose_plugins_release(registry, "a.ESP"), RADICLE_STATUS_OK);
  int32_t has_index = 1;
  EXPECT_EQ(radicle_loose_plugins_has_index(registry, "A.esp", &has_index), RADICLE_STATUS_OK);
  EXPECT_EQ(has_index, 0);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_GT(size, 0u);
  radicle_free_bytes(bytes);
  radicle_plugin_destroy(plugin);
  radicle_loose_plugins_destroy(registry);
}

TEST(LoosePlugins, DistinctFullNamesDoNotAliasBasename) {
  LoosePlugins plugins;
  auto path_plugin = std::make_shared<Plugin>();
  auto bare_plugin = std::make_shared<Plugin>();
  plugins.replace("folder/A.esp", path_plugin);
  plugins.replace("A.esp", bare_plugin);
  plugins.release("a.ESP");
  EXPECT_EQ(plugins.get("folder/a.ESP"), path_plugin);
  EXPECT_FALSE(plugins.get("A.esp"));
}

TEST(LoosePluginsAbi, RegistryRetainsPluginAfterCallerDestroysHandle) {
  radicle_loose_plugins* registry = nullptr;
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_loose_plugins_create(&registry), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_create("A.esp", &plugin), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_loose_plugins_replace(registry, "A.esp", plugin), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);
  plugin = nullptr;
  ASSERT_EQ(radicle_loose_plugins_get(registry, "a.ESP", &plugin), RADICLE_STATUS_OK);
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &size), RADICLE_STATUS_OK);
  EXPECT_GT(size, 0u);
  radicle_free_bytes(bytes);
  radicle_plugin_destroy(plugin);
  radicle_loose_plugins_destroy(registry);
}
}
