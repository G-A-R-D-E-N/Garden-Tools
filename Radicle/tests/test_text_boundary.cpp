#include "fo4/archive/assets.hpp"
#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/strings.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/text/encoding.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const std::string k_cafe_esm = "Caf\xC3\xA9.esm";

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
};

std::filesystem::path from_utf8(std::string_view text) { return std::u8string(text.begin(), text.end()); }

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> make_plugin(std::uint32_t tes4_flags, const std::vector<std::string>& masters,
                                   const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& record : records) {
    const auto group = fo4::test::begin_group(out, record.signature, 0);
    const auto header = fo4::test::begin_record(out, record.signature, 0, record.form_id);
    for (const auto& sub : record.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, header);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

class TextBoundaryTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_text_" + std::to_string(id));
    std::filesystem::create_directories(root_ / "Strings");
    const std::vector<std::byte> light_data(48);
    paths_ = {
        write(k_cafe_esm, make_plugin(0x81, {},
                                      {{"STAT", 0x800, {{"EDID", zstring("Caf\xE9")}, {"MODL", zstring("Meshes\\Caf\xE9.nif")}}},
                                       {"LIGH", 0x801, {{"DATA", light_data}, {"NAM0", zstring("Textures\\Caf\xE9.dds")}}}})),
        write("Patch.esp", make_plugin(0, {"Caf\xE9.esm"}, {{"STAT", 0x800, {{"EDID", zstring("Caf\xE9")}}}})),
    };
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view utf8_name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / from_utf8(utf8_name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  radicle_env* load_c() {
    std::vector<std::string> utf8;
    for (const auto& path : paths_) {
      const auto text = path.u8string();
      utf8.emplace_back(text.begin(), text.end());
    }
    std::vector<const char*> raw;
    for (const auto& path : utf8) raw.push_back(path.c_str());
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(raw.data(), raw.size(), &env), RADICLE_STATUS_OK);
    return env;
  }

  std::filesystem::path root_;
  std::vector<std::filesystem::path> paths_;
};

}

TEST(TextBoundary, PluginKeysIgnoreNonAsciiCase) {
  EXPECT_EQ(fo4::PluginKey::from_filename("\xC3\x89gout.esp"), fo4::PluginKey::from_filename("\xC3\xA9gout.esp"));
  EXPECT_FALSE(fo4::PluginKey::from_filename("\xC3\x89gout.esp") < fo4::PluginKey::from_filename("\xC3\xA9gout.esp"));
}

TEST(TextBoundary, FoldMatchesDotnetOrdinalIgnoreCase) {
  EXPECT_FALSE(fo4::text::equals_folded("\xC4\xB1", "I"));
  EXPECT_FALSE(fo4::text::equals_folded("\xC5\xBF", "s"));
  EXPECT_TRUE(fo4::text::equals_folded("\xE1\xBE\x80", "\xE1\xBE\x88"));
  EXPECT_TRUE(fo4::text::equals_folded("\xF0\x90\x90\x80", "\xF0\x90\x90\xA8"));
  EXPECT_EQ(fo4::text::hash_folded("\xF0\x90\x90\x80"), fo4::text::hash_folded("\xF0\x90\x90\xA8"));
}

TEST(TextBoundary, MasterNamesAreWrittenAsWindows1252) {
  auto plugin = fo4::read_plugin(make_plugin(0, {}, {}), "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::vector{fo4::PluginKey::from_filename(k_cafe_esm)});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  ASSERT_EQ(reread.tes4.subrecords.size() > 1, true);
  const auto& mast = reread.tes4.subrecords[1];
  ASSERT_EQ(mast.signature.to_string(), "MAST");
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(mast.data.data()), mast.data.size()), std::string("Caf\xE9.esm", 8) + '\0');
  ASSERT_EQ(reread.header.masters.size(), 1u);
  EXPECT_EQ(reread.header.masters[0].key.filename(), k_cafe_esm);
}

TEST_F(TextBoundaryTest, CApiTextIsUtf8) {
  auto* env = load_c();
  ASSERT_NE(env, nullptr);
  radicle_env_plugin_info info{};
  ASSERT_EQ(radicle_env_get_plugin_info(env, 0, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info.filename, k_cafe_esm.c_str());

  const radicle_record* stat = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {k_cafe_esm.c_str(), 0x800}, &stat), RADICLE_STATUS_OK);
  radicle_record_info record{};
  ASSERT_EQ(radicle_record_get_info(env, stat, &record), RADICLE_STATUS_OK);
  EXPECT_STREQ(record.editor_id, "Caf\xC3\xA9");
  EXPECT_STREQ(record.owner_plugin, k_cafe_esm.c_str());

  const char* master = nullptr;
  int32_t resolved = 0;
  ASSERT_EQ(radicle_env_plugin_master(env, 1, 0, &master, &resolved), RADICLE_STATUS_OK);
  EXPECT_STREQ(master, k_cafe_esm.c_str());
  EXPECT_EQ(resolved, 1);

  const radicle_record** versions = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_versions(env, {k_cafe_esm.c_str(), 0x800}, &versions, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  ASSERT_EQ(radicle_record_get_info(env, versions[0], &record), RADICLE_STATUS_OK);
  EXPECT_STREQ(record.model_path, "Meshes\\Caf\xC3\xA9.nif");
  radicle_free_record_array(versions);

  const radicle_record* found = nullptr;
  ASSERT_EQ(radicle_env_find_editor_id(env, "CAF\xC3\x89", nullptr, &found), RADICLE_STATUS_OK);

  const radicle_record* light = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {k_cafe_esm.c_str(), 0x801}, &light), RADICLE_STATUS_OK);
  radicle_light_info light_info{};
  ASSERT_EQ(radicle_light_get_info(env, light, &light_info), RADICLE_STATUS_OK);
  EXPECT_STREQ(light_info.gobo, "Textures\\Caf\xC3\xA9.dds");
  radicle_env_destroy(env);
}

TEST_F(TextBoundaryTest, StringTablesAndAssetsMatchNonAsciiCase) {
  fo4::localization::LocalizedStringTable table;
  table.kind = fo4::localization::StringTableKind::strings;
  table.entries.push_back({0x10, {std::byte{'x'}}});
  fo4::localization::write_string_table_file(table, root_ / "Strings" / from_utf8("CAF\xC3\x89_en.STRINGS"));
  const auto env = fo4::env::Environment::load(paths_);
  EXPECT_NE(fo4::env::StringTables::load(env, root_ / "Strings", "en").table_path(0, fo4::env::StringSource::normal),
            nullptr);

  write("CAF\xC3\x89.ba2", {});
  const std::vector<std::string> plugins{k_cafe_esm};
  EXPECT_EQ(fo4::archive::archive_load_order(root_, {}, plugins).size(), 1u);

  std::filesystem::create_directories(root_ / "Textures");
  write("Textures/\xC3\xA9.dds", {std::byte{1}});
  const auto resolver = fo4::archive::AssetResolver::open(root_, {});
  EXPECT_TRUE(resolver.locate("textures/\xC3\x89.dds").has_value());
}
