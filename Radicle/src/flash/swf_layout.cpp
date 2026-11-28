#include "fo4/swf/swf_layout.hpp"

#include "fo4/avm1/avm1.hpp"
#include "fo4/constants.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_text_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

constexpr std::int32_t kTwipsPerPixelWhole = 20;
constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
constexpr std::uint8_t kActionEnd = 0x00;
constexpr std::uint8_t kActionPlay = 0x06;
constexpr std::uint8_t kActionStop = 0x07;
constexpr std::uint8_t kActionPush = 0x96;
constexpr std::uint8_t kActionCall = 0x9E;

// Constraint: only actions that cannot jump or write a property keep the timeline linear; anything else may move a row.
bool keeps_timeline(const avm1::Avm1Action& action) {
  return !action.unknown && (action.code == kActionEnd || action.code == kActionPush ||
                             action.code == kActionStop || action.code == kActionPlay ||
                             action.code == kActionCall);
}

bool action_tag_moves(const SwfTag& tag) {
  if (!tag.action) {
    return true;
  }
  const auto bytes =
      std::span<const std::byte>(tag.payload).subspan(tag.action->offset, tag.action->size);
  const avm1::Avm1Stream stream = avm1::decode_actions(bytes);
  return !stream.ok() ||
         !std::all_of(stream.actions.begin(), stream.actions.end(), keeps_timeline);
}

// Constraint: DefineButton2 carries its ActionOffset at payload bytes 3 and 4; zero means no condition actions.
bool button_has_actions(const SwfTag& tag) {
  return tag.payload.size() >= 5 &&
         (std::to_integer<unsigned>(tag.payload[3]) |
          (std::to_integer<unsigned>(tag.payload[4]) << 8)) != 0;
}

// Constraint: a DefineButton whose records did not decode cannot be checked, so it counts as moving.
bool plain_button_moves(const SwfTag& tag) {
  if (!tag.button || !tag.button->action_list_offset) {
    return true;
  }
  const auto bytes =
      std::span<const std::byte>(tag.payload).subspan(*tag.button->action_list_offset);
  const avm1::Avm1Stream stream = avm1::decode_actions(bytes);
  return !stream.ok() ||
         !std::all_of(stream.actions.begin(), stream.actions.end(), keeps_timeline);
}

struct ScriptKinds {
  bool as3{false};
  bool legacy{false};
};

void collect_script(const std::vector<SwfTag>& tags, ScriptKinds& kinds) {
  for (const SwfTag& tag : tags) {
    kinds.as3 = kinds.as3 || tag.code == tag::kDoAbc || tag.code == tag::kDoAbc2;
    kinds.legacy =
        kinds.legacy ||
        ((tag.code == tag::kDoAction || tag.code == tag::kDoInitAction) && action_tag_moves(tag)) ||
        (tag.code == tag::kDefineButton && plain_button_moves(tag)) ||
        (tag.code == tag::kDefineButton2 && button_has_actions(tag)) ||
        (tag.placement && tag.placement->has_clip_actions);
    collect_script(tag.children, kinds);
  }
}

void extend_bounds(SwfBounds& total, bool& any, const SwfBounds& box) {
  if (!any) {
    total = box;
    any = true;
    return;
  }
  const double x0 = std::min(total.x, box.x);
  const double y0 = std::min(total.y, box.y);
  const double x1 = std::max(total.x + total.width, box.x + box.width);
  const double y1 = std::max(total.y + total.height, box.y + box.height);
  total = SwfBounds{x0, y0, x1 - x0, y1 - y0};
}

bool drawn_rect(const SwfCharacter& character, const SwfTransform& world, SwfBounds& out) {
  if (character.kind == SwfCharacterKind::bitmap && character.has_bitmap_size) {
    SwfRect pixels;
    pixels.x_max = static_cast<std::int32_t>(character.bitmap_width) * kTwipsPerPixelWhole;
    pixels.y_max = static_cast<std::int32_t>(character.bitmap_height) * kTwipsPerPixelWhole;
    out = transformed_bounds(world, pixels);
    return true;
  }
  if (!character.has_bounds || character.kind == SwfCharacterKind::morph) {
    return false;
  }
  out = transformed_bounds(world, character.bounds);
  return true;
}

bool button_rect(const SwfFile& file, const SwfCharacterModel& model,
                 const SwfCharacter& character, const SwfTransform& world, SwfBounds& out) {
  const SwfTag& tag = file.tags[character.tag_index];
  if (!tag.button) {
    return false;
  }
  bool any = false;
  for (const SwfButtonRecord& record : tag.button->records) {
    const SwfCharacter* part = record.up ? find_character(model, record.character_id) : nullptr;
    SwfBounds box;
    if (part != nullptr &&
        drawn_rect(*part, compose_transform(world, transform_from_matrix(record.matrix)), box)) {
      extend_bounds(out, any, box);
    }
  }
  return any;
}

SwfLayoutText text_of(const SwfEditText& edit) {
  SwfLayoutText out;
  out.font_id = edit.font_id;
  if ((edit.flags2 & 0x80u) != 0) {
    out.has_font_class = true;
    out.font_class = edit.font_class;
  }
  if (edit.font_height_twips) {
    out.font_height = static_cast<double>(*edit.font_height_twips) / k_twips_per_pixel;
  }
  out.color = edit.color;
  if (edit.layout) {
    out.has_layout = true;
    out.align = edit.layout->align;
    out.left_margin = static_cast<double>(edit.layout->left_margin) / k_twips_per_pixel;
    out.right_margin = static_cast<double>(edit.layout->right_margin) / k_twips_per_pixel;
    out.indent = static_cast<double>(edit.layout->indent) / k_twips_per_pixel;
    out.leading = static_cast<double>(edit.layout->leading) / k_twips_per_pixel;
  }
  out.multiline = (edit.flags1 & 0x20u) != 0;
  out.word_wrap = (edit.flags1 & 0x40u) != 0;
  out.auto_size = (edit.flags2 & 0x40u) != 0;
  out.html = (edit.flags2 & 0x02u) != 0;
  out.border = (edit.flags2 & 0x08u) != 0;
  out.variable = edit.variable;
  out.text = edit.text;
  return out;
}

constexpr std::uint8_t kFont4HasData = 0x04;
constexpr std::uint8_t kFontBold = 0x01;
constexpr std::uint8_t kFontItalic = 0x02;
constexpr std::size_t kFont4NameOffset = 3;

// Constraint: DefineFont4 has no typed decode, so its UI16 id, flag byte and null-terminated name are read from the raw payload.
bool font4_of(const SwfTag& tag, SwfLayoutFont& out) {
  const std::vector<std::byte>& bytes = tag.payload;
  if (bytes.size() < kFont4NameOffset) {
    return false;
  }
  const auto flags = std::to_integer<std::uint8_t>(bytes[2]);
  std::size_t end = kFont4NameOffset;
  while (end < bytes.size() && bytes[end] != std::byte{0}) {
    ++end;
  }
  if (end == bytes.size()) {
    return false;
  }
  out.version = 4;
  out.name.assign(reinterpret_cast<const char*>(bytes.data()) + kFont4NameOffset,
                  end - kFont4NameOffset);
  out.has_style = true;
  out.bold = (flags & kFontBold) != 0;
  out.italic = (flags & kFontItalic) != 0;
  out.has_outlines = (flags & kFont4HasData) != 0;
  return true;
}

void font_of(const SwfFont& font, std::uint16_t code, SwfLayoutFont& out) {
  out.version = code == tag::kDefineFont ? 1 : (code == tag::kDefineFont2 ? 2 : 3);
  out.has_glyph_count = true;
  out.glyph_count = font.glyph_count;
  out.has_outlines = font.glyph_count != 0 && font.glyphs.size() == font.glyph_count;
  if (out.version == 1) {
    return;
  }
  out.has_codes = true;
  out.codes = font.glyph_codes;
  out.name = font.name;
  out.has_style = true;
  out.bold = (font.flags & kFontBold) != 0;
  out.italic = (font.flags & kFontItalic) != 0;
  out.has_language = true;
  out.language = font.language;
  if (font.has_layout) {
    out.has_metrics = true;
    out.ascent = font.ascent;
    out.descent = font.descent;
    out.leading = font.leading;
    out.units_per_em = font.units_per_em;
  }
}

constexpr std::size_t kFontInfoHeader = 3;
constexpr std::uint8_t kFontInfoWide = 0x01;
constexpr std::uint8_t kFontInfoBold = 0x02;
constexpr std::uint8_t kFontInfoItalic = 0x04;

struct FontInfo {
  std::string name;
  std::uint8_t flags{0};
  bool has_language{false};
  std::uint8_t language{0};
  std::vector<std::uint16_t> codes;
};

struct FontExtras {
  std::optional<FontInfo> info;
  std::optional<std::pair<std::string, std::string>> name_tag;
  std::optional<std::string> font_class;
};

std::uint16_t u16_at(const std::vector<std::byte>& bytes, std::size_t at) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[at]) |
                                    (std::to_integer<unsigned>(bytes[at + 1]) << 8));
}

// Constraint: DefineFontInfo and DefineFontInfo2 are read from the raw payload so a bad tag only refuses the layout; tag 62 adds a LanguageCode byte after the flag byte; the flag byte is reserved(2), small, ShiftJIS, ANSI, italic, bold, wide from the high bit.
bool font_info_of(const SwfTag& tag, std::uint16_t& font_id, FontInfo& info) {
  const std::vector<std::byte>& bytes = tag.payload;
  if (bytes.size() < kFontInfoHeader) {
    return false;
  }
  font_id = u16_at(bytes, 0);
  std::size_t at = kFontInfoHeader + std::to_integer<std::size_t>(bytes[2]);
  if (at >= bytes.size()) {
    return false;
  }
  info.name.assign(reinterpret_cast<const char*>(bytes.data()) + kFontInfoHeader,
                   at - kFontInfoHeader);
  info.flags = std::to_integer<std::uint8_t>(bytes[at]);
  const bool wide = (info.flags & kFontInfoWide) != 0;
  at += tag.code == tag::kDefineFontInfo2 ? 2 : 1;
  if (at > bytes.size()) {
    return false;
  }
  if (tag.code == tag::kDefineFontInfo2) {
    info.has_language = true;
    info.language = std::to_integer<std::uint8_t>(bytes[at - 1]);
  }
  const std::size_t left = bytes.size() - at;
  if (wide && (left % 2) != 0) {
    return false;
  }
  info.codes.resize(wide ? left / 2 : left);
  for (std::size_t index = 0; index < info.codes.size(); ++index) {
    info.codes[index] = wide ? u16_at(bytes, at + index * 2)
                             : std::to_integer<std::uint16_t>(bytes[at + index]);
  }
  return true;
}

// Constraint: DefineFontName is a UI16 id then two null-terminated strings, name and copyright; both terminators must be present.
bool font_name_of(const SwfTag& tag, std::uint16_t& font_id,
                  std::pair<std::string, std::string>& out) {
  const std::vector<std::byte>& bytes = tag.payload;
  if (bytes.size() < 2) {
    return false;
  }
  font_id = u16_at(bytes, 0);
  std::size_t at = 2;
  for (std::string* part : {&out.first, &out.second}) {
    std::size_t end = at;
    while (end < bytes.size() && bytes[end] != std::byte{0}) {
      ++end;
    }
    if (end == bytes.size()) {
      return false;
    }
    part->assign(reinterpret_cast<const char*>(bytes.data()) + at, end - at);
    at = end + 1;
  }
  return true;
}

bool collect_font_extras(const std::vector<SwfTag>& tags,
                         std::map<std::uint16_t, FontExtras>& extras, std::string& message) {
  for (const SwfTag& tag : tags) {
    std::uint16_t id = 0;
    if (tag.code == tag::kDefineFontInfo || tag.code == tag::kDefineFontInfo2) {
      FontInfo info;
      if (!font_info_of(tag, id, info)) {
        message = "font info tag (code " + std::to_string(tag.code) + ") did not decode";
        return false;
      }
      extras[id].info = std::move(info);
    } else if (tag.code == tag::kDefineFontName) {
      std::pair<std::string, std::string> names;
      if (!font_name_of(tag, id, names)) {
        message = "font name tag did not decode";
        return false;
      }
      extras[id].name_tag = std::move(names);
    }
    if (tag.symbols) {
      for (const SwfSymbol& symbol : *tag.symbols) {
        std::optional<std::string>& slot = extras[symbol.tag_id].font_class;
        if (!slot) {
          slot = symbol.name;
        }
      }
    }
    if (!collect_font_extras(tag.children, extras, message)) {
      return false;
    }
  }
  return true;
}

// Invariant: a DefineFontInfo tag is only taken for a version 1 font, and its table only when it holds one code per glyph; versions 2 and 3 own their name, style, language and table and version 4 has none.
bool apply_extras(const FontExtras& extras, SwfLayoutFont& font, std::string& message) {
  if (extras.info && font.version == 1) {
    const FontInfo& info = *extras.info;
    if (info.codes.size() != font.glyph_count) {
      message = "font " + std::to_string(font.font_id) + " code table does not match its glyph count";
      return false;
    }
    font.has_codes = true;
    font.codes = info.codes;
    font.name = info.name;
    font.has_style = true;
    font.bold = (info.flags & kFontInfoBold) != 0;
    font.italic = (info.flags & kFontInfoItalic) != 0;
    font.has_language = info.has_language;
    font.language = info.language;
  }
  if (extras.name_tag) {
    font.has_font_name = true;
    font.font_name = extras.name_tag->first;
    font.font_copyright = extras.name_tag->second;
  }
  if (extras.font_class) {
    font.has_font_class = true;
    font.font_class = *extras.font_class;
  }
  return true;
}

bool collect_fonts(const SwfFile& file, const SwfCharacterModel& model,
                   std::vector<SwfLayoutFont>& fonts, std::string& message) {
  std::map<std::uint16_t, FontExtras> extras;
  if (!collect_font_extras(file.tags, extras, message)) {
    return false;
  }
  for (const SwfCharacter& character : model.characters) {
    if (character.kind != SwfCharacterKind::font) {
      continue;
    }
    const SwfTag& tag = file.tags[character.tag_index];
    SwfLayoutFont font;
    font.font_id = character.character_id;
    if (tag.code == tag::kDefineFont4) {
      if (!font4_of(tag, font)) {
        message = "font tag " + std::to_string(character.tag_index) + " did not decode";
        return false;
      }
    } else if (tag.font) {
      font_of(*tag.font, tag.code, font);
    } else {
      message = "font tag " + std::to_string(character.tag_index) + " did not decode";
      return false;
    }
    const auto found = extras.find(font.font_id);
    if (found != extras.end() && !apply_extras(found->second, font, message)) {
      return false;
    }
    fonts.push_back(std::move(font));
  }
  return true;
}

struct FrameTree {
  std::vector<std::size_t> parent;
  std::vector<std::size_t> end;
};

// Invariant: instances are in pre-order, so an instance's descendants are the run that follows it at a deeper nesting level.
FrameTree frame_tree(const std::vector<SwfFrameInstance>& instances) {
  FrameTree tree;
  tree.parent.assign(instances.size(), kNoIndex);
  tree.end.assign(instances.size(), instances.size());
  std::vector<std::size_t> last_at;
  for (std::size_t index = 0; index < instances.size(); ++index) {
    const std::size_t level = instances[index].nesting_level;
    if (last_at.size() <= level) {
      last_at.resize(level + 1, kNoIndex);
    }
    if (level > 0) {
      tree.parent[index] = last_at[level - 1];
    }
    last_at[level] = index;
  }
  for (std::size_t index = 0; index < instances.size(); ++index) {
    std::size_t next = index + 1;
    while (next < instances.size() &&
           instances[next].nesting_level > instances[index].nesting_level) {
      ++next;
    }
    tree.end[index] = next;
  }
  return tree;
}

bool append_rows(const SwfFile& file, const SwfCharacterModel& model,
                 const std::map<std::uint16_t, std::uint32_t>& clip_frames,
                 const std::vector<SwfFrameInstance>& instances, std::uint8_t script_flags,
                 std::vector<SwfLayoutRow>& rows, std::string& message) {
  const FrameTree tree = frame_tree(instances);
  std::vector<const SwfCharacter*> characters(instances.size(), nullptr);
  std::vector<std::optional<SwfBounds>> rects(instances.size());
  for (std::size_t index = 0; index < instances.size(); ++index) {
    const SwfFrameInstance& instance = instances[index];
    const SwfCharacter* character = find_character(model, instance.character_id);
    if (character == nullptr) {
      message = "placed character " + std::to_string(instance.character_id) + " is not defined";
      return false;
    }
    characters[index] = character;
    if (character->kind == SwfCharacterKind::sprite) {
      continue;
    }
    SwfBounds box;
    const bool drawn = character->kind == SwfCharacterKind::button
                           ? button_rect(file, model, *character, instance.world_transform, box)
                           : drawn_rect(*character, instance.world_transform, box);
    if (drawn) {
      rects[index] = box;
    }
  }
  for (std::size_t index = 0; index < instances.size(); ++index) {
    if (characters[index]->kind != SwfCharacterKind::sprite) {
      continue;
    }
    SwfBounds total;
    bool any = false;
    for (std::size_t inner = index + 1; inner < tree.end[index]; ++inner) {
      if (rects[inner] && instances[inner].world_visible) {
        extend_bounds(total, any, *rects[inner]);
      }
    }
    if (any) {
      rects[index] = total;
    }
  }
  std::map<std::string, std::size_t> named;
  for (const SwfFrameInstance& instance : instances) {
    if (!instance.name.empty()) {
      ++named[instance.path];
    }
  }
  for (std::size_t index = 0; index < instances.size(); ++index) {
    const SwfFrameInstance& instance = instances[index];
    if (instance.name.empty()) {
      continue;
    }
    const SwfCharacter& character = *characters[index];
    SwfLayoutRow row;
    row.path = instance.path;
    if (tree.parent[index] != kNoIndex) {
      row.parent_path = instances[tree.parent[index]].path;
    }
    row.depth = instance.depth;
    row.character_id = instance.character_id;
    row.kind = character.kind;
    row.visible = instance.world_visible;
    row.world_transform = instance.world_transform;
    if (rects[index]) {
      row.has_rect = true;
      row.rect = *rects[index];
    }
    row.clip_depth = instance.clip_depth;
    if (character.kind == SwfCharacterKind::sprite) {
      const auto frames = clip_frames.find(instance.character_id);
      if (frames != clip_frames.end() && frames->second != 0) {
        row.clip_frame_count = frames->second;
        row.clip_last_frame = frames->second - 1;
      }
    }
    row.flags = static_cast<std::uint8_t>(row.flags | script_flags);
    if (named[row.path] > 1) {
      row.flags = static_cast<std::uint8_t>(row.flags | kLayoutDuplicatePath);
    }
    if (character.kind == SwfCharacterKind::edit_text) {
      const SwfTag& tag = file.tags[character.tag_index];
      if (tag.edit_text) {
        row.text = text_of(*tag.edit_text);
      }
    }
    rows.push_back(std::move(row));
  }
  return true;
}

}  // namespace

bool build_layout(const SwfFile& file, SwfLayout& out, std::string& message) {
  message.clear();
  SwfCharacterModel model;
  if (!build_character_model(file, model, message)) {
    return false;
  }
  const SwfDisplayList display = build_display_list(file);
  if (display.status != SwfTimelineStatus::ok) {
    message = display.message.empty() ? "the display list did not build" : display.message;
    return false;
  }
  SwfLayout built;
  built.stage.x = static_cast<double>(file.header.frame_size.x_min) / k_twips_per_pixel;
  built.stage.y = static_cast<double>(file.header.frame_size.y_min) / k_twips_per_pixel;
  built.stage.width =
      static_cast<double>(file.header.frame_size.x_max - file.header.frame_size.x_min) / k_twips_per_pixel;
  built.stage.height =
      static_cast<double>(file.header.frame_size.y_max - file.header.frame_size.y_min) / k_twips_per_pixel;
  built.frame_rate = file.header.frame_rate_fps();
  if (!collect_fonts(file, model, built.fonts, message)) {
    return false;
  }
  ScriptKinds kinds;
  collect_script(file.tags, kinds);
  built.has_as3 = kinds.as3;
  built.legacy_moves = kinds.legacy;
  built.scripted = kinds.as3 || kinds.legacy;
  const std::uint8_t script_flags = static_cast<std::uint8_t>(
      (built.scripted ? kLayoutScripted : 0) | (kinds.as3 ? kLayoutHasAs3 : 0) |
      (kinds.legacy ? kLayoutLegacyMoves : 0));
  std::map<std::uint16_t, std::uint32_t> clip_frames;
  for (const SwfDisplayTimeline& timeline : display.timelines) {
    clip_frames[timeline.character_id] = static_cast<std::uint32_t>(timeline.frames.size());
  }
  for (const SwfDisplayTimeline& timeline : display.timelines) {
    std::vector<SwfFrameLabel> labels;
    if (!list_frame_labels(file, timeline.character_id, labels, message)) {
      return false;
    }
    std::vector<std::pair<std::string, std::uint32_t>> states;
    for (const SwfFrameLabel& label : labels) {
      states.emplace_back(label.name, label.frame);
    }
    if (timeline.character_id == 0 && states.empty()) {
      states.emplace_back(std::string{}, 0u);
    }
    for (const auto& [label, frame] : states) {
      SwfLayoutState state;
      state.label = label;
      state.timeline_id = timeline.character_id;
      state.frame = frame;
      state.first_row = built.rows.size();
      if (frame < timeline.frames.size() &&
          !append_rows(file, model, clip_frames, timeline.frames[frame].instances, script_flags,
                       built.rows, message)) {
        return false;
      }
      state.row_count = built.rows.size() - state.first_row;
      built.states.push_back(std::move(state));
    }
  }
  out = std::move(built);
  return true;
}

}  // namespace fo4::swf
