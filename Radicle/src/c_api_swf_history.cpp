#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"

#include "fo4/c_api_swf.h"
#include "fo4/swf/swf_history.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

bool payload_ok(const uint8_t* payload, size_t size) {
  return (payload != nullptr || size == 0) && size <= std::numeric_limits<std::uint32_t>::max();
}

std::vector<std::byte> copy_payload(const uint8_t* payload, size_t size) {
  if (size == 0) {
    return {};
  }
  return std::vector<std::byte>(reinterpret_cast<const std::byte*>(payload),
                                reinterpret_cast<const std::byte*>(payload) + size);
}

// Requires every plan index to name a top-level tag, matching radicle_swf_apply_plan.
radicle_status translate(const fo4::swf::SwfFile& file, const radicle_swf_tag_plan& plan,
                         fo4::swf::SwfTagPlan& out) {
  if (plan.replacement_count != 0 && plan.replacements == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the plan carries no replacement array");
  }
  if (plan.insertion_count != 0 && plan.insertions == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the plan carries no insertion array");
  }
  if (plan.removal_count != 0 && plan.removals == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the plan carries no removal array");
  }
  out = {};
  for (std::size_t index = 0; index < plan.replacement_count; ++index) {
    const auto& entry = plan.replacements[index];
    std::size_t position = 0;
    if (!radicle_swf_detail::top_level_position(file, entry.tag_index, position)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a plan index must address a top-level tag; replace the DefineSprite tag instead");
    }
    if (!payload_ok(entry.payload, entry.size)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a plan payload needs a pointer when its size is not zero, and must fit the tag length");
    }
    out.replacements.push_back(
        fo4::swf::SwfTagReplacement{position, copy_payload(entry.payload, entry.size)});
  }
  for (std::size_t index = 0; index < plan.insertion_count; ++index) {
    const auto& entry = plan.insertions[index];
    std::size_t position = 0;
    if (!radicle_swf_detail::top_level_position(file, entry.before_tag_index, position)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a plan insertion index must address a top-level tag; replace the DefineSprite tag instead");
    }
    if (entry.code == 0 || entry.code > 0x03FFu) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "an inserted tag code must lie in 1..1023, and End must be refused");
    }
    if (!payload_ok(entry.payload, entry.size)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a plan payload needs a pointer when its size is not zero, and must fit the tag length");
    }
    fo4::swf::SwfTag tag;
    tag.code = entry.code;
    tag.payload = copy_payload(entry.payload, entry.size);
    out.insertions.push_back(fo4::swf::SwfTagInsertion{position, std::move(tag)});
  }
  for (std::size_t index = 0; index < plan.removal_count; ++index) {
    std::size_t position = 0;
    if (!radicle_swf_detail::top_level_position(file, plan.removals[index].tag_index, position)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a plan removal index must address a top-level tag; remove the DefineSprite tag instead");
    }
    out.removals.push_back(fo4::swf::SwfTagRemoval{position});
  }
  return RADICLE_STATUS_OK;
}

void fill_result(const radicle_swf_history& history, bool changed, radicle_swf_plan_result& out) {
  out.changed = changed ? 1 : 0;
  out.revision = history.history.revision();
}

}  // namespace

namespace radicle_swf_detail {

std::size_t subtree_size(const std::vector<fo4::swf::SwfTag>& tags) {
  std::size_t total = 0;
  for (const auto& tag : tags) {
    total += 1 + subtree_size(tag.children);
  }
  return total;
}

bool top_level_position(const fo4::swf::SwfFile& file, std::size_t flat, std::size_t& out) {
  std::size_t start = 0;
  for (std::size_t index = 0; index < file.tags.size(); ++index) {
    if (start == flat) {
      out = index;
      return true;
    }
    start += 1 + subtree_size(file.tags[index].children);
  }
  return false;
}

bool timeline_flat_indices(const fo4::swf::SwfFile& file, std::uint16_t timeline_id,
                           std::vector<std::size_t>& out) {
  out.clear();
  const std::vector<fo4::swf::SwfTag>* tags = &file.tags;
  std::size_t flat = 0;
  if (timeline_id != 0) {
    const fo4::swf::SwfTag* sprite = nullptr;
    for (const auto& tag : file.tags) {
      if (tag.code == fo4::swf::tag::kDefineSprite && tag.sprite_id && *tag.sprite_id == timeline_id) {
        sprite = &tag;
        break;
      }
      flat += 1 + subtree_size(tag.children);
    }
    if (sprite == nullptr) return false;
    tags = &sprite->children;
    flat += 1;
  }
  for (const auto& tag : *tags) {
    out.push_back(flat);
    flat += 1 + subtree_size(tag.children);
  }
  return true;
}

}  // namespace radicle_swf_detail

extern "C" {

radicle_status radicle_swf_history_create(const radicle_swf* swf,
                                          radicle_swf_history** out_history) {
  clear_error();
  if (out_history) *out_history = nullptr;
  if (swf == nullptr || out_history == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_history are required");
  }
  return guarded("unknown exception while opening the edit history", [&] {
    const auto bytes = fo4::swf::write_swf(swf->file);
    auto history = std::make_unique<radicle_swf_history>();
    std::string message;
    if (!history->history.open(bytes, message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    *out_history = history.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_swf_history_destroy(radicle_swf_history* history) { delete history; }

radicle_status radicle_swf_history_apply(radicle_swf_history* history,
                                         const radicle_swf_tag_plan* plan,
                                         radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || plan == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, plan and out_result are required");
  }
  return guarded("unknown exception while applying an edit command", [&] {
    fo4::swf::SwfTagPlan native;
    const auto status = translate(history->history.file(), *plan, native);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const std::uint64_t before = history->history.revision();
    std::string message;
    if (!history->history.apply(native, message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    fill_result(*history, history->history.revision() != before, *out_result);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_history_undo(radicle_swf_history* history,
                                        radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_result are required");
  }
  return guarded("unknown exception while undoing an edit command", [&] {
    std::string message;
    if (!history->history.undo(message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    fill_result(*history, true, *out_result);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_history_redo(radicle_swf_history* history,
                                        radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = history == nullptr ? 0 : history->history.revision();
  }
  if (history == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_result are required");
  }
  return guarded("unknown exception while redoing an edit command", [&] {
    std::string message;
    if (!history->history.redo(message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    fill_result(*history, true, *out_result);
    return RADICLE_STATUS_OK;
  });
}

uint64_t radicle_swf_history_revision(const radicle_swf_history* history) {
  clear_error();
  return history == nullptr ? 0 : history->history.revision();
}

radicle_status radicle_swf_history_undo_count(const radicle_swf_history* history,
                                              size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (history == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_count are required");
  }
  *out_count = history->history.undo_count();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_history_redo_count(const radicle_swf_history* history,
                                              size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (history == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history and out_count are required");
  }
  *out_count = history->history.redo_count();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_history_write_bytes(const radicle_swf_history* history,
                                               uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (history == nullptr || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "history, out_bytes and out_size are required");
  }
  return guarded("unknown exception while serializing a history", [&] {
    std::vector<std::byte> bytes;
    if (!history->history.write_bytes(bytes)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the history is not open");
    }
    if (bytes.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (buffer == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the movie bytes");
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_bytes = buffer;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
