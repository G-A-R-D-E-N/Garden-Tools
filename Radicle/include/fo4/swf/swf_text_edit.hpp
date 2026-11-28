#pragma once

#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::swf {

// Constraint: an unset field must leave the payload byte-identical, so an edit is a splice, not a rewrite.
struct SwfEditTextEdit {
  std::optional<std::string> text;
  std::optional<std::uint32_t> color;
  std::optional<SwfRect> bounds;
};

// Requires a tag 37 payload; the re-encode must reuse the original RECT bytes unless the bounds are edited.
bool replace_edit_text(const SwfTag& tag, const SwfEditTextEdit& edit, std::vector<std::byte>& out,
                       std::string& message);

// Requires a decoded tag 37 at the index; a refusal must leave the file's bytes untouched.
bool apply_edit_text(SwfFile& file, std::size_t tag_index, const SwfEditTextEdit& edit,
                     std::string& message);

// Constraint: timeline_id 0 is the root timeline; any other value is the DefineSprite character that owns it.
struct SwfFrameLabel {
  std::uint16_t timeline_id{0};
  std::size_t label_index{0};
  std::uint32_t frame{0};
  std::string name;
};

// Requires a decoded movie; labels are reported in tag order with the frame they sit on.
bool list_frame_labels(const SwfFile& file, std::uint16_t timeline_id,
                       std::vector<SwfFrameLabel>& out, std::string& message);

// Requires the label index on that timeline; a duplicate name on the same timeline is a refusal.
bool rename_frame_label(SwfFile& file, std::uint16_t timeline_id, std::size_t label_index,
                        std::string_view name, std::string& message);

// Requires a frame the timeline reaches; the label is inserted ahead of that frame's ShowFrame.
bool add_frame_label(SwfFile& file, std::uint16_t timeline_id, std::uint32_t frame,
                     std::string_view name, std::string& message);

// Requires the label index on that timeline; the tag is removed through the plan.
bool remove_frame_label(SwfFile& file, std::uint16_t timeline_id, std::size_t label_index,
                        std::string& message);

}  // namespace fo4::swf
