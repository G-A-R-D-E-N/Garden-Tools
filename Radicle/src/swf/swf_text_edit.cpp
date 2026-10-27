#include "fo4/swf/swf_text_edit.hpp"

#include "swf_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

// Constraint: the RECT is bit-packed, so every write goes through one writer that pads the last byte.
struct BitWriter {
  std::vector<std::byte>& out;
  std::uint32_t pending{0};
  unsigned filled{0};

  void push(std::uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
      pending = (pending << 1) | ((value >> (count - 1u - index)) & 1u);
      ++filled;
      if (filled == 8) {
        out.push_back(static_cast<std::byte>(pending & 0xFFu));
        pending = 0;
        filled = 0;
      }
    }
  }

  void align() {
    if (filled != 0) {
      push(0, 8 - filled);
    }
  }
};

unsigned signed_bits(std::int32_t value) {
  const auto wide = static_cast<std::int64_t>(value);
  for (unsigned bits = 1; bits <= 31; ++bits) {
    const auto limit = static_cast<std::int64_t>(1) << (bits - 1);
    if (wide >= -limit && wide <= limit - 1) {
      return bits;
    }
  }
  return 32;
}

bool encode_rect(const SwfRect& rect, std::vector<std::byte>& out, std::string& message) {
  const unsigned needed = std::max(std::max(signed_bits(rect.x_min), signed_bits(rect.x_max)),
                                   std::max(signed_bits(rect.y_min), signed_bits(rect.y_max)));
  const unsigned bits = std::max(needed, static_cast<unsigned>(rect.nbits));
  if (bits > 31) {
    message = "the bounds do not fit a RECT field";
    return false;
  }
  BitWriter writer{out};
  writer.push(bits, 5);
  const std::uint32_t mask = (1u << bits) - 1u;
  writer.push(static_cast<std::uint32_t>(rect.x_min) & mask, bits);
  writer.push(static_cast<std::uint32_t>(rect.x_max) & mask, bits);
  writer.push(static_cast<std::uint32_t>(rect.y_min) & mask, bits);
  writer.push(static_cast<std::uint32_t>(rect.y_max) & mask, bits);
  writer.align();
  return true;
}

void write_u16(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFu));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
}

void write_u32(std::vector<std::byte>& out, std::uint32_t value) {
  write_u16(out, value & 0xFFFFu);
  write_u16(out, value >> 16);
}

// Constraint: a SWF string is NUL terminated and must not carry a NUL of its own.
bool append_zstring(std::vector<std::byte>& out, std::string_view value, std::string& message) {
  if (value.find('\0') != std::string_view::npos) {
    message = "a string field must not carry a NUL byte";
    return false;
  }
  for (const char ch : value) {
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
  }
  out.push_back(std::byte{0});
  return true;
}

// Invariant: the field order matches swf_tags.cpp:decode_edit_text, so a replay is byte-identical.
bool encode_edit_text_tail(const SwfEditText& text, std::vector<std::byte>& out, std::string& message) {
  out.clear();
  out.push_back(static_cast<std::byte>(text.flags1));
  out.push_back(static_cast<std::byte>(text.flags2));
  if ((text.flags1 & 0x01u) != 0) {
    write_u16(out, text.font_id.value_or(0));
  }
  if ((text.flags2 & 0x80u) != 0 && !append_zstring(out, text.font_class, message)) {
    return false;
  }
  if ((text.flags1 & 0x01u) != 0 || (text.flags2 & 0x80u) != 0) {
    write_u16(out, text.font_height_twips.value_or(0));
  }
  if ((text.flags1 & 0x04u) != 0) {
    const std::uint32_t color = text.color.value_or(0);
    out.push_back(static_cast<std::byte>((color >> 16) & 0xFFu));
    out.push_back(static_cast<std::byte>((color >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>(color & 0xFFu));
    out.push_back(static_cast<std::byte>((color >> 24) & 0xFFu));
  }
  if ((text.flags1 & 0x02u) != 0) {
    write_u16(out, text.max_length.value_or(0));
  }
  if ((text.flags2 & 0x20u) != 0) {
    if (!text.layout.has_value()) {
      message = "the layout flag is set but no layout was decoded";
      return false;
    }
    out.push_back(static_cast<std::byte>(text.layout->align));
    write_u16(out, text.layout->left_margin);
    write_u16(out, text.layout->right_margin);
    write_u16(out, text.layout->indent);
    write_u16(out, static_cast<std::uint16_t>(text.layout->leading));
  }
  if (!append_zstring(out, text.variable, message)) {
    return false;
  }
  if ((text.flags1 & 0x80u) != 0 && !append_zstring(out, text.text, message)) {
    return false;
  }
  return true;
}

struct LabelRow {
  std::size_t tag_index{0};
  std::uint32_t frame{0};
  std::string name;
};

struct Timeline {
  std::uint16_t id{0};
  bool sprite{false};
  std::size_t owner_tag{0};
  const std::vector<SwfTag>* tags{nullptr};
};

bool resolve_timeline(const SwfFile& file, std::uint16_t timeline_id, Timeline& out,
                      std::string& message) {
  if (timeline_id == 0) {
    out = {};
    out.tags = &file.tags;
    return true;
  }
  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    const SwfTag& tag = file.tags[index];
    if (tag.code == tag::kDefineSprite && tag.sprite_id && *tag.sprite_id == timeline_id) {
      out = {};
      out.id = timeline_id;
      out.sprite = true;
      out.owner_tag = index;
      out.tags = &tag.children;
      return true;
    }
  }
  message = "timeline " + std::to_string(timeline_id) + " is not a DefineSprite in this movie";
  return false;
}

void collect_labels(const std::vector<SwfTag>& tags, std::vector<LabelRow>& out) {
  out.clear();
  std::uint32_t frame = 0;
  for (std::size_t index = 0; index < tags.size(); ++index) {
    const SwfTag& tag = tags[index];
    if (tag.code == tag::kShowFrame) {
      ++frame;
      continue;
    }
    if (tag.code != tag::kFrameLabel) {
      continue;
    }
    std::string name;
    for (const std::byte value : tag.payload) {
      if (value == std::byte{0}) {
        break;
      }
      name.push_back(static_cast<char>(value));
    }
    out.push_back(LabelRow{index, frame, std::move(name)});
  }
}

bool name_taken(const std::vector<LabelRow>& rows, std::string_view name,
                std::optional<std::size_t> skip, std::string& message) {
  for (std::size_t index = 0; index < rows.size(); ++index) {
    if (skip.has_value() && *skip == index) {
      continue;
    }
    if (rows[index].name == name) {
      message = "a frame label with that name already exists on this timeline";
      return true;
    }
  }
  return false;
}

std::size_t tag_header_size(const SwfTag& tag) { return tag.long_header ? 6 : 2; }

void write_tag_header(std::vector<std::byte>& out, std::uint16_t code, std::size_t length,
                      bool force_long) {
  if (force_long || length >= 0x3Fu) {
    write_u16(out, (static_cast<std::uint32_t>(code & 0x3FFu) << 6) | 0x3Fu);
    write_u32(out, static_cast<std::uint32_t>(length));
    return;
  }
  write_u16(out, (static_cast<std::uint32_t>(code & 0x3FFu) << 6) |
                     static_cast<std::uint32_t>(length));
}

// Constraint: a child offset is relative to the sprite payload, so its header sits just ahead of it.
bool child_header_start(const SwfTag& sprite, const SwfTag& child, std::size_t& out,
                        std::string& message) {
  const std::size_t header = tag_header_size(child);
  if (child.offset < header || child.offset + child.length > sprite.payload.size()) {
    message = "the sprite child offsets do not fit its payload";
    return false;
  }
  out = child.offset - header;
  return true;
}

bool replace_child_payload(const SwfTag& sprite, const SwfTag& child,
                           std::span<const std::byte> payload, std::vector<std::byte>& out,
                           std::string& message) {
  std::size_t start = 0;
  if (!child_header_start(sprite, child, start, message)) {
    return false;
  }
  out.clear();
  out.insert(out.end(), sprite.payload.begin(),
             sprite.payload.begin() + static_cast<std::ptrdiff_t>(start));
  write_tag_header(out, child.code, payload.size(), child.long_header || payload.size() >= 0x3Fu);
  out.insert(out.end(), payload.begin(), payload.end());
  out.insert(out.end(),
             sprite.payload.begin() + static_cast<std::ptrdiff_t>(child.offset + child.length),
             sprite.payload.end());
  return true;
}

bool insert_child(const SwfTag& sprite, const SwfTag& before, std::span<const std::byte> payload,
                  std::vector<std::byte>& out, std::string& message) {
  std::size_t start = 0;
  if (!child_header_start(sprite, before, start, message)) {
    return false;
  }
  out.clear();
  out.insert(out.end(), sprite.payload.begin(),
             sprite.payload.begin() + static_cast<std::ptrdiff_t>(start));
  write_tag_header(out, tag::kFrameLabel, payload.size(), false);
  out.insert(out.end(), payload.begin(), payload.end());
  out.insert(out.end(), sprite.payload.begin() + static_cast<std::ptrdiff_t>(start),
             sprite.payload.end());
  return true;
}

bool remove_child(const SwfTag& sprite, const SwfTag& child, std::vector<std::byte>& out,
                  std::string& message) {
  std::size_t start = 0;
  if (!child_header_start(sprite, child, start, message)) {
    return false;
  }
  out.clear();
  out.insert(out.end(), sprite.payload.begin(),
             sprite.payload.begin() + static_cast<std::ptrdiff_t>(start));
  out.insert(out.end(),
             sprite.payload.begin() + static_cast<std::ptrdiff_t>(child.offset + child.length),
             sprite.payload.end());
  return true;
}

// Constraint: an insertion point is the ShowFrame that ends the requested frame, so frame N is before it.
bool show_frame_index(const std::vector<SwfTag>& tags, std::uint32_t frame, std::size_t& out,
                      std::string& message) {
  std::uint32_t seen = 0;
  for (std::size_t index = 0; index < tags.size(); ++index) {
    if (tags[index].code != tag::kShowFrame) {
      continue;
    }
    if (seen == frame) {
      out = index;
      return true;
    }
    ++seen;
  }
  message = "the timeline does not reach that frame";
  return false;
}

bool apply_sprite_payload(SwfFile& file, const Timeline& timeline, std::vector<std::byte> payload,
                          std::string& message) {
  SwfTagPlan plan;
  SwfTagReplacement replacement;
  replacement.tag_index = timeline.owner_tag;
  replacement.payload = std::move(payload);
  plan.replacements.push_back(std::move(replacement));
  SwfTagPlanResult result;
  if (!apply_tag_plan(file, plan, result)) {
    message = "the tag plan refused the sprite edit";
    return false;
  }
  return true;
}

}  // namespace

bool replace_edit_text(const SwfTag& tag, const SwfEditTextEdit& edit, std::vector<std::byte>& out,
                       std::string& message) {
  out.clear();
  message.clear();
  if (tag.code != tag::kDefineEditText || !tag.edit_text) {
    message = "the tag is not a decoded DefineEditText";
    return false;
  }
  SwfEditText text = *tag.edit_text;
  if (edit.text.has_value()) {
    text.text = *edit.text;
    text.flags1 = static_cast<std::uint8_t>(text.flags1 | 0x80u);
  }
  if (edit.color.has_value()) {
    text.color = *edit.color;
    text.flags1 = static_cast<std::uint8_t>(text.flags1 | 0x04u);
  }
  const bool bounds_changed = edit.bounds.has_value();
  if (bounds_changed) {
    text.bounds = *edit.bounds;
    text.bounds.nbits = 0;
  }
  std::vector<std::byte> tail;
  if (!encode_edit_text_tail(text, tail, message)) {
    return false;
  }
  if (bounds_changed) {
    write_u16(out, text.character_id);
    if (!encode_rect(text.bounds, out, message)) {
      return false;
    }
    out.insert(out.end(), tail.begin(), tail.end());
  } else {
    SwfRect bounds;
    std::size_t after_rect = 0;
    if (!detail::read_rect(tag.payload, 2, bounds, after_rect) ||
        after_rect > tag.payload.size()) {
      message = "the DefineEditText bounds did not decode";
      return false;
    }
    out.insert(out.end(), tag.payload.begin(),
               tag.payload.begin() + static_cast<std::ptrdiff_t>(after_rect));
    out.insert(out.end(), tail.begin(), tail.end());
  }
  if (out.size() > std::numeric_limits<std::uint32_t>::max()) {
    message = "the edited payload does not fit a tag length";
    return false;
  }
  return true;
}

bool apply_edit_text(SwfFile& file, std::size_t tag_index, const SwfEditTextEdit& edit,
                     std::string& message) {
  message.clear();
  if (tag_index >= file.tags.size()) {
    message = "the tag index is out of range";
    return false;
  }
  std::vector<std::byte> payload;
  if (!replace_edit_text(file.tags[tag_index], edit, payload, message)) {
    return false;
  }
  SwfTagPlan plan;
  SwfTagReplacement replacement;
  replacement.tag_index = tag_index;
  replacement.payload = std::move(payload);
  plan.replacements.push_back(std::move(replacement));
  SwfTagPlanResult result;
  if (!apply_tag_plan(file, plan, result)) {
    message = "the tag plan refused the edit-text edit";
    return false;
  }
  return true;
}

bool list_frame_labels(const SwfFile& file, std::uint16_t timeline_id,
                       std::vector<SwfFrameLabel>& out, std::string& message) {
  out.clear();
  message.clear();
  Timeline timeline;
  if (!resolve_timeline(file, timeline_id, timeline, message)) {
    return false;
  }
  std::vector<LabelRow> rows;
  collect_labels(*timeline.tags, rows);
  out.reserve(rows.size());
  for (std::size_t index = 0; index < rows.size(); ++index) {
    SwfFrameLabel label;
    label.timeline_id = timeline_id;
    label.label_index = index;
    label.frame = rows[index].frame;
    label.name = rows[index].name;
    out.push_back(std::move(label));
  }
  return true;
}

bool rename_frame_label(SwfFile& file, std::uint16_t timeline_id, std::size_t label_index,
                        std::string_view name, std::string& message) {
  message.clear();
  if (name.empty()) {
    message = "a frame label name must not be empty";
    return false;
  }
  Timeline timeline;
  if (!resolve_timeline(file, timeline_id, timeline, message)) {
    return false;
  }
  std::vector<LabelRow> rows;
  collect_labels(*timeline.tags, rows);
  if (label_index >= rows.size()) {
    message = "the frame label index is out of range";
    return false;
  }
  if (name_taken(rows, name, label_index, message)) {
    return false;
  }
  std::vector<std::byte> payload;
  if (!append_zstring(payload, name, message)) {
    return false;
  }
  const auto& source = (*timeline.tags)[rows[label_index].tag_index].payload;
  const auto source_nul = std::find(source.begin(), source.end(), std::byte{0});
  if (source_nul != source.end()) {
    payload.insert(payload.end(), source_nul + 1, source.end());
  }
  if (!timeline.sprite) {
    SwfTagPlan plan;
    SwfTagReplacement replacement;
    replacement.tag_index = rows[label_index].tag_index;
    replacement.payload = std::move(payload);
    plan.replacements.push_back(std::move(replacement));
    SwfTagPlanResult result;
    if (!apply_tag_plan(file, plan, result)) {
      message = "the tag plan refused the frame-label rename";
      return false;
    }
    return true;
  }
  const SwfTag& sprite = file.tags[timeline.owner_tag];
  std::vector<std::byte> sprite_payload;
  if (!replace_child_payload(sprite, sprite.children[rows[label_index].tag_index], payload,
                             sprite_payload, message)) {
    return false;
  }
  return apply_sprite_payload(file, timeline, std::move(sprite_payload), message);
}

bool add_frame_label(SwfFile& file, std::uint16_t timeline_id, std::uint32_t frame,
                     std::string_view name, std::string& message) {
  message.clear();
  if (name.empty()) {
    message = "a frame label name must not be empty";
    return false;
  }
  Timeline timeline;
  if (!resolve_timeline(file, timeline_id, timeline, message)) {
    return false;
  }
  std::vector<LabelRow> rows;
  collect_labels(*timeline.tags, rows);
  if (name_taken(rows, name, std::nullopt, message)) {
    return false;
  }
  std::size_t insert_at = 0;
  if (!show_frame_index(*timeline.tags, frame, insert_at, message)) {
    return false;
  }
  std::vector<std::byte> payload;
  if (!append_zstring(payload, name, message)) {
    return false;
  }
  if (!timeline.sprite) {
    SwfTagPlan plan;
    SwfTagInsertion insertion;
    insertion.before_tag_index = insert_at;
    insertion.tag.code = tag::kFrameLabel;
    insertion.tag.payload = std::move(payload);
    insertion.tag.length = static_cast<std::uint32_t>(insertion.tag.payload.size());
    plan.insertions.push_back(std::move(insertion));
    SwfTagPlanResult result;
    if (!apply_tag_plan(file, plan, result)) {
      message = "the tag plan refused the frame-label insertion";
      return false;
    }
    return true;
  }
  const SwfTag& sprite = file.tags[timeline.owner_tag];
  std::vector<std::byte> sprite_payload;
  if (!insert_child(sprite, sprite.children[insert_at], payload, sprite_payload, message)) {
    return false;
  }
  return apply_sprite_payload(file, timeline, std::move(sprite_payload), message);
}

bool remove_frame_label(SwfFile& file, std::uint16_t timeline_id, std::size_t label_index,
                        std::string& message) {
  message.clear();
  Timeline timeline;
  if (!resolve_timeline(file, timeline_id, timeline, message)) {
    return false;
  }
  std::vector<LabelRow> rows;
  collect_labels(*timeline.tags, rows);
  if (label_index >= rows.size()) {
    message = "the frame label index is out of range";
    return false;
  }
  if (!timeline.sprite) {
    SwfTagPlan plan;
    SwfTagRemoval removal;
    removal.tag_index = rows[label_index].tag_index;
    plan.removals.push_back(removal);
    SwfTagPlanResult result;
    if (!apply_tag_plan(file, plan, result)) {
      message = "the tag plan refused the frame-label removal";
      return false;
    }
    return true;
  }
  const SwfTag& sprite = file.tags[timeline.owner_tag];
  std::vector<std::byte> sprite_payload;
  if (!remove_child(sprite, sprite.children[rows[label_index].tag_index], sprite_payload,
                    message)) {
    return false;
  }
  return apply_sprite_payload(file, timeline, std::move(sprite_payload), message);
}

}  // namespace fo4::swf
