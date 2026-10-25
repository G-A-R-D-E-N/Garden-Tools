#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"

#include "fo4/c_api_swf.h"

#include <cstddef>
#include <cstdint>
#include <string>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

// Constraint: one build per movie revision; a refusal is cached with its message so every getter agrees.
const fo4::swf::SwfCharacterModel& character_model(const radicle_swf* swf) {
  auto& cache = swf->characters;
  if (!cache.built) {
    cache.built = true;
    cache.message.clear();
    fo4::swf::build_character_model(swf->file, cache.model, cache.message);
  }
  return cache.model;
}

void fill_rect(const fo4::swf::SwfRect& rect, radicle_swf_rect& out) {
  out = {};
  out.x_min = rect.x_min;
  out.x_max = rect.x_max;
  out.y_min = rect.y_min;
  out.y_max = rect.y_max;
}

void fill_character(const fo4::swf::SwfCharacter& source, radicle_swf_character& out) {
  out = {};
  out.character_id = source.character_id;
  out.kind = static_cast<std::uint8_t>(source.kind);
  out.has_bounds = static_cast<std::uint8_t>(source.has_bounds ? 1 : 0);
  out.has_bitmap_size = static_cast<std::uint8_t>(source.has_bitmap_size ? 1 : 0);
  out.tag_index = source.tag_index;
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
    fill_character(model->characters[index], *out_character);
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
    fill_character(*found, *out_character);
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
