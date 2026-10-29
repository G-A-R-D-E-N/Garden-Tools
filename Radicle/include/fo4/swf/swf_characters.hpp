#pragma once

#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::swf {

enum class SwfCharacterKind : std::uint8_t {
  shape = 0,
  morph = 1,
  bitmap = 2,
  text = 3,
  edit_text = 4,
  font = 5,
  sprite = 6,
  button = 7,
  sound = 8,
  video = 9,
  imported = 10,
};

// Constraint: one row per character id, with the definition tag, kind, drawn bounds and name-table entry.
struct SwfCharacter {
  std::uint16_t character_id{0};
  SwfCharacterKind kind{SwfCharacterKind::shape};
  std::size_t tag_index{0};
  bool has_bounds{false};
  SwfRect bounds{};
  bool has_bitmap_size{false};
  std::uint16_t bitmap_width{0};
  std::uint16_t bitmap_height{0};
  std::string name;
  std::string imported_from;
  std::string imported_name;

  bool operator==(const SwfCharacter&) const = default;
};

// Invariant: characters are sorted by character_id and the model is either complete or empty.
struct SwfCharacterModel {
  std::vector<SwfCharacter> characters;
  bool refused{false};
  std::string message;
};

// Requires every character tag to be decoded; a duplicate id or an unknown name-table reference is a refusal.
bool build_character_model(const SwfFile& file, SwfCharacterModel& out, std::string& message);

// Requires a built model; returns nullptr when no character carries the id.
const SwfCharacter* find_character(const SwfCharacterModel& model,
                                   std::uint16_t character_id) noexcept;

// Requires decoded top-level tags; a repeated id returns the last grid.
const SwfScalingGrid* find_scaling_grid(const SwfFile& file, std::uint16_t character_id) noexcept;

}  // namespace fo4::swf
