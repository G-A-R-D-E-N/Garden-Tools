#include "fo4/forms/perk.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using Payload = std::vector<std::pair<std::string, std::vector<std::byte>>>;

Payload subrecord_payload(const fo4::Record& record) {
  Payload payload;
  for (const auto& subrecord : record.subrecords) {
    payload.emplace_back(subrecord.signature.to_string(), subrecord.data);
  }
  return payload;
}

std::vector<std::filesystem::path> official_master_paths(const char* directory) {
  constexpr std::array<const char*, 7> names{
      "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
      "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"};
  std::vector<std::filesystem::path> paths;
  for (const auto* name : names) {
    auto path = std::filesystem::path{directory} / name;
    if (std::filesystem::exists(path)) paths.push_back(std::move(path));
  }
  return paths;
}

std::string property_key(const std::filesystem::path& path, const char* what) {
  std::string key = path.filename().string() + "_" + what;
  for (auto& ch : key) {
    if (ch == '.') ch = '_';
  }
  return key;
}

std::vector<fo4::FormKey> perk_keys(const fo4::Plugin& plugin) {
  std::vector<fo4::FormKey> perks;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature.to_string() == "PERK") perks.push_back(record.form_key);
  });
  return perks;
}

}  // namespace

// Detects: set_perk_next with each vanilla NNAM must leave every PERK subrecord byte-identical in every official master.
TEST(PerkEffectsReal, SetPerkNextRewritesEveryVanillaNextLinkByteForByte) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run the official-master PERK link round-trip";
  }
  record_game_build(detect_game_build(directory));

  std::size_t total = 0;
  std::size_t linked_total = 0;
  for (const auto& path : official_master_paths(directory)) {
    const auto original = fo4::read_plugin_file(path);
    const auto perks = perk_keys(original);
    total += perks.size();

    auto copy = original;
    std::size_t linked = 0;
    for (const auto& key : perks) {
      const auto* source = fo4::find_record(original, key);
      ASSERT_NE(source, nullptr) << key.to_string();
      const auto links = fo4::forms::read_perk_links(original, *source);
      if (links.next_perk == fo4::FormKey{}) continue;
      ++linked;
      ASSERT_EQ(fo4::forms::set_perk_next(copy, key, links.next_perk), fo4::forms::PerkLinkWrite::written)
          << key.to_string();
      const auto* rewritten = fo4::find_record(copy, key);
      ASSERT_NE(rewritten, nullptr) << key.to_string();
      EXPECT_EQ(subrecord_payload(*rewritten), subrecord_payload(*source)) << key.to_string();
    }
    linked_total += linked;
    RecordProperty(property_key(path, "PerkCount"), static_cast<int>(perks.size()));
    RecordProperty(property_key(path, "PerkNextLinkCount"), static_cast<int>(linked));
  }
  ASSERT_GT(total, 0u);
  EXPECT_GT(linked_total, 0u);
  RecordProperty("OfficialPerkCount", static_cast<int>(total));
  RecordProperty("OfficialPerkNextLinkCount", static_cast<int>(linked_total));
}

// Detects: rewriting each vanilla PERK's existing SNAM and FNAM to the value it already holds must leave every PERK subrecord byte-identical in every official master.
TEST(PerkEffectsReal, SetPerkSoundAndSwfRewriteEveryVanillaSnamAndFnamByteForByte) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run the official-master PERK sound and SWF round-trip";
  }
  record_game_build(detect_game_build(directory));

  std::size_t total = 0;
  std::size_t sounds_total = 0;
  std::size_t swfs_total = 0;
  for (const auto& path : official_master_paths(directory)) {
    const auto original = fo4::read_plugin_file(path);
    const auto perks = perk_keys(original);
    total += perks.size();

    auto copy = original;
    std::size_t sounds = 0;
    std::size_t swfs = 0;
    for (const auto& key : perks) {
      const auto* source = fo4::find_record(original, key);
      ASSERT_NE(source, nullptr) << key.to_string();
      const auto links = fo4::forms::read_perk_links(original, *source);
      if (links.sound != fo4::FormKey{}) {
        ++sounds;
        ASSERT_EQ(fo4::forms::set_perk_sound(copy, key, links.sound), fo4::forms::PerkLinkWrite::written)
            << key.to_string();
      }
      if (links.swf.has_value()) {
        ++swfs;
        ASSERT_FALSE(links.swf->empty()) << "empty FNAM " << key.to_string();
        ASSERT_EQ(fo4::forms::set_perk_swf(copy, key, *links.swf), fo4::forms::PerkLinkWrite::written)
            << key.to_string();
      }
      const auto* rewritten = fo4::find_record(copy, key);
      ASSERT_NE(rewritten, nullptr) << key.to_string();
      EXPECT_EQ(subrecord_payload(*rewritten), subrecord_payload(*source)) << key.to_string();
    }
    sounds_total += sounds;
    swfs_total += swfs;
    RecordProperty(property_key(path, "PerkCount"), static_cast<int>(perks.size()));
    RecordProperty(property_key(path, "PerkSoundCount"), static_cast<int>(sounds));
    RecordProperty(property_key(path, "PerkSwfCount"), static_cast<int>(swfs));
  }
  ASSERT_GT(total, 0u);
  EXPECT_GT(sounds_total, 0u);
  EXPECT_GT(swfs_total, 0u);
  RecordProperty("OfficialPerkCount", static_cast<int>(total));
  RecordProperty("OfficialPerkSoundCount", static_cast<int>(sounds_total));
  RecordProperty("OfficialPerkSwfCount", static_cast<int>(swfs_total));
}

// Detects: set_perk_effects must rewrite every vanilla PERK in every official master byte-for-byte, and the seven masters must hold the 656 PERK records the note records.
TEST(PerkEffectsReal, SetPerkEffectsRewritesEveryOfficialMasterPerkByteForByte) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (directory == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run the official-master PERK effect round-trip";
  }
  record_game_build(detect_game_build(directory));

  const auto masters = official_master_paths(directory);
  std::size_t total = 0;
  for (const auto& path : masters) {
    const auto original = fo4::read_plugin_file(path);
    const auto perks = perk_keys(original);
    total += perks.size();
    RecordProperty(property_key(path, "PerkCount"), static_cast<int>(perks.size()));

    auto copy = original;
    for (const auto& key : perks) {
      const auto* source = fo4::find_record(copy, key);
      ASSERT_NE(source, nullptr) << key.to_string();
      const auto effects = fo4::forms::read_perk_effects(copy, *source);
      ASSERT_EQ(fo4::forms::set_perk_effects(copy, key, effects),
                fo4::forms::PerkEffectWrite::written)
          << key.to_string();
      const auto* rewritten = fo4::find_record(copy, key);
      const auto* expected = fo4::find_record(original, key);
      ASSERT_NE(rewritten, nullptr) << key.to_string();
      ASSERT_NE(expected, nullptr) << key.to_string();
      EXPECT_EQ(subrecord_payload(*rewritten), subrecord_payload(*expected)) << key.to_string();
    }
  }
  ASSERT_GT(total, 0u);
  RecordProperty("OfficialPerkCount", static_cast<int>(total));
  RecordProperty("OfficialMasterCount", static_cast<int>(masters.size()));
  if (masters.size() == 7) EXPECT_EQ(total, 656u);
}
