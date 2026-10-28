#include "fo4/c_api_swf_verbs.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_edit.hpp"
#include "fo4/swf/swf_history.hpp"
#include "fo4/swf/swf_text_edit.hpp"
#include "fo4/swf/swf_timeline.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::copy_strings;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

using fo4::swf::SwfFile;
using fo4::swf::SwfTag;
using fo4::swf::SwfTagPlan;

bool same_tag(const SwfTag& left, const SwfTag& right) {
  return left.code == right.code && left.payload == right.payload;
}

// Invariant: the plan maps before onto after by top-level position and keeps every matching tag untouched.
SwfTagPlan plan_between(const SwfFile& before, const SwfFile& after) {
  const auto& from = before.tags;
  const auto& to = after.tags;
  std::size_t prefix = 0;
  while (prefix < from.size() && prefix < to.size() && same_tag(from[prefix], to[prefix])) {
    ++prefix;
  }
  std::size_t suffix = 0;
  while (suffix < from.size() - prefix && suffix < to.size() - prefix &&
         same_tag(from[from.size() - 1 - suffix], to[to.size() - 1 - suffix])) {
    ++suffix;
  }
  const std::size_t from_end = from.size() - suffix;
  const std::size_t to_end = to.size() - suffix;
  SwfTagPlan plan;
  const auto insert = [&](std::size_t before_index, const SwfTag& source) {
    SwfTag inserted;
    inserted.code = source.code;
    inserted.payload = source.payload;
    plan.insertions.push_back(fo4::swf::SwfTagInsertion{before_index, std::move(inserted)});
  };
  std::size_t old_index = prefix;
  std::size_t new_index = prefix;
  while (old_index < from_end || new_index < to_end) {
    if (old_index == from_end) {
      insert(old_index, to[new_index]);
      ++new_index;
    } else if (new_index == to_end) {
      plan.removals.push_back(fo4::swf::SwfTagRemoval{old_index});
      ++old_index;
    } else if (same_tag(from[old_index], to[new_index])) {
      ++old_index;
      ++new_index;
    } else if (old_index + 1 < from_end && same_tag(from[old_index + 1], to[new_index])) {
      plan.removals.push_back(fo4::swf::SwfTagRemoval{old_index});
      ++old_index;
    } else if (new_index + 1 < to_end && same_tag(from[old_index], to[new_index + 1])) {
      insert(old_index, to[new_index]);
      ++new_index;
    } else if (from[old_index].code == to[new_index].code) {
      plan.replacements.push_back(fo4::swf::SwfTagReplacement{old_index, to[new_index].payload});
      ++old_index;
      ++new_index;
    } else {
      plan.removals.push_back(fo4::swf::SwfTagRemoval{old_index});
      insert(old_index, to[new_index]);
      ++old_index;
      ++new_index;
    }
  }
  return plan;
}

bool plan_reproduces(const SwfFile& current, const SwfTagPlan& plan, const SwfFile& candidate) {
  SwfFile probe = current;
  fo4::swf::SwfTagPlanResult probe_result;
  if (!fo4::swf::apply_tag_plan(probe, plan, probe_result) || probe.tags.size() != candidate.tags.size()) {
    return false;
  }
  for (std::size_t index = 0; index < candidate.tags.size(); ++index) {
    if (!same_tag(probe.tags[index], candidate.tags[index])) return false;
  }
  return true;
}

// Invariant: a refused edit moves neither the file, the revision nor the undo stack.
template <typename Edit>
radicle_status commit_edit(radicle_swf_history& history, radicle_swf_plan_result& out, Edit&& edit) {
  SwfFile candidate = history.history.file();
  std::string message;
  if (!edit(candidate, message)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
  }
  const SwfTagPlan plan = plan_between(history.history.file(), candidate);
  if (!plan_reproduces(history.history.file(), plan, candidate)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "the tag plan does not reproduce the edited movie, so nothing was changed");
  }
  const std::uint64_t before = history.history.revision();
  if (!history.history.apply(plan, message)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
  }
  out.changed = history.history.revision() != before ? 1 : 0;
  out.revision = history.history.revision();
  return RADICLE_STATUS_OK;
}

fo4::swf::SwfTransform to_transform(const radicle_swf_transform& value) {
  fo4::swf::SwfTransform out;
  out.scale_x = value.scale_x;
  out.scale_y = value.scale_y;
  out.rotate_skew0 = value.rotate_skew0;
  out.rotate_skew1 = value.rotate_skew1;
  out.translate_x = value.translate_x;
  out.translate_y = value.translate_y;
  return out;
}

fo4::swf::SwfRect from_rect(const radicle_swf_rect& value) {
  fo4::swf::SwfRect out;
  out.x_min = value.x_min;
  out.x_max = value.x_max;
  out.y_min = value.y_min;
  out.y_max = value.y_max;
  return out;
}

radicle_swf_rect to_rect(const fo4::swf::SwfRect& value) {
  return radicle_swf_rect{value.x_min, value.x_max, value.y_min, value.y_max};
}

std::string text_bytes(const radicle_swf_edit_text_fields& fields) {
  if (fields.text_size == 0) return {};
  return std::string(reinterpret_cast<const char*>(fields.text), fields.text_size);
}

// Invariant: the buffer carries a trailing NUL that *out_size does not count.
std::uint8_t* copy_text(const std::string& text, std::size_t* out_size) {
  auto* buffer = static_cast<std::uint8_t*>(std::malloc(text.size() + 1));
  if (buffer == nullptr) return nullptr;
  if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = 0;
  *out_size = text.size();
  return buffer;
}

bool timeline_tags(const SwfFile& file, std::uint16_t timeline_id, const std::vector<SwfTag>*& out,
                   std::string& message) {
  if (timeline_id == 0) {
    out = &file.tags;
    return true;
  }
  for (const auto& tag : file.tags) {
    if (tag.code == fo4::swf::tag::kDefineSprite && tag.sprite_id && *tag.sprite_id == timeline_id) {
      out = &tag.children;
      return true;
    }
  }
  message = "timeline " + std::to_string(timeline_id) + " is not a DefineSprite in this movie";
  return false;
}

void comma(std::string& out) {
  if (out.back() != '[' && out.back() != '{') out += ',';
}

void member(std::string& out, const char* key) {
  comma(out);
  out += '"';
  out += key;
  out += "\":";
}

template <typename T>
void value(std::string& out, T number) {
  if constexpr (std::is_same_v<T, bool>) {
    out += number ? "true" : "false";
  } else if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(number)) {
      out += "null";
      return;
    }
    char buffer[32];
    const auto written = std::to_chars(buffer, buffer + sizeof buffer, static_cast<double>(number),
                                       std::chars_format::general, 17);
    out.append(buffer, written.ptr);
  } else {
    out += std::to_string(number);
  }
}

template <typename T>
void field(std::string& out, const char* key, T number) {
  member(out, key);
  value(out, number);
}

template <typename Range>
void array_field(std::string& out, const char* key, const Range& values) {
  member(out, key);
  out += '[';
  for (const auto& item : values) {
    comma(out);
    value(out, item);
  }
  out += ']';
}

void append_transform(std::string& out, const fo4::swf::SwfTransform& transform) {
  out += '{';
  field(out, "scale_x", transform.scale_x);
  field(out, "scale_y", transform.scale_y);
  field(out, "rotate_skew0", transform.rotate_skew0);
  field(out, "rotate_skew1", transform.rotate_skew1);
  field(out, "translate_x", transform.translate_x);
  field(out, "translate_y", transform.translate_y);
  out += '}';
}

void append_filter(std::string& out, const fo4::swf::SwfFilter& filter) {
  out += '{';
  field(out, "id", filter.id);
  field(out, "color", filter.color);
  field(out, "highlight_color", filter.highlight_color);
  array_field(out, "gradient_colors", filter.gradient_colors);
  array_field(out, "gradient_ratios", filter.gradient_ratios);
  field(out, "blur_x", filter.blur_x);
  field(out, "blur_y", filter.blur_y);
  field(out, "angle", filter.angle);
  field(out, "distance", filter.distance);
  field(out, "strength", filter.strength);
  field(out, "inner", filter.inner);
  field(out, "knockout", filter.knockout);
  field(out, "composite_source", filter.composite_source);
  field(out, "on_top", filter.on_top);
  field(out, "passes", filter.passes);
  field(out, "matrix_columns", filter.matrix_columns);
  field(out, "matrix_rows", filter.matrix_rows);
  field(out, "divisor", filter.divisor);
  field(out, "bias", filter.bias);
  array_field(out, "matrix", filter.matrix);
  field(out, "default_color", filter.default_color);
  field(out, "clamp", filter.clamp);
  field(out, "preserve_alpha", filter.preserve_alpha);
  out += '}';
}

std::string placement_json(const std::vector<SwfTag>& tags) {
  std::string out = "[";
  for (std::size_t index = 0; index < tags.size(); ++index) {
    const auto& placement = tags[index].placement;
    if (!placement) continue;
    comma(out);
    out += '{';
    field(out, "tag_index", index);
    field(out, "depth", placement->depth);
    member(out, "character_id");
    if (placement->character_id) {
      value(out, *placement->character_id);
    } else {
      out += "null";
    }
    member(out, "matrix");
    if (placement->matrix) {
      append_transform(out, fo4::swf::transform_from_matrix(*placement->matrix));
    } else {
      out += "null";
    }
    member(out, "filters");
    out += '[';
    for (const auto& filter : placement->filters) {
      comma(out);
      append_filter(out, filter);
    }
    out += ']';
    out += '}';
  }
  out += ']';
  return out;
}

}  // namespace

extern "C" {

radicle_status radicle_swf_edit_text_read(const radicle_swf_history* history, size_t tag_index,
                                          radicle_swf_edit_text_info* out_info, uint8_t** out_text,
                                          size_t* out_text_size) {
  clear_error();
  if (out_info) *out_info = radicle_swf_edit_text_info{};
  if (out_text) *out_text = nullptr;
  if (out_text_size) *out_text_size = 0;
  if (history == nullptr || out_info == nullptr || out_text == nullptr || out_text_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, out_info, out_text and out_text_size are required");
  }
  return guarded("unknown exception while reading a DefineEditText", [&] {
    const SwfFile& file = history->history.file();
    if (tag_index >= file.tags.size()) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the tag index is out of range");
    }
    const SwfTag& tag = file.tags[tag_index];
    if (tag.code != fo4::swf::tag::kDefineEditText || !tag.edit_text) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the tag is not a decoded DefineEditText");
    }
    const auto& text = *tag.edit_text;
    *out_text = copy_text(text.text, out_text_size);
    if (*out_text == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the DefineEditText text");
    }
    out_info->character_id = text.character_id;
    out_info->bounds = to_rect(text.bounds);
    out_info->has_color = text.color ? 1 : 0;
    out_info->color = text.color.value_or(0);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_edit_text_apply(radicle_swf_history* history, size_t tag_index,
                                           const radicle_swf_edit_text_fields* fields,
                                           radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || fields == nullptr || out_result == nullptr ||
      (fields->has_text && fields->text_size != 0 && fields->text == nullptr)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, fields and out_result are required");
  }
  return guarded("unknown exception while editing a DefineEditText", [&] {
    fo4::swf::SwfEditTextEdit edit;
    if (fields->has_text) edit.text = text_bytes(*fields);
    if (fields->has_color) edit.color = fields->color;
    if (fields->has_bounds) edit.bounds = from_rect(fields->bounds);
    return commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
      return fo4::swf::apply_edit_text(file, tag_index, edit, message);
    });
  });
}

radicle_status radicle_swf_frame_label_rows(const radicle_swf_history* history, uint16_t timeline_id,
                                            char*** out_rows, size_t* out_count) {
  clear_error();
  if (out_rows) *out_rows = nullptr;
  if (out_count) *out_count = 0;
  if (history == nullptr || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, out_rows and out_count are required");
  }
  return guarded("unknown exception while listing frame labels", [&] {
    std::vector<fo4::swf::SwfFrameLabel> labels;
    std::string message;
    if (!fo4::swf::list_frame_labels(history->history.file(), timeline_id, labels, message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    std::vector<std::string> rows;
    rows.reserve(labels.size());
    for (const auto& label : labels) {
      rows.push_back(std::to_string(label.label_index) + "\t" + std::to_string(label.frame) + "\t" + label.name);
    }
    return copy_strings(rows, out_rows, out_count);
  });
}

radicle_status radicle_swf_frame_label_add(radicle_swf_history* history, uint16_t timeline_id, uint32_t frame,
                                           const char* name, radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || name == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, name and out_result are required");
  }
  return guarded("unknown exception while adding a frame label", [&] {
    return commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
      return fo4::swf::add_frame_label(file, timeline_id, frame, name, message);
    });
  });
}

radicle_status radicle_swf_frame_label_rename(radicle_swf_history* history, uint16_t timeline_id,
                                              size_t label_index, const char* name,
                                              radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || name == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, name and out_result are required");
  }
  return guarded("unknown exception while renaming a frame label", [&] {
    return commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
      return fo4::swf::rename_frame_label(file, timeline_id, label_index, name, message);
    });
  });
}

radicle_status radicle_swf_frame_label_remove(radicle_swf_history* history, uint16_t timeline_id,
                                              size_t label_index, radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_result are required");
  }
  return guarded("unknown exception while removing a frame label", [&] {
    return commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
      return fo4::swf::remove_frame_label(file, timeline_id, label_index, message);
    });
  });
}

radicle_status radicle_swf_placement_list_json(const radicle_swf_history* history, uint16_t timeline_id,
                                               uint8_t** out_text, size_t* out_size) {
  clear_error();
  if (out_text) *out_text = nullptr;
  if (out_size) *out_size = 0;
  if (history == nullptr || out_text == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, out_text and out_size are required");
  }
  return guarded("unknown exception while listing placements", [&] {
    const std::vector<SwfTag>* tags = nullptr;
    std::string message;
    if (!timeline_tags(history->history.file(), timeline_id, tags, message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    *out_text = copy_text(placement_json(*tags), out_size);
    if (*out_text == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the placement list");
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_apply_instance_transforms(radicle_swf_history* history, uint32_t root_frame,
                                                     const radicle_swf_instance_transform* edits,
                                                     size_t edit_count, radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || out_result == nullptr || (edit_count != 0 && edits == nullptr)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "history and out_result are required, and edits must not be NULL when edit_count is not zero");
  }
  return guarded("unknown exception while applying instance transforms", [&] {
    std::vector<fo4::swf::SwfInstanceTransformEdit> native;
    native.reserve(edit_count);
    for (std::size_t index = 0; index < edit_count; ++index) {
      native.push_back(fo4::swf::SwfInstanceTransformEdit{edits[index].instance_id,
                                                          to_transform(edits[index].transform)});
    }
    return commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
      fo4::swf::SwfTransformEditResult result;
      const bool applied = fo4::swf::apply_instance_transforms(file, root_frame, native, result);
      message = result.message;
      return applied;
    });
  });
}

radicle_status radicle_swf_reassign_instance_depth(radicle_swf_history* history, uint32_t root_frame,
                                                   uint32_t instance_id, uint16_t destination_depth,
                                                   uint32_t* out_instance_id,
                                                   radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_instance_id) *out_instance_id = 0;
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || out_instance_id == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, out_instance_id and out_result are required");
  }
  return guarded("unknown exception while reassigning an instance depth", [&] {
    std::uint32_t moved = 0;
    const radicle_status status =
        commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
          fo4::swf::SwfDepthReassignResult result;
          const bool applied =
              fo4::swf::reassign_instance_depth(file, root_frame, instance_id, destination_depth, result);
          message = result.message;
          moved = result.new_instance_id;
          return applied;
        });
    if (status == RADICLE_STATUS_OK) *out_instance_id = moved;
    return status;
  });
}

radicle_status radicle_swf_insert_instance_transform_keyframe(radicle_swf_history* history, uint32_t root_frame,
                                                              uint32_t instance_id,
                                                              const radicle_swf_transform* transform,
                                                              radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_result are required");
  }
  return guarded("unknown exception while inserting a transform keyframe", [&] {
    return commit_edit(*history, *out_result, [&](SwfFile& file, std::string& message) {
      fo4::swf::SwfKeyframeInsertResult result;
      const bool applied =
          transform == nullptr
              ? fo4::swf::insert_instance_transform_keyframe(file, root_frame, instance_id, result)
              : fo4::swf::insert_instance_transform_keyframe(file, root_frame, instance_id,
                                                             to_transform(*transform), result);
      message = result.message;
      return applied;
    });
  });
}

}  // extern "C"
