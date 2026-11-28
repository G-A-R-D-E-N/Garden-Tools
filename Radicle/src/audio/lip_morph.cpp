#include "fo4/audio/lip_morph.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace fo4::audio {
namespace {

constexpr std::array<std::string_view, k_lip_phoneme_count> k_phonemes{
    "IY", "IH", "Eh", "EY", "AE", "AA", "AW", "AY", "AH", "AO", "OY", "OW", "UH", "UW", "ER", "AX", "S", "SH", "Z", "ZH", "F", "Th", "V", "DH", "M", "N", "NG", "L", "R", "W", "Y", "HH", "B", "D", "JH", "G", "P", "T", "K", "CH", "SIL", "SHORTSIL", "FLAP"};

constexpr std::array<std::string_view, k_lip_viseme_count> k_visemes{
    "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W"};

constexpr LipVisemeWeight k_slot_0[]{{5, 1.0f}};
constexpr LipVisemeWeight k_slot_1[]{{5, 0.2f}, {6, 0.7f}};
constexpr LipVisemeWeight k_slot_2[]{{6, 1.0f}};
constexpr LipVisemeWeight k_slot_3[]{{0, 0.6f}, {5, 0.3f}};
constexpr LipVisemeWeight k_slot_4[]{{0, 0.8f}};
constexpr LipVisemeWeight k_slot_5[]{{0, 1.0f}};
constexpr LipVisemeWeight k_slot_6[]{{0, 0.2f}, {11, 0.6f}};
constexpr LipVisemeWeight k_slot_7[]{{5, 0.2f}, {8, 0.8f}};
constexpr LipVisemeWeight k_slot_8[]{{0, 0.8f}, {11, 0.2f}};
constexpr LipVisemeWeight k_slot_9[]{{0, 0.1f}, {11, 0.5f}, {15, 0.2f}};
constexpr LipVisemeWeight k_slot_10[]{{5, 0.3f}, {11, 0.6f}};
constexpr LipVisemeWeight k_slot_11[]{{1, 0.8f}, {11, 0.2f}};
constexpr LipVisemeWeight k_slot_12[]{{1, 0.5f}, {11, 0.2f}};
constexpr LipVisemeWeight k_slot_13[]{{12, 1.0f}};
constexpr LipVisemeWeight k_slot_14[]{{8, 0.5f}, {13, 0.3f}};
constexpr LipVisemeWeight k_slot_15[]{{1, 0.8f}};
constexpr LipVisemeWeight k_slot_16[]{{4, 0.9f}};
constexpr LipVisemeWeight k_slot_17[]{{3, 0.8f}};
constexpr LipVisemeWeight k_slot_18[]{{4, 0.8f}};
constexpr LipVisemeWeight k_slot_19[]{{3, 0.7f}};
constexpr LipVisemeWeight k_slot_20[]{{7, 0.8f}};
constexpr LipVisemeWeight k_slot_21[]{{14, 1.0f}};
constexpr LipVisemeWeight k_slot_22[]{{7, 1.0f}};
constexpr LipVisemeWeight k_slot_23[]{{4, 0.2f}, {14, 0.8f}};
constexpr LipVisemeWeight k_slot_24[]{{2, 1.0f}};
constexpr LipVisemeWeight k_slot_25[]{{10, 1.0f}};
constexpr LipVisemeWeight k_slot_26[]{{10, 0.9f}};
constexpr LipVisemeWeight k_slot_27[]{{10, 0.6f}, {14, 0.2f}};
constexpr LipVisemeWeight k_slot_28[]{{13, 1.0f}};
constexpr LipVisemeWeight k_slot_29[]{{15, 1.0f}};
constexpr LipVisemeWeight k_slot_30[]{{0, 0.2f}, {5, 0.4f}, {8, 0.4f}};
constexpr LipVisemeWeight k_slot_31[]{{0, 0.6f}};
constexpr LipVisemeWeight k_slot_32[]{{2, 1.0f}};
constexpr LipVisemeWeight k_slot_33[]{{4, 1.0f}};
constexpr LipVisemeWeight k_slot_34[]{{3, 1.0f}};
constexpr LipVisemeWeight k_slot_35[]{{0, 0.4f}, {3, 0.3f}};
constexpr LipVisemeWeight k_slot_36[]{{2, 0.9f}};
constexpr LipVisemeWeight k_slot_37[]{{4, 0.8f}};
constexpr LipVisemeWeight k_slot_38[]{{9, 1.0f}};
constexpr LipVisemeWeight k_slot_39[]{{3, 1.0f}};

constexpr std::array<std::span<const LipVisemeWeight>, k_lip_phoneme_count> k_slot_visemes{{
    k_slot_0,
    k_slot_1,
    k_slot_2,
    k_slot_3,
    k_slot_4,
    k_slot_5,
    k_slot_6,
    k_slot_7,
    k_slot_8,
    k_slot_9,
    k_slot_10,
    k_slot_11,
    k_slot_12,
    k_slot_13,
    k_slot_14,
    k_slot_15,
    k_slot_16,
    k_slot_17,
    k_slot_18,
    k_slot_19,
    k_slot_20,
    k_slot_21,
    k_slot_22,
    k_slot_23,
    k_slot_24,
    k_slot_25,
    k_slot_26,
    k_slot_27,
    k_slot_28,
    k_slot_29,
    k_slot_30,
    k_slot_31,
    k_slot_32,
    k_slot_33,
    k_slot_34,
    k_slot_35,
    k_slot_36,
    k_slot_37,
    k_slot_38,
    k_slot_39,
    std::span<const LipVisemeWeight>{},
    std::span<const LipVisemeWeight>{},
    std::span<const LipVisemeWeight>{}}};

constexpr LipMorphWeight k_recipe_Aah[]{{"JawOpen", 0.6f}};
constexpr LipMorphWeight k_recipe_BigAah[]{{"JawOpen", 1.0f}};
constexpr LipMorphWeight k_recipe_BMP[]{{"StickyLips", 1.0f}};
constexpr LipMorphWeight k_recipe_ChJSh[]{{"Pucker", 0.5f}, {"UprLipFunnel", 0.4f}, {"LwrLipFunnel", 0.4f}, {"JawOpen", 0.2f}};
constexpr LipMorphWeight k_recipe_DST[]{{"JawOpen", 0.2f}, {"LLipCornerOut", 0.3f}, {"RLipCornerOut", 0.3f}};
constexpr LipMorphWeight k_recipe_Eee[]{{"LSmile", 0.6f}, {"RSmile", 0.6f}, {"LLipCornerOut", 0.3f}, {"RLipCornerOut", 0.3f}, {"JawOpen", 0.15f}};
constexpr LipMorphWeight k_recipe_Eh[]{{"JawOpen", 0.4f}, {"LLipCornerOut", 0.2f}, {"RLipCornerOut", 0.2f}};
constexpr LipMorphWeight k_recipe_FV[]{{"LwrLipRollIn", 0.8f}, {"JawOpen", 0.1f}};
constexpr LipMorphWeight k_recipe_I[]{{"JawOpen", 0.3f}, {"LLipCornerOut", 0.3f}, {"RLipCornerOut", 0.3f}};
constexpr LipMorphWeight k_recipe_K[]{{"JawOpen", 0.3f}};
constexpr LipMorphWeight k_recipe_N[]{{"JawOpen", 0.2f}};
constexpr LipMorphWeight k_recipe_Oh[]{{"Pucker", 0.6f}, {"UprLipFunnel", 0.5f}, {"LwrLipFunnel", 0.5f}, {"JawOpen", 0.4f}};
constexpr LipMorphWeight k_recipe_OohQ[]{{"Pucker", 1.0f}, {"UprLipFunnel", 0.7f}, {"LwrLipFunnel", 0.7f}, {"JawOpen", 0.1f}};
constexpr LipMorphWeight k_recipe_R[]{{"Pucker", 0.3f}, {"UprLipFunnel", 0.3f}, {"LwrLipFunnel", 0.3f}, {"JawOpen", 0.2f}};
constexpr LipMorphWeight k_recipe_Th[]{{"JawOpen", 0.25f}};
constexpr LipMorphWeight k_recipe_W[]{{"Pucker", 0.8f}, {"UprLipFunnel", 0.5f}, {"LwrLipFunnel", 0.5f}};

constexpr std::array<std::span<const LipMorphWeight>, k_lip_viseme_count> k_recipes{{
    k_recipe_Aah,
    k_recipe_BigAah,
    k_recipe_BMP,
    k_recipe_ChJSh,
    k_recipe_DST,
    k_recipe_Eee,
    k_recipe_Eh,
    k_recipe_FV,
    k_recipe_I,
    k_recipe_K,
    k_recipe_N,
    k_recipe_Oh,
    k_recipe_OohQ,
    k_recipe_R,
    k_recipe_Th,
    k_recipe_W}};

}  // namespace

std::string_view lip_phoneme_name(std::uint32_t slot) {
  if (slot >= k_lip_phoneme_count) throw std::invalid_argument("lip phoneme slot is out of range");
  return k_phonemes[slot];
}

std::string_view lip_viseme_name(std::uint32_t viseme) {
  if (viseme >= k_lip_viseme_count) throw std::invalid_argument("lip viseme is out of range");
  return k_visemes[viseme];
}

std::span<const LipVisemeWeight> lip_slot_visemes(std::uint32_t slot) {
  if (slot >= k_lip_phoneme_count) throw std::invalid_argument("lip phoneme slot is out of range");
  return k_slot_visemes[slot];
}

std::span<const LipMorphWeight> lip_viseme_recipe(std::uint32_t viseme) {
  if (viseme >= k_lip_viseme_count) throw std::invalid_argument("lip viseme is out of range");
  return k_recipes[viseme];
}

std::vector<LipMorphWeight> lip_slot_morphs(std::uint32_t slot) {
  std::vector<LipMorphWeight> out;
  for (const auto& viseme : lip_slot_visemes(slot)) {
    for (const auto& entry : lip_viseme_recipe(viseme.viseme)) {
      const float add = viseme.weight * entry.weight;
      const auto found = std::find_if(out.begin(), out.end(), [&](const LipMorphWeight& have) { return have.morph == entry.morph; });
      if (found == out.end()) out.push_back({entry.morph, std::min(add, 1.0f)});
      else found->weight = std::min(found->weight + add, 1.0f);
    }
  }
  return out;
}

}  // namespace fo4::audio
