#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

/* Invariant: a plan index must be the flat index radicle_swf_tag_at reports, and only a top-level tag is addressable. */
bool top_level_position(const radicle_swf& swf, std::size_t flat, std::size_t& out) {
  if (flat >= swf.tags.size() || swf.tags[flat].parent != RADICLE_SWF_NO_INDEX) return false;
  std::size_t position = 0;
  for (std::size_t index = 0; index < flat; ++index) {
    if (swf.tags[index].parent == RADICLE_SWF_NO_INDEX) ++position;
  }
  out = position;
  return true;
}

/* Requires the payload pointer whenever the size is not zero, and a size the tag length field can hold. */
bool payload_ok(const uint8_t* payload, size_t size) {
  return (payload != nullptr || size == 0) && size <= std::numeric_limits<std::uint32_t>::max();
}

std::vector<std::byte> copy_payload(const uint8_t* payload, size_t size) {
  if (size == 0) return {};
  return std::vector<std::byte>(reinterpret_cast<const std::byte*>(payload),
                                reinterpret_cast<const std::byte*>(payload) + size);
}

/* Requires every plan index to name a top-level tag, because the engine inserts into the root tag stream only. */
radicle_status translate(const radicle_swf& swf, const radicle_swf_tag_plan& plan,
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
    if (!top_level_position(swf, entry.tag_index, position)) {
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
    if (!top_level_position(swf, entry.before_tag_index, position)) {
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
    if (!top_level_position(swf, plan.removals[index].tag_index, position)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "a plan removal index must address a top-level tag; replace the DefineSprite tag instead");
    }
    out.removals.push_back(fo4::swf::SwfTagRemoval{position});
  }
  return RADICLE_STATUS_OK;
}

/* Invariant: the handle must keep its file, views and revision unless the plan landed, so the build runs on a copy. */
radicle_status apply(radicle_swf& swf, const radicle_swf_tag_plan& plan, radicle_swf_plan_result& out) {
  fo4::swf::SwfTagPlan translated;
  if (const auto refused = translate(swf, plan, translated); refused != RADICLE_STATUS_OK) {
    return refused;
  }
  fo4::swf::SwfFile candidate = swf.file;
  fo4::swf::SwfTagPlanResult applied;
  if (!fo4::swf::apply_tag_plan(candidate, translated, applied)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "the plan did not apply: a target was repeated, named the terminal End, or carried a body that did not decode");
  }
  if (!applied.changed) return RADICLE_STATUS_OK;

  radicle_swf_detail::swf_views views;
  radicle_swf_detail::build_views(candidate, views);
  swf.file = std::move(candidate);
  radicle_swf_detail::install_views(swf, std::move(views));
  if (swf.revision && swf.revision->revision != std::numeric_limits<std::uint64_t>::max()) {
    ++swf.revision->revision;
  }
  out.changed = 1;
  out.revision = swf.revision ? swf.revision->revision : 0;
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_swf_apply_plan(radicle_swf* swf, const radicle_swf_tag_plan* plan,
                                      radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result != nullptr) {
    out_result->changed = 0;
    out_result->revision = swf == nullptr || !swf->revision ? 0 : swf->revision->revision;
  }
  if (swf == nullptr || plan == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, plan and out_result are required");
  }
  return guarded("unknown exception while applying a tag plan",
                 [&] { return apply(*swf, *plan, *out_result); });
}

uint64_t radicle_swf_revision(const radicle_swf* swf) {
  clear_error();
  return swf == nullptr || !swf->revision ? 0 : swf->revision->revision;
}

radicle_status radicle_swf_write_bytes(const radicle_swf* swf, uint8_t** out_bytes,
                                       size_t* out_size) {
  clear_error();
  if (out_bytes) *out_bytes = nullptr;
  if (out_size) *out_size = 0;
  if (swf == nullptr || out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_bytes and out_size are required");
  }
  return guarded("unknown exception while serializing a movie", [&] {
    const auto bytes = fo4::swf::write_swf(swf->file);
    if (bytes.empty()) return RADICLE_STATUS_OK;
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
