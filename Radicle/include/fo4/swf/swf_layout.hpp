#pragma once

#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_characters.hpp"
#include "fo4/swf/swf_timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::swf {

inline constexpr std::uint8_t kLayoutScripted = 1;
inline constexpr std::uint8_t kLayoutDuplicatePath = 2;
inline constexpr std::uint8_t kLayoutHasAs3 = 4;
inline constexpr std::uint8_t kLayoutLegacyMoves = 8;

struct SwfLayoutText {
  std::optional<std::uint16_t> font_id;
  // Constraint: set from the DefineEditText HasFontClass flag, so a field that names its font by class has no font_id.
  bool has_font_class{false};
  std::string font_class;
  std::optional<double> font_height;
  std::optional<std::uint32_t> color;
  bool has_layout{false};
  std::uint8_t align{0};
  double left_margin{0.0};
  double right_margin{0.0};
  double indent{0.0};
  double leading{0.0};
  bool multiline{false};
  bool word_wrap{false};
  bool auto_size{false};
  bool html{false};
  bool border{false};
  std::string variable;
  std::string text;
};

// Constraint: every field is only what the defining tag holds; an absent fact is left unset, never defaulted to false or zero.
struct SwfLayoutFont {
  std::uint16_t font_id{0};
  std::uint8_t version{0};
  std::string name;
  bool has_style{false};
  bool bold{false};
  bool italic{false};
  bool has_language{false};
  std::uint8_t language{0};
  bool has_glyph_count{false};
  std::uint32_t glyph_count{0};
  bool has_outlines{false};
  bool has_metrics{false};
  std::uint16_t ascent{0};
  std::uint16_t descent{0};
  std::int16_t leading{0};
  std::uint32_t units_per_em{0};
  // Constraint: codes[i] is the character code of glyph index i; has_codes is false when the movie holds no table for this font.
  bool has_codes{false};
  std::vector<std::uint16_t> codes;
  bool has_font_name{false};
  std::string font_name;
  std::string font_copyright;
  bool has_font_class{false};
  std::string font_class;
};

struct SwfLayoutRow {
  std::string path;
  // Constraint: an unnamed enclosing clip appears in parent_path as #<character id> and has no row of its own.
  std::string parent_path;
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  SwfCharacterKind kind{SwfCharacterKind::shape};
  bool visible{true};
  bool has_rect{false};
  SwfBounds rect{};
  SwfTransform world_transform{};
  std::optional<std::uint16_t> clip_depth;
  std::uint32_t clip_frame_count{0};
  // Constraint: the inclusive span of the clip's own timeline, read only when clip_frame_count is nonzero; never the frame the clip rests on.
  std::uint32_t clip_first_frame{0};
  std::uint32_t clip_last_frame{0};
  std::uint8_t flags{0};
  std::optional<SwfLayoutText> text;
};

struct SwfLayoutState {
  std::string label;
  std::uint16_t timeline_id{0};
  // Constraint: a frame at or past the last ShowFrame gives a state with zero rows and raises no flag.
  std::uint32_t frame{0};
  std::size_t first_row{0};
  std::size_t row_count{0};
};

struct SwfLayout {
  SwfBounds stage{};
  double frame_rate{0.0};
  // Constraint: scripted is decided per movie, so every row of a scripted movie carries kLayoutScripted.
  bool scripted{false};
  // Constraint: scripted is has_as3 or legacy_moves; every row carries kLayoutHasAs3 and kLayoutLegacyMoves the same way.
  bool has_as3{false};
  bool legacy_moves{false};
  // Invariant: sorted by font_id; a text row's font_id with no entry names a font the movie does not define.
  std::vector<SwfLayoutFont> fonts;
  std::vector<SwfLayoutState> states;
  std::vector<SwfLayoutRow> rows;
};

// Requires a movie whose display list builds and whose character model is complete; on refusal out is left as it was and message names the cause.
bool build_layout(const SwfFile& file, SwfLayout& out, std::string& message);

}  // namespace fo4::swf
