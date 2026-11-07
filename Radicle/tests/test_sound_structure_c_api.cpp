#include "fo4/c_api_sound.h"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr std::uint32_t k_auto_weapon = 0xED157AE3u;
constexpr std::uint32_t k_standard = 0x1EEF540Au;

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes dword(std::uint32_t value) {
  Bytes out(4, std::byte{0});
  std::memcpy(out.data(), &value, 4);
  return out;
}

std::vector<std::byte> plugin_bytes() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto sndr = fo4::test::begin_group(out, "SNDR", 0);
  const auto auto_record = fo4::test::begin_record(out, "SNDR", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("Auto"));
  fo4::test::write_subrecord(out, "CNAM", dword(k_auto_weapon));
  fo4::test::write_subrecord(out, "ANAM", zstr("a.wav"));
  fo4::test::end_record(out, auto_record);
  const auto standard_record = fo4::test::begin_record(out, "SNDR", 0, 0x801);
  fo4::test::write_subrecord(out, "EDID", zstr("Standard"));
  fo4::test::write_subrecord(out, "CNAM", dword(k_standard));
  fo4::test::write_subrecord(out, "ANAM", zstr("b.wav"));
  fo4::test::end_record(out, standard_record);
  fo4::test::end_group(out, sndr);
  const auto misc = fo4::test::begin_group(out, "MISC", 0);
  const auto misc_record = fo4::test::begin_record(out, "MISC", 0, 0x802);
  fo4::test::write_subrecord(out, "EDID", zstr("Other"));
  fo4::test::end_record(out, misc_record);
  fo4::test::end_group(out, misc);
  return out.bytes;
}

struct SoundFile {
  std::filesystem::path root;
  std::filesystem::path path;
  radicle_plugin* plugin{nullptr};
  SoundFile() {
    root = std::filesystem::temp_directory_path() /
           ("radicle_sound_structure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    path = root / "Snd.esp";
    const auto bytes = plugin_bytes();
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
  }
  ~SoundFile() {
    radicle_plugin_destroy(plugin);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  bool open() { return radicle_plugin_read_file(path.string().c_str(), &plugin) == RADICLE_STATUS_OK; }
  std::filesystem::path out(const char* name) const { return root / name; }
};

std::vector<std::pair<std::string, Bytes>> subs_of(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<std::pair<std::string, Bytes>> out;
  const auto* record = fo4::find_record(plugin, fo4::FormKey{plugin.key, fo4::LocalFormId{local}});
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.emplace_back(sub.signature.to_string(), sub.data);
  return out;
}

}  // namespace

TEST(CApiSoundStructure, AddsRemovesAndSetsSoundFilesAndBaseLinks) {
  SoundFile file;
  ASSERT_TRUE(file.open());
  const radicle_form_key key{"Snd.esp", 0x800};
  auto* plugin = file.plugin;
  size_t index = 99;
  EXPECT_EQ(radicle_plugin_add_sound_file(plugin, key, "c.wav", &index), RADICLE_STATUS_OK);
  EXPECT_EQ(index, 1u);
  index = 99;
  EXPECT_EQ(radicle_plugin_add_sound_file(plugin, key, nullptr, &index), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(radicle_plugin_remove_sound_file(plugin, key, 5), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_sound_file(plugin, key, 0, "d.wav"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_sound_file(plugin, key, 9, "e.wav"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_sound_file(plugin, key, 1, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_sound_base_link(plugin, key, radicle_form_key{"Snd.esp", 0x801}),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_sound_base_link(plugin, key, radicle_form_key{"Snd.esp", 0x801}),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_sound_base_link(plugin, key, radicle_form_key{nullptr, 0}),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_sound_base_link(plugin, key), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_sound_base_link(plugin, key), RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_form_key standard{"Snd.esp", 0x801};
  EXPECT_EQ(radicle_plugin_add_sound_base_link(plugin, standard, radicle_form_key{"Snd.esp", 0x800}),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_sound_base_link(plugin, standard), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_sound_file(plugin, radicle_form_key{"Snd.esp", 0x802}, "x.wav", &index),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.out("Saved.esp").string().c_str()), RADICLE_STATUS_OK);
  const auto saved = fo4::read_plugin_file(file.out("Saved.esp"));
  const auto auto_subs = subs_of(saved, 0x800);
  ASSERT_EQ(auto_subs.size(), 4u);
  EXPECT_EQ(auto_subs[0].first, "EDID");
  EXPECT_EQ(auto_subs[1].first, "CNAM");
  EXPECT_EQ(auto_subs[2].first, "ANAM");
  EXPECT_EQ(auto_subs[2].second, zstr("d.wav"));
  EXPECT_EQ(auto_subs[3].first, "ANAM");
  EXPECT_EQ(auto_subs[3].second, zstr("c.wav"));
}

TEST(CApiSoundStructure, CreatesSoundMarkersAndFootstepSetsFromProvenDefaults) {
  SoundFile file;
  ASSERT_TRUE(file.open());
  auto* plugin = file.plugin;
  radicle_form_key marker{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_sound_record(plugin, "SOUN", "NewMarker", &marker), RADICLE_STATUS_OK);
  EXPECT_NE(marker.plugin, nullptr);
  EXPECT_NE(marker.local_id, 0u);
  const auto marker_id = marker.local_id;
  radicle_form_key set{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_sound_record(plugin, "FSTS", "NewFootstepSet", &set), RADICLE_STATUS_OK);
  EXPECT_NE(set.plugin, nullptr);
  const auto set_id = set.local_id;
  marker = {nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_sound_record(plugin, "MISC", "Nope", &marker), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(marker.plugin, nullptr);
  EXPECT_EQ(radicle_plugin_create_sound_record(plugin, "SOUN", "", &marker), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_sound_record(nullptr, "SOUN", "X", &marker), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_write_file(plugin, file.out("Created.esp").string().c_str()), RADICLE_STATUS_OK);
  const auto saved = fo4::read_plugin_file(file.out("Created.esp"));
  ASSERT_NE(fo4::find_record(saved, fo4::FormKey{fo4::PluginKey::from_filename("Created.esp"),
                                                 fo4::LocalFormId{marker_id}}),
            nullptr);
  const auto* marker_record = fo4::find_record(saved, fo4::FormKey{fo4::PluginKey::from_filename("Created.esp"),
                                                                   fo4::LocalFormId{set_id}});
  ASSERT_NE(marker_record, nullptr);
  bool has_xcnt = false;
  bool has_data = false;
  for (const auto& sub : marker_record->subrecords) {
    if (sub.signature.to_string() == "XCNT") has_xcnt = sub.data.size() == 20;
    if (sub.signature.to_string() == "DATA") has_data = true;
  }
  EXPECT_TRUE(has_xcnt);
  EXPECT_TRUE(has_data);
}
