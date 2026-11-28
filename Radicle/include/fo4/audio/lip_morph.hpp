#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::audio {

inline constexpr std::uint32_t k_lip_phoneme_count = 43;
inline constexpr std::uint32_t k_lip_viseme_count = 16;

struct LipVisemeWeight {
  std::uint32_t viseme;
  float weight;
};

struct LipMorphWeight {
  std::string_view morph;
  float weight;
};

// Invariant: slot order is the LIP phoneme order and the RACE Phoneme Target Names (PHTN) order of DefaultRace; slot 40 is SIL.
std::string_view lip_phoneme_name(std::uint32_t slot);

// Invariant: the 16 viseme names are the legacy phoneme targets (Aah .. W) the RACE PHWT rows weight.
std::string_view lip_viseme_name(std::uint32_t viseme);

std::span<const LipVisemeWeight> lip_slot_visemes(std::uint32_t slot);

// Constraint: the viseme to head morph recipe is authored; morph names are the shipped head .tri names.
std::span<const LipMorphWeight> lip_viseme_recipe(std::uint32_t viseme);

// Invariant: weights of one morph from several visemes add and clamp to 1; a morph appears once; slots with an all-zero PHWT row answer an empty list.
std::vector<LipMorphWeight> lip_slot_morphs(std::uint32_t slot);

}  // namespace fo4::audio
