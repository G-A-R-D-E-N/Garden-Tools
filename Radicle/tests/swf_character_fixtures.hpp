#pragma once

#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace swf_character_test {

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

// Constraint: every movie must carry a ShowFrame before its terminal End so the tag walk has a frame.
inline std::vector<std::byte> movie_of(std::initializer_list<TagPayload> tags) {
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  for (const auto& tag : tags) {
    body.tag(tag.first, tag.second, false);
  }
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

// Constraint: DefineSprite must start with the id and frame count the model reports.
inline std::vector<std::byte> sprite_payload(std::uint16_t sprite_id, std::uint16_t frame_count) {
  SwfBuilder body;
  body.u16(sprite_id);
  body.u16(frame_count);
  body.tag(1);
  body.tag(0);
  return body.body;
}

// Constraint: DefineButton2 starts with the character id the model must report.
inline std::vector<std::byte> button_payload(std::uint16_t character_id) {
  SwfBuilder body;
  body.u16(character_id);
  body.u8(0);
  body.u16(0);
  return body.body;
}

// Constraint: DefineVideoStream starts with the character id the model must report.
inline std::vector<std::byte> video_payload(std::uint16_t character_id) {
  SwfBuilder body;
  body.u16(character_id);
  body.u16(1);
  body.u16(16);
  body.u16(16);
  body.u8(0);
  body.u8(0);
  return body.body;
}

// Constraint: DefineSound starts with the character id, then one flags byte and one UI32 count.
inline std::vector<std::byte> sound_payload(std::uint16_t sound_id) {
  SwfBuilder body;
  body.u16(sound_id);
  body.u8(0);
  body.u32(0);
  return body.body;
}

// Constraint: DefineFont's first offset doubles as twice the glyph count, so zero means no glyphs.
inline std::vector<std::byte> font_payload(std::uint16_t font_id) {
  SwfBuilder body;
  body.u16(font_id);
  body.u16(0);
  return body.body;
}

// Constraint: DefineEditText is id, bounds, both flag bytes, then the variable name.
inline std::vector<std::byte> edit_text_payload(std::uint16_t character_id) {
  SwfBuilder body;
  body.u16(character_id);
  body.rect(0, 400, 0, 200, 12);
  body.u8(0);
  body.u8(0);
  body.text("variable");
  return body.body;
}

// Constraint: DefineText is id, bounds, MATRIX, the two bit-widths and the record terminator.
inline std::vector<std::byte> text_payload(std::uint16_t character_id) {
  SwfBuilder body;
  body.u16(character_id);
  body.rect(0, 600, 0, 300, 12);
  swf_art_test::identity_matrix(body);
  body.u8(0);
  body.u8(0);
  body.u8(0);
  return body.body;
}

// Constraint: DefineBitsJpeg2 is a UI16 character id followed by the JPEG stream.
inline std::vector<std::byte> jpeg_payload(std::uint16_t character_id) {
  SwfBuilder body;
  body.u16(character_id);
  body.bytes(swf_art_test::minimal_jpeg(2, 2));
  return body.body;
}

// Constraint: ImportAssets is the source movie, then the imported symbol pairs.
inline std::vector<std::byte> import_payload() {
  SwfBuilder body;
  body.text("Shared.swf");
  body.u16(2);
  body.u16(20);
  body.text("FontA");
  body.u16(21);
  body.text("IconB");
  return body.body;
}

inline std::vector<std::byte> symbol_text(std::string_view name) {
  std::vector<std::byte> bytes;
  for (const char ch : name) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

// Constraint: a name table is a UI16 count followed by id/name pairs.
inline std::vector<std::byte> symbol_payload(
    std::initializer_list<std::pair<std::uint16_t, std::vector<std::byte>>> symbols) {
  SwfBuilder body;
  body.u16(static_cast<std::uint32_t>(symbols.size()));
  for (const auto& symbol : symbols) {
    body.u16(symbol.first);
    body.text(reinterpret_cast<const char*>(symbol.second.data()));
  }
  return body.body;
}

// Constraint: this fixture must carry every kind the model reports.
inline std::vector<std::byte> every_kind_movie() {
  return movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {46, swf_art_test::morph_payload(false)},
      {36, swf_art_test::lossless_payload(5, 4, 3, 16, 0, true)},
      {21, jpeg_payload(4)},
      {11, text_payload(9)},
      {37, edit_text_payload(11)},
      {10, font_payload(13)},
      {39, sprite_payload(14, 1)},
      {34, button_payload(15)},
      {14, sound_payload(16)},
      {60, video_payload(17)},
      {57, import_payload()},
      {76, symbol_payload({{5, symbol_text("Game.Shape")}, {12, symbol_text("Game.Morph")}})},
      {56, symbol_payload({{14, symbol_text("ExportSprite")}})},
      {1, {}},
  });
}

// Constraint: the C ABI cases must not depend on every kind being present.
inline std::vector<std::byte> small_movie() {
  return movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {37, edit_text_payload(11)},
      {76, symbol_payload({{5, symbol_text("Game.Shape")}})},
      {1, {}},
  });
}

// Constraint: two definitions of one id must be refused, never merged.
inline std::vector<std::byte> duplicate_movie() {
  return movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {32, swf_art_test::define_shape_3_payload()},
      {1, {}},
  });
}

// Constraint: a class name that no definition carries is an unknown reference.
inline std::vector<std::byte> unknown_symbol_movie() {
  return movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {76, symbol_payload({{99, symbol_text("Missing.Class")}})},
      {1, {}},
  });
}

// Constraint: a character tag shorter than its leading id must be refused.
inline std::vector<std::byte> truncated_character_movie() {
  return movie_of({
      {34, {std::byte{0x01}}},
      {1, {}},
  });
}

}  // namespace swf_character_test
