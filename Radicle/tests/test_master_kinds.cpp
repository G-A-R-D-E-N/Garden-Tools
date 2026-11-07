#include "fo4/plugin/master_kinds.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_header_only_plugin(std::uint32_t flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> make_plugin_with_masters() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);

  fo4::test::write_subrecord(out, "MAST", std::string_view{"FlaggedLight.esp\0", 17});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Unreadable.esp\0", 15});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.good());
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good());
}

}  // namespace

TEST(MasterKinds, StampsSmallFileEspAndPreservesUnreadableFallback) {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root = std::filesystem::temp_directory_path() / ("radicle-master-kind-" + unique);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  const auto light_path = root / "FlaggedLight.esp";
  write_bytes(light_path, make_header_only_plugin(fo4::RecordFlags::small_file));

  auto plugin = fo4::read_plugin(make_plugin_with_masters(), "Child.esp");
  ASSERT_EQ(plugin.header.masters.size(), 2u);
  EXPECT_EQ(plugin.header.masters[0].kind, fo4::PluginKind::full);
  EXPECT_EQ(plugin.header.masters[1].kind, fo4::PluginKind::full);

  const std::array master_paths{
      light_path,
      root / "Unreadable.esp",
  };
  fo4::stamp_master_kinds_from_files(plugin, master_paths);

  EXPECT_EQ(plugin.header.masters[0].kind, fo4::PluginKind::light);
  EXPECT_EQ(plugin.header.masters[1].kind, fo4::PluginKind::full);

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(MasterKinds, StampsEslExtensionWithoutLightFlagFromFile) {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root = std::filesystem::temp_directory_path() / ("radicle-master-kind-esl-" + unique);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  const auto esl_path = root / "Extension.esl";
  write_bytes(esl_path, make_header_only_plugin(0));

  auto plugin = fo4::read_plugin(make_plugin_with_masters(), "Child.esp");
  plugin.header.masters[0].key = fo4::PluginKey::from_filename("Extension.esl");
  const std::array master_paths{esl_path};
  fo4::stamp_master_kinds_from_files(plugin, master_paths);

  EXPECT_EQ(plugin.header.masters[0].kind, fo4::PluginKind::light);

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(MasterKinds, StampsEslExtensionWithoutLightFlagFromLoadedPlugin) {
  auto plugin = fo4::read_plugin(make_plugin_with_masters(), "Child.esp");
  plugin.header.masters[0].key = fo4::PluginKey::from_filename("Extension.esl");
  const std::array loaded{fo4::read_plugin(make_header_only_plugin(0), "Extension.esl")};
  fo4::stamp_master_kinds(plugin, loaded);

  EXPECT_EQ(plugin.header.masters[0].kind, fo4::PluginKind::light);
  EXPECT_EQ(plugin.header.masters[1].kind, fo4::PluginKind::full);
}
