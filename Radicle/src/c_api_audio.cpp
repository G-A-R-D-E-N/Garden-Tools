#include "fo4/c_api_sound.h"

#include "c_api_internal.hpp"
#include "fo4/audio/lip_file.hpp"
#include "fo4/audio/lip_morph.hpp"
#include "fo4/audio/voice_path.hpp"
#include "fo4/audio/xwma_decoder.hpp"
#include "fo4/error.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace radicle_c_detail;

std::span<const std::byte> as_bytes(const uint8_t* bytes, size_t size) {
  return std::as_bytes(std::span<const uint8_t>(bytes, size));
}

}

extern "C" {

radicle_status radicle_audio_decode_xwma(const uint8_t* bytes,
                                         size_t size,
                                         uint8_t** out_pcm,
                                         size_t* out_pcm_size,
                                         uint32_t* out_sample_rate,
                                         uint16_t* out_channels,
                                         size_t* out_bad_packets) {
  clear_error();
  if (out_pcm) *out_pcm = nullptr;
  if (out_pcm_size) *out_pcm_size = 0;
  if (out_sample_rate) *out_sample_rate = 0;
  if (out_channels) *out_channels = 0;
  if (out_bad_packets) *out_bad_packets = 0;
  if (!bytes || size == 0 || !out_pcm || !out_pcm_size || !out_sample_rate || !out_channels || !out_bad_packets) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and every output are required");
  }
  return guard_exceptions("unknown exception while decoding xWMA", [&] {
    const auto pcm = fo4::audio::decode_xwma(std::as_bytes(std::span<const uint8_t>(bytes, size)));
    const size_t byte_count = pcm.samples.size() * sizeof(int16_t);
    if (byte_count != 0) {
      auto* buffer = static_cast<uint8_t*>(std::malloc(byte_count));
      if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the decoded samples");
      std::memcpy(buffer, pcm.samples.data(), byte_count);
      *out_pcm = buffer;
    }
    *out_pcm_size = byte_count;
    *out_sample_rate = pcm.sample_rate;
    *out_channels = pcm.channels;
    *out_bad_packets = pcm.bad_packets;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_audio_decode_lip(const uint8_t* bytes,
                                        size_t size,
                                        uint32_t* out_frame_count,
                                        int32_t* out_preroll_frames,
                                        uint32_t* out_phoneme_count,
                                        uint32_t* out_modifier_count,
                                        float** out_weights,
                                        size_t* out_weight_count) {
  clear_error();
  if (out_frame_count) *out_frame_count = 0;
  if (out_preroll_frames) *out_preroll_frames = 0;
  if (out_phoneme_count) *out_phoneme_count = 0;
  if (out_modifier_count) *out_modifier_count = 0;
  if (out_weights) *out_weights = nullptr;
  if (out_weight_count) *out_weight_count = 0;
  if (!bytes || size == 0 || !out_frame_count || !out_preroll_frames || !out_phoneme_count || !out_modifier_count ||
      !out_weights || !out_weight_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and every output are required");
  }
  return guard_exceptions("unknown exception while decoding a lip block", [&] {
    const auto lip = fo4::audio::parse_lip(as_bytes(bytes, size));
    const size_t byte_count = lip.weights.size() * sizeof(float);
    auto* buffer = static_cast<float*>(std::malloc(byte_count));
    if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the lip weights");
    std::memcpy(buffer, lip.weights.data(), byte_count);
    *out_weights = buffer;
    *out_weight_count = lip.weights.size();
    *out_frame_count = lip.frame_count;
    *out_preroll_frames = lip.preroll_frames;
    *out_phoneme_count = lip.phoneme_count;
    *out_modifier_count = lip.modifier_count;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_audio_format_voice_path(const char* plugin,
                                               const char* voice_type,
                                               uint32_t info_form_id,
                                               uint32_t response_index,
                                               const char* language,
                                               const char* variant,
                                               const char* extension,
                                               uint8_t** out_path,
                                               size_t* out_size) {
  clear_error();
  if (out_path) *out_path = nullptr;
  if (out_size) *out_size = 0;
  if (!plugin || !voice_type || !out_path || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, voice_type and both outputs are required");
  }
  return guard_exceptions("unknown exception while formatting a voice path", [&] {
    const std::string_view ext = (extension && *extension) ? std::string_view{extension} : std::string_view{"fuz"};
    const auto path = fo4::audio::format_voice_path(plugin, voice_type, info_form_id, response_index,
                                                    language ? std::string_view{language} : std::string_view{},
                                                    variant ? std::string_view{variant} : std::string_view{}, ext);
    auto* buffer = static_cast<uint8_t*>(std::malloc(path.size()));
    if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate the voice path");
    std::memcpy(buffer, path.data(), path.size());
    *out_path = buffer;
    *out_size = path.size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_lip_phoneme_name(uint32_t slot, const char** out_name) {
  clear_error();
  if (out_name) *out_name = nullptr;
  if (!out_name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_name is required");
  return guard_exceptions("unknown exception while naming a lip phoneme", [&] {
    *out_name = fo4::audio::lip_phoneme_name(slot).data();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_lip_slot_morph_count(uint32_t slot, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  if (!out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_count is required");
  return guard_exceptions("unknown exception while counting lip slot morphs", [&] {
    *out_count = fo4::audio::lip_slot_morphs(slot).size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_lip_slot_morph(uint32_t slot, size_t index, const char** out_morph, float* out_weight) {
  clear_error();
  if (out_morph) *out_morph = nullptr;
  if (out_weight) *out_weight = 0.0f;
  if (!out_morph || !out_weight) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_morph and out_weight are required");
  return guard_exceptions("unknown exception while reading a lip slot morph", [&] {
    const auto morphs = fo4::audio::lip_slot_morphs(slot);
    if (index >= morphs.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lip slot morph index is out of range");
    *out_morph = morphs[index].morph.data();
    *out_weight = morphs[index].weight;
    return RADICLE_STATUS_OK;
  });
}

}
