#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/swf/swf_sound.hpp"
#include "fo4/swf/swf_sound_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

const fo4::swf::SwfTag* decoded_tag(const radicle_swf* swf, std::size_t index) {
  if (swf == nullptr || index >= swf->tags.size()) {
    return nullptr;
  }
  return swf->tags[index].tag;
}

/* Invariant: the handle keeps its views and revision unless the edit reports ok. */
radicle_status finish_edit(radicle_swf& swf, fo4::swf::SwfSoundEditStatus status,
                           radicle_swf_plan_result* out_result) {
  switch (status) {
    case fo4::swf::SwfSoundEditStatus::ok:
      break;
    case fo4::swf::SwfSoundEditStatus::unknown_tag:
    case fo4::swf::SwfSoundEditStatus::unknown_sound:
    // Requires NOT_FOUND when the index names no StartSound, so a bad retarget is observable only as a status.
    case fo4::swf::SwfSoundEditStatus::not_a_start_sound:
      return fail(RADICLE_STATUS_NOT_FOUND, "the sound id or tag index names nothing");
    default:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the sound edit was refused");
  }
  radicle_swf_detail::swf_views views;
  radicle_swf_detail::build_views(swf.file, views);
  radicle_swf_detail::install_views(swf, std::move(views));
  if (swf.revision && swf.revision->revision != std::numeric_limits<std::uint64_t>::max()) {
    ++swf.revision->revision;
  }
  out_result->changed = 1;
  out_result->revision = swf.revision ? swf.revision->revision : 0;
  return RADICLE_STATUS_OK;
}

void fill_sound(const fo4::swf::SwfSound& sound, radicle_swf_sound& out) {
  out = {};
  out.sound_id = sound.sound_id;
  out.format = sound.format;
  out.rate = sound.rate;
  out.sample_16bit = sound.sample_16bit ? 1 : 0;
  out.stereo = sound.stereo ? 1 : 0;
  out.sample_count = sound.sample_count;
  out.data_size = sound.data_size;
}

void fill_start_sound(const fo4::swf::SwfStartSound& start, radicle_swf_start_sound& out) {
  out = {};
  out.sound_id = start.sound_id;
  out.sync_stop = start.info.sync_stop ? 1 : 0;
  out.sync_no_multiple = start.info.sync_no_multiple ? 1 : 0;
  out.has_in_point = start.info.in_point.has_value() ? 1 : 0;
  out.in_point = start.info.in_point.value_or(0);
  out.has_out_point = start.info.out_point.has_value() ? 1 : 0;
  out.out_point = start.info.out_point.value_or(0);
  out.has_loop_count = start.info.loop_count.has_value() ? 1 : 0;
  out.loop_count = start.info.loop_count.value_or(0);
  out.envelope_count = start.info.envelope.has_value() ? start.info.envelope->size() : 0;
}

}  // namespace

extern "C" {

radicle_status radicle_swf_sound_at(const radicle_swf* swf, size_t index,
                                    radicle_swf_sound* out_sound) {
  clear_error();
  if (out_sound != nullptr) {
    *out_sound = {};
  }
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_sound == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_sound and a tag index are required");
  }
  if (!tag->sound) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded DefineSound");
  }
  fill_sound(*tag->sound, *out_sound);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_start_sound_at(const radicle_swf* swf, size_t index,
                                          radicle_swf_start_sound* out_start) {
  clear_error();
  if (out_start != nullptr) {
    *out_start = {};
  }
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_start == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_start and a tag index are required");
  }
  fo4::swf::SwfStartSound start;
  if (fo4::swf::decode_start_sound(tag->payload, start) != fo4::swf::SwfSoundStatus::ok) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded StartSound");
  }
  fill_start_sound(start, *out_start);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_replace_sound_data(radicle_swf* swf, const radicle_swf_sound* header,
                                              const uint8_t* data, size_t size,
                                              radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result != nullptr) {
    out_result->changed = 0;
    out_result->revision = swf == nullptr || !swf->revision ? 0 : swf->revision->revision;
  }
  if (swf == nullptr || header == nullptr || out_result == nullptr ||
      (data == nullptr && size != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, header, data and out_result are required");
  }
  return guarded("unknown exception while replacing sound data", [&] {
    fo4::swf::SwfSound sound;
    sound.sound_id = header->sound_id;
    sound.format = header->format;
    sound.rate = header->rate;
    sound.sample_16bit = header->sample_16bit != 0;
    sound.stereo = header->stereo != 0;
    sound.sample_count = header->sample_count;
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
    const auto status = fo4::swf::replace_sound_data(swf->file, sound, bytes);
    return finish_edit(*swf, status, out_result);
  });
}

radicle_status radicle_swf_retarget_start_sound(radicle_swf* swf, size_t index,
                                                uint16_t new_sound_id,
                                                radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result != nullptr) {
    out_result->changed = 0;
    out_result->revision = swf == nullptr || !swf->revision ? 0 : swf->revision->revision;
  }
  if (swf == nullptr || out_result == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_result are required");
  }
  return guarded("unknown exception while retargeting a StartSound", [&] {
    const auto status = fo4::swf::retarget_start_sound(swf->file, index, new_sound_id);
    return finish_edit(*swf, status, out_result);
  });
}

}  // extern "C"
