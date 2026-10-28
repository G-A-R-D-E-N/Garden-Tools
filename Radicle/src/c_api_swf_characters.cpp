#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"

#include "fo4/c_api_swf.h"
#include "fo4/swf/swf_svg.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

// Invariant: the buffer carries a trailing NUL that *out_size does not count.
std::uint8_t* copy_text(const std::string& text, std::size_t* out_size) {
  auto* buffer = static_cast<std::uint8_t*>(std::malloc(text.size() + 1));
  if (buffer == nullptr) return nullptr;
  if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = 0;
  *out_size = text.size();
  return buffer;
}

// Constraint: one build per movie revision; a refusal is cached with its message so every getter agrees.
const fo4::swf::SwfCharacterModel& character_model(const radicle_swf* swf) {
  auto& cache = swf->characters;
  if (!cache.built) {
    cache.message.clear();
    fo4::swf::build_character_model(swf->file, cache.model, cache.message);
    cache.built = true;
  }
  return cache.model;
}

std::size_t flat_index_of_root(const radicle_swf& swf, std::size_t position) {
  std::size_t root = 0;
  for (std::size_t index = 0; index < swf.tags.size(); ++index) {
    if (swf.tags[index].parent != RADICLE_SWF_NO_INDEX) continue;
    if (root == position) return index;
    ++root;
  }
  return RADICLE_SWF_NO_INDEX;
}

void fill_rect(const fo4::swf::SwfRect& rect, radicle_swf_rect& out) {
  out = {};
  out.x_min = rect.x_min;
  out.x_max = rect.x_max;
  out.y_min = rect.y_min;
  out.y_max = rect.y_max;
}

void fill_character(const radicle_swf& swf, const fo4::swf::SwfCharacter& source,
                    radicle_swf_character& out) {
  out = {};
  out.character_id = source.character_id;
  out.kind = static_cast<std::uint8_t>(source.kind);
  out.has_bounds = static_cast<std::uint8_t>(source.has_bounds ? 1 : 0);
  out.has_bitmap_size = static_cast<std::uint8_t>(source.has_bitmap_size ? 1 : 0);
  out.tag_index = flat_index_of_root(swf, source.tag_index);
  if (source.has_bounds) {
    fill_rect(source.bounds, out.bounds);
  }
  out.bitmap_width = source.bitmap_width;
  out.bitmap_height = source.bitmap_height;
  out.name = source.name.c_str();
  out.imported_from = source.imported_from.c_str();
  out.imported_name = source.imported_name.c_str();
}

// Constraint: a refused model must report the refusal, not an empty list, and text must be stable.
radicle_status require_model(const radicle_swf* swf, const fo4::swf::SwfCharacterModel** out_model) {
  const auto& model = character_model(swf);
  if (model.refused) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, model.message.c_str());
  }
  *out_model = &model;
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_swf_character_count(const radicle_swf* swf, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (swf == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_count are required");
  }
  return guarded("unknown exception while building the character model", [&] {
    const fo4::swf::SwfCharacterModel* model = nullptr;
    const auto status = require_model(swf, &model);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    *out_count = model->characters.size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_character_at(const radicle_swf* swf, size_t index,
                                        radicle_swf_character* out_character) {
  clear_error();
  if (out_character) *out_character = {};
  if (swf == nullptr || out_character == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_character are required");
  }
  return guarded("unknown exception while reading one character", [&] {
    const fo4::swf::SwfCharacterModel* model = nullptr;
    const auto status = require_model(swf, &model);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    if (index >= model->characters.size()) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the character index is out of range");
    }
    fill_character(*swf, model->characters[index], *out_character);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_character_by_id(const radicle_swf* swf, uint16_t character_id,
                                           radicle_swf_character* out_character) {
  clear_error();
  if (out_character) *out_character = {};
  if (swf == nullptr || out_character == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_character are required");
  }
  return guarded("unknown exception while resolving one character id", [&] {
    const fo4::swf::SwfCharacterModel* model = nullptr;
    const auto status = require_model(swf, &model);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const fo4::swf::SwfCharacter* found = fo4::swf::find_character(*model, character_id);
    if (found == nullptr) {
      const std::string text = "character id " + std::to_string(character_id) + " is not defined";
      return fail(RADICLE_STATUS_NOT_FOUND, text.c_str());
    }
    fill_character(*swf, *found, *out_character);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_swf_shape_svg(const radicle_swf* swf, uint16_t character_id, uint8_t** out_text,
                                     size_t* out_size) {
  clear_error();
  if (out_text) *out_text = nullptr;
  if (out_size) *out_size = 0;
  if (swf == nullptr || out_text == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_text and out_size are required");
  }
  return guarded("unknown exception while exporting a shape", [&] {
    const fo4::swf::SwfCharacterModel* model = nullptr;
    const auto status = require_model(swf, &model);
    if (status != RADICLE_STATUS_OK) {
      return status;
    }
    const fo4::swf::SwfCharacter* found = fo4::swf::find_character(*model, character_id);
    if (found == nullptr || found->kind != fo4::swf::SwfCharacterKind::shape ||
        !swf->file.tags[found->tag_index].shape) {
      const std::string text = "character id " + std::to_string(character_id) + " is not a decoded shape";
      return fail(RADICLE_STATUS_NOT_FOUND, text.c_str());
    }
    const std::string svg = fo4::swf::shape_to_svg(*swf->file.tags[found->tag_index].shape);
    *out_text = copy_text(svg, out_size);
    if (*out_text == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the SVG text");
    }
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
