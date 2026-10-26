#include "fo4/c_api_behavior.h"
#include "fo4/plugin/reader.hpp"
#include "package_tree_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

namespace fixture = fo4::test::packtree;

struct File {
  std::filesystem::path root;
  std::filesystem::path path;
  radicle_plugin* plugin{nullptr};
  File() {
    root = std::filesystem::temp_directory_path() /
           ("radicle_pack_tree_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Pack.esp";
    const auto bytes = fixture::plugin();
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
  }
  ~File() {
    radicle_plugin_destroy(plugin);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  bool open() { return radicle_plugin_read_file(path.string().c_str(), &plugin) == RADICLE_STATUS_OK; }
};

}  // namespace

TEST(CApiPackageTree, AddsAndRemovesNodesWithOutIndexResetAndNamedRefusals) {
  File file;
  ASSERT_TRUE(file.open());
  const radicle_form_key key{"Pack.esp", 0x800};
  auto* plugin = file.plugin;
  size_t added = 99;
  EXPECT_EQ(radicle_package_procedure_add(plugin, key, 1, "Activate", &added), RADICLE_STATUS_OK);
  EXPECT_NE(added, 0u);
  added = 99;
  EXPECT_EQ(radicle_package_procedure_add(plugin, key, 1, "Travel", &added), RADICLE_STATUS_OK);
  EXPECT_NE(added, 0u);
  added = 99;
  EXPECT_EQ(radicle_package_procedure_add(plugin, key, 2, "Activate", &added), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(added, 0u);
  EXPECT_EQ(radicle_package_procedure_add(plugin, key, 0, nullptr, &added), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_package_procedure_remove(plugin, key, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_package_procedure_remove(plugin, radicle_form_key{"Pack.esp", 0x801}, 2),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_package_procedure_remove(plugin, key, added), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, (file.root / "Saved.esp").string().c_str()), RADICLE_STATUS_OK);
  const auto saved = fo4::read_plugin_file(file.root / "Saved.esp");
  const auto* record = fo4::find_record(saved, fo4::FormKey{fo4::PluginKey::from_filename("Pack.esp"),
                                                             fo4::LocalFormId{0x800}});
  ASSERT_NE(record, nullptr);
  const auto decoded = fo4::env::decode_package_procedure(*record);
  EXPECT_TRUE(decoded.decoded);
  EXPECT_EQ(decoded.nodes.size(), 6u);
  EXPECT_EQ(decoded.inputs.size(), 8u);
}
