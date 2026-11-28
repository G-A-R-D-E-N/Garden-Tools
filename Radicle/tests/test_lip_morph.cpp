#include "fo4/audio/lip_morph.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_sound.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::audio::lip_phoneme_name;
using fo4::audio::lip_slot_morphs;
using fo4::audio::lip_slot_visemes;
using fo4::audio::lip_viseme_name;
using fo4::audio::lip_viseme_recipe;

float weight_of(const std::vector<fo4::audio::LipMorphWeight>& morphs, std::string_view name) {
  for (const auto& morph : morphs) {
    if (morph.morph == name) return morph.weight;
  }
  return -1.0f;
}

}  // namespace

TEST(LipMorph, PhonemeNamesAreTheFortyThreeSlotsInFileOrder) {
  EXPECT_EQ(lip_phoneme_name(0), "IY");
  EXPECT_EQ(lip_phoneme_name(5), "AA");
  EXPECT_EQ(lip_phoneme_name(24), "M");
  EXPECT_EQ(lip_phoneme_name(40), "SIL");
  EXPECT_EQ(lip_phoneme_name(42), "FLAP");
  std::set<std::string_view> unique;
  for (std::uint32_t slot = 0; slot < fo4::audio::k_lip_phoneme_count; ++slot) unique.insert(lip_phoneme_name(slot));
  EXPECT_EQ(unique.size(), 43u);
  EXPECT_THROW(lip_phoneme_name(43), std::invalid_argument);
}

TEST(LipMorph, VisemeNamesAreTheSixteenLegacyTargets) {
  EXPECT_EQ(lip_viseme_name(0), "Aah");
  EXPECT_EQ(lip_viseme_name(2), "BMP");
  EXPECT_EQ(lip_viseme_name(15), "W");
  EXPECT_THROW(lip_viseme_name(16), std::invalid_argument);
}

TEST(LipMorph, SlotsWeightTheLegacyVisemes) {
  const auto bmp = lip_slot_visemes(24);
  ASSERT_EQ(bmp.size(), 1u);
  EXPECT_EQ(lip_viseme_name(bmp[0].viseme), "BMP");
  EXPECT_FLOAT_EQ(bmp[0].weight, 1.0f);
  const auto ih = lip_slot_visemes(1);
  ASSERT_EQ(ih.size(), 2u);
  EXPECT_EQ(lip_viseme_name(ih[0].viseme), "Eee");
  EXPECT_EQ(lip_viseme_name(ih[1].viseme), "Eh");
  EXPECT_TRUE(lip_slot_visemes(40).empty());
  EXPECT_TRUE(lip_slot_visemes(41).empty());
  EXPECT_TRUE(lip_slot_visemes(42).empty());
}

TEST(LipMorph, EveryRecipeHasMorphsWithWeightsInRange) {
  for (std::uint32_t viseme = 0; viseme < fo4::audio::k_lip_viseme_count; ++viseme) {
    const auto recipe = lip_viseme_recipe(viseme);
    EXPECT_FALSE(recipe.empty()) << lip_viseme_name(viseme);
    std::set<std::string_view> unique;
    for (const auto& entry : recipe) {
      EXPECT_GT(entry.weight, 0.0f);
      EXPECT_LE(entry.weight, 1.0f);
      EXPECT_TRUE(unique.insert(entry.morph).second) << lip_viseme_name(viseme) << " repeats " << entry.morph;
    }
  }
}

TEST(LipMorph, SlotMorphsMultiplyTheVisemeWeightByTheRecipeAndMergeByName) {
  const auto bmp = lip_slot_morphs(24);
  ASSERT_EQ(bmp.size(), 1u);
  EXPECT_EQ(bmp[0].morph, "StickyLips");
  EXPECT_FLOAT_EQ(bmp[0].weight, 1.0f);

  const auto ih = lip_slot_morphs(1);
  EXPECT_NEAR(weight_of(ih, "JawOpen"), 0.2f * 0.15f + 0.7f * 0.4f, 1e-6);
  EXPECT_NEAR(weight_of(ih, "LSmile"), 0.2f * 0.6f, 1e-6);
  EXPECT_NEAR(weight_of(ih, "LLipCornerOut"), 0.2f * 0.3f + 0.7f * 0.2f, 1e-6);
  std::set<std::string_view> unique;
  for (const auto& morph : ih) EXPECT_TRUE(unique.insert(morph.morph).second) << morph.morph;
}

TEST(LipMorph, SlotsWithAnAllZeroPhwtRowDriveNoMorph) {
  EXPECT_TRUE(lip_slot_morphs(40).empty());
  EXPECT_TRUE(lip_slot_morphs(41).empty());
  EXPECT_TRUE(lip_slot_morphs(42).empty());
  EXPECT_THROW(lip_slot_morphs(43), std::invalid_argument);
}

TEST(LipMorph, EverySlotStaysWithinOne) {
  for (std::uint32_t slot = 0; slot < fo4::audio::k_lip_phoneme_count; ++slot) {
    for (const auto& morph : lip_slot_morphs(slot)) {
      EXPECT_GE(morph.weight, 0.0f) << slot;
      EXPECT_LE(morph.weight, 1.0f) << slot;
    }
  }
}

TEST(LipMorphCApi, NamesAndMorphsMatchTheLibrary) {
  const char* name = nullptr;
  ASSERT_EQ(radicle_lip_phoneme_name(24, &name), RADICLE_STATUS_OK);
  EXPECT_STREQ(name, "M");
  EXPECT_EQ(radicle_lip_phoneme_name(43, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(name, nullptr);
  EXPECT_EQ(radicle_lip_phoneme_name(0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  size_t count = 99;
  ASSERT_EQ(radicle_lip_slot_morph_count(24, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
  const char* morph = nullptr;
  float weight = 0.0f;
  ASSERT_EQ(radicle_lip_slot_morph(24, 0, &morph, &weight), RADICLE_STATUS_OK);
  EXPECT_STREQ(morph, "StickyLips");
  EXPECT_FLOAT_EQ(weight, 1.0f);
  EXPECT_EQ(radicle_lip_slot_morph(24, 1, &morph, &weight), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_lip_slot_morph_count(40, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_lip_slot_morph_count(43, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_lip_slot_morph_count(0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
