#include "fo4/archive/ba2.hpp"
#include "fo4/audio/lip_morph.hpp"
#include "fo4/nif/tri_file.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "real_data.hpp"
#include "real_data_build.hpp"
#include "real_meshes_archive.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

fo4::nif::TriFile read_head(const fo4::archive::Ba2GeneralArchive& archive, const char* entry_path) {
  const auto* entry = fo4::archive::find_ba2_general_entry(archive, entry_path);
  EXPECT_NE(entry, nullptr) << entry_path;
  if (entry == nullptr) return {};
  return fo4::nif::parse_tri(fo4::archive::extract_ba2_general_entry(*entry));
}

}  // namespace

TEST(LipMorphRealData, EveryRecipeMorphExistsOnTheShippedMaleAndFemaleHeads) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  record_game_build(detect_game_build(dir));
  const auto* archive = fo4::test::shared_meshes_archive(dir);
  if (archive == nullptr) GTEST_SKIP() << "no Fallout4 - Meshes.ba2 in FO4_DATA_DIR";

  const char* heads[] = {"Meshes\\Actors\\Character\\CharacterAssets\\BaseMaleHead.tri",
                         "Meshes\\Actors\\Character\\CharacterAssets\\BaseFemaleHead.tri"};
  std::set<std::string> used;
  for (std::uint32_t slot = 0; slot < fo4::audio::k_lip_phoneme_count; ++slot) {
    for (const auto& morph : fo4::audio::lip_slot_morphs(slot)) used.emplace(morph.morph);
  }
  std::size_t slots_driving_morphs = 0;
  for (std::uint32_t slot = 0; slot < fo4::audio::k_lip_phoneme_count; ++slot) {
    if (!fo4::audio::lip_slot_morphs(slot).empty()) ++slots_driving_morphs;
  }
  for (const char* head : heads) {
    const auto tri = read_head(*archive, head);
    ASSERT_GT(tri.morphs.size(), 0u) << head;
    for (const auto& name : used) EXPECT_NE(tri.find(name), nullptr) << head << " lacks " << name;
    std::cout << head << ": " << tri.vertex_count << " vertices, " << tri.morphs.size() << " morphs, " << used.size()
              << " recipe morphs all present, " << slots_driving_morphs << "/43 slots drive morphs\n";
  }
  EXPECT_EQ(slots_driving_morphs, 40u);
}

TEST(LipMorphRealData, TheEmbeddedSlotNamesAndVisemeRowsAreTheDefaultRacePhtnAndPhwt) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  const auto path = std::filesystem::path(dir) / "Fallout4.esm";
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "no Fallout4.esm in FO4_DATA_DIR";
  auto plugin = fo4::read_plugin_file(path);

  const auto race = fo4::Signature::from_string("RACE");
  const auto edid = fo4::Signature::from_string("EDID");
  const auto phtn = fo4::Signature::from_string("PHTN");
  const auto phwt = fo4::Signature::from_string("PHWT");
  constexpr std::uint32_t kRowStride = 43;
  std::vector<std::string> names;
  std::vector<float> weights;
  std::size_t default_races = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != race) return;
    bool is_default = false;
    for (const auto& sub : record.subrecords) {
      if (sub.signature != edid) continue;
      const std::string text(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
      is_default = text.compare(0, 11, "DefaultRace") == 0 && (text.size() == 11 || text[11] == 0);
    }
    if (!is_default) return;
    ++default_races;
    for (const auto& sub : record.subrecords) {
      if (sub.signature == phtn) {
        std::string text(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
        text.resize(text.find(static_cast<char>(0)) == std::string::npos ? text.size() : text.find(static_cast<char>(0)));
        names.push_back(text);
      } else if (sub.signature == phwt) {
        ASSERT_EQ(sub.data.size() % sizeof(float), 0u);
        const auto old = weights.size();
        weights.resize(old + sub.data.size() / sizeof(float));
        std::memcpy(weights.data() + old, sub.data.data(), sub.data.size());
      }
    }
  });
  ASSERT_EQ(default_races, 1u);
  ASSERT_EQ(names.size(), static_cast<std::size_t>(fo4::audio::k_lip_phoneme_count));
  ASSERT_EQ(weights.size(), static_cast<std::size_t>(fo4::audio::k_lip_phoneme_count) * kRowStride);
  for (std::uint32_t slot = 0; slot < fo4::audio::k_lip_phoneme_count; ++slot) {
    EXPECT_EQ(fo4::audio::lip_phoneme_name(slot), names[slot]) << slot;
    std::vector<float> row(fo4::audio::k_lip_viseme_count, 0.0f);
    for (const auto& entry : fo4::audio::lip_slot_visemes(slot)) row[entry.viseme] = entry.weight;
    for (std::uint32_t viseme = 0; viseme < fo4::audio::k_lip_viseme_count; ++viseme) {
      EXPECT_FLOAT_EQ(row[viseme], weights[slot * kRowStride + viseme]) << names[slot] << " viseme " << viseme;
    }
    for (std::uint32_t column = fo4::audio::k_lip_viseme_count; column < kRowStride; ++column) {
      EXPECT_EQ(weights[slot * kRowStride + column], 0.0f) << names[slot] << " column " << column;
    }
  }
}
