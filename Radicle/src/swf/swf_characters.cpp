#include "fo4/swf/swf_characters.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

// Constraint: a raw character tag must start with its little-endian UI16 id.
bool leading_id(const SwfTag& tag, std::uint16_t& out, std::string& message) {
  if (tag.payload.size() < 2) {
    message = "tag " + std::to_string(tag.code) + " carries a character but no character id";
    return false;
  }
  out = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(tag.payload[0]) |
                                   (std::to_integer<std::uint16_t>(tag.payload[1]) << 8));
  return true;
}

// Constraint: one id must name one definition, so a repeated id is a refusal rather than a merge.
bool add_character(SwfCharacterModel& model, std::map<std::uint16_t, std::size_t>& by_id,
                   SwfCharacter character, std::string& message) {
  const auto existing = by_id.find(character.character_id);
  if (existing != by_id.end()) {
    message = "character id " + std::to_string(character.character_id) + " is defined twice (tags " +
              std::to_string(model.characters[existing->second].tag_index) + " and " +
              std::to_string(character.tag_index) + ")";
    return false;
  }
  by_id.emplace(character.character_id, model.characters.size());
  model.characters.push_back(std::move(character));
  return true;
}

// Constraint: a name-table entry for no defined character must name nothing, never refuse the model.
void apply_name(SwfCharacterModel& model, const std::map<std::uint16_t, std::size_t>& by_id,
                const SwfSymbol& symbol, bool overwrite) {
  const auto found = by_id.find(symbol.tag_id);
  if (found == by_id.end()) {
    return;
  }
  SwfCharacter& character = model.characters[found->second];
  if (overwrite || character.name.empty()) {
    character.name = symbol.name;
  }
}

// Constraint: the model must be cleared and marked refused together so a caller never sees a partial map.
bool refuse(SwfCharacterModel& model, std::string& message, std::string text) {
  model.refused = true;
  model.characters.clear();
  message = std::move(text);
  model.message = message;
  return false;
}

}  // namespace

bool build_character_model(const SwfFile& file, SwfCharacterModel& out, std::string& message) {
  out = {};
  message.clear();
  std::map<std::uint16_t, std::size_t> by_id;

  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    const SwfTag& tag = file.tags[index];
    SwfCharacter character;
    character.tag_index = index;
    bool defined = false;
    switch (tag.code) {
      case tag::kDefineShape:
      case tag::kDefineShape2:
      case tag::kDefineShape3:
      case tag::kDefineShape4: {
        if (!tag.shape) {
          return refuse(out, message, "shape tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.shape->shape_id;
        character.kind = SwfCharacterKind::shape;
        character.has_bounds = true;
        character.bounds = tag.shape->bounds;
        defined = true;
        break;
      }
      case tag::kDefineMorphShape:
      case tag::kDefineMorphShape2: {
        if (!tag.morph_shape) {
          return refuse(out, message, "morph tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.morph_shape->character_id;
        character.kind = SwfCharacterKind::morph;
        character.has_bounds = true;
        character.bounds = tag.morph_shape->start_bounds;
        defined = true;
        break;
      }
      case tag::kDefineBitsLossless:
      case tag::kDefineBitsLossless2: {
        if (!tag.bitmap) {
          return refuse(out, message, "bitmap tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.bitmap->character_id;
        character.kind = SwfCharacterKind::bitmap;
        character.has_bitmap_size = true;
        character.bitmap_width = tag.bitmap->width;
        character.bitmap_height = tag.bitmap->height;
        defined = true;
        break;
      }
      case tag::kDefineBits:
      case tag::kDefineBitsJpeg2:
      case tag::kDefineBitsJpeg3:
      case tag::kDefineBitsJpeg4: {
        if (!tag.jpeg) {
          return refuse(out, message, "jpeg tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.jpeg->character_id;
        character.kind = SwfCharacterKind::bitmap;
        defined = true;
        break;
      }
      case tag::kDefineText:
      case tag::kDefineText2: {
        if (!tag.text) {
          return refuse(out, message, "text tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.text->character_id;
        character.kind = SwfCharacterKind::text;
        character.has_bounds = true;
        character.bounds = tag.text->bounds;
        defined = true;
        break;
      }
      case tag::kDefineEditText: {
        if (!tag.edit_text) {
          return refuse(out, message, "edit-text tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.edit_text->character_id;
        character.kind = SwfCharacterKind::edit_text;
        character.has_bounds = true;
        character.bounds = tag.edit_text->bounds;
        defined = true;
        break;
      }
      case tag::kDefineFont:
      case tag::kDefineFont2:
      case tag::kDefineFont3: {
        if (!tag.font) {
          return refuse(out, message, "font tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.font->font_id;
        character.kind = SwfCharacterKind::font;
        defined = true;
        break;
      }
      case tag::kDefineFont4: {
        if (!leading_id(tag, character.character_id, message)) {
          return refuse(out, message, message);
        }
        character.kind = SwfCharacterKind::font;
        defined = true;
        break;
      }
      case tag::kDefineSprite: {
        if (!tag.sprite_id) {
          return refuse(out, message, "sprite tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = *tag.sprite_id;
        character.kind = SwfCharacterKind::sprite;
        defined = true;
        break;
      }
      case tag::kDefineButton:
      case tag::kDefineButton2: {
        if (!leading_id(tag, character.character_id, message)) {
          return refuse(out, message, message);
        }
        character.kind = SwfCharacterKind::button;
        defined = true;
        break;
      }
      case tag::kDefineSound: {
        if (!tag.sound) {
          return refuse(out, message, "sound tag " + std::to_string(index) + " did not decode");
        }
        character.character_id = tag.sound->sound_id;
        character.kind = SwfCharacterKind::sound;
        defined = true;
        break;
      }
      case tag::kDefineVideoStream: {
        if (!leading_id(tag, character.character_id, message)) {
          return refuse(out, message, message);
        }
        character.kind = SwfCharacterKind::video;
        defined = true;
        break;
      }
      case tag::kImportAssets:
      case tag::kImportAssets2: {
        if (!tag.imports) {
          return refuse(out, message, "import tag " + std::to_string(index) + " did not decode");
        }
        for (const SwfSymbol& symbol : tag.imports->symbols) {
          SwfCharacter imported;
          imported.character_id = symbol.tag_id;
          imported.kind = SwfCharacterKind::imported;
          imported.tag_index = index;
          imported.imported_from = tag.imports->source_movie;
          imported.imported_name = symbol.name;
          if (!add_character(out, by_id, std::move(imported), message)) {
            return refuse(out, message, message);
          }
        }
        break;
      }
      default:
        break;
    }
    if (!defined) {
      continue;
    }
    if (!add_character(out, by_id, std::move(character), message)) {
      return refuse(out, message, message);
    }
  }

  for (const SwfTag& tag : file.tags) {
    if (!tag.symbols) {
      continue;
    }
    for (const SwfSymbol& symbol : *tag.symbols) {
      apply_name(out, by_id, symbol, true);
    }
  }
  for (const SwfTag& tag : file.tags) {
    if (!tag.exports) {
      continue;
    }
    for (const SwfSymbol& symbol : *tag.exports) {
      apply_name(out, by_id, symbol, false);
    }
  }

  std::sort(out.characters.begin(), out.characters.end(),
            [](const SwfCharacter& left, const SwfCharacter& right) {
              return left.character_id < right.character_id;
            });
  return true;
}

const SwfCharacter* find_character(const SwfCharacterModel& model,
                                   std::uint16_t character_id) noexcept {
  const auto found = std::find_if(model.characters.begin(), model.characters.end(),
                                  [character_id](const SwfCharacter& character) {
                                    return character.character_id == character_id;
                                  });
  return found == model.characters.end() ? nullptr : &*found;
}

}  // namespace fo4::swf
