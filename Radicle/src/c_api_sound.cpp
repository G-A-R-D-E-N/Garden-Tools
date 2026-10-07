#include "c_api_env_internal.hpp"
#include "fo4/c_api_sound.h"
#include "fo4/environment/sound.hpp"

#include <memory>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct DescriptorHolder {
  radicle_sound_descriptor view{};
  std::vector<radicle_form_key> descriptors;
  std::vector<radicle_sound_file> files;
  std::vector<radicle_sound_rate_file> rates;
};

struct MusicHolder {
  radicle_music_type view{};
  std::vector<radicle_form_key> tracks;
};

struct FootstepSetHolder {
  radicle_footstep_set view{};
  std::vector<radicle_form_key> steps;
};

bool valid(const radicle_env* env, const radicle_record* record, const void* output) {
  return env && record && output;
}

radicle_status invalid() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output are required"); }

}

extern "C" {

radicle_status radicle_sound_descriptor_decode(const radicle_env* env, const radicle_record* record,
                                                const radicle_sound_descriptor** out_descriptor) {
  clear_error();
  if (out_descriptor) *out_descriptor = nullptr;
  if (!valid(env, record, out_descriptor)) return invalid();
  return guarded("unknown exception while decoding SNDR", [&] {
    const auto data = fo4::env::decode_sound_descriptor(env->value, *to_ref(record));
    auto holder = std::make_unique<DescriptorHolder>();
    auto& v = holder->view;
    set_optional(data.kind, v.has_kind, v.kind);
    v.category = to_c_key(*env, data.category);
    v.alternate = to_c_key(*env, data.alternate);
    v.output_model = to_c_key(*env, data.output_model);
    v.base_descriptor = to_c_key(*env, data.base_descriptor);
    for (const auto& key : data.descriptors) holder->descriptors.push_back(to_c_key(*env, key));
    for (const auto file : data.files) holder->files.push_back({stored_text(*env, file)});
    for (const auto& rate : data.rate_files) holder->rates.push_back({rate.rpm, stored_text(*env, rate.file)});
    v.descriptors = holder->descriptors.data();
    v.descriptor_count = holder->descriptors.size();
    v.files = holder->files.data();
    v.file_count = holder->files.size();
    v.rate_files = holder->rates.data();
    v.rate_file_count = holder->rates.size();
    if (data.controls) {
      v.has_controls = 1;
      std::copy(data.controls->begin(), data.controls->end(), v.controls);
    }
    if (data.playback) {
      v.has_playback = 1;
      const auto& p = *data.playback;
      v.playback = {p.frequency_shift, p.frequency_variance, p.priority, p.db_variance, p.attenuation_hundredths};
    }
    v.internal = holder.get();
    *out_descriptor = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_sound_descriptor_free(const radicle_sound_descriptor* descriptor) {
  if (descriptor) delete static_cast<DescriptorHolder*>(descriptor->internal);
}

radicle_status radicle_sound_marker_get_info(const radicle_env* env, const radicle_record* record,
                                            radicle_sound_marker_info* out_info) {
  clear_error();
  if (!valid(env, record, out_info)) return invalid();
  return guarded("unknown exception while decoding SOUN", [&] {
    const auto data = fo4::env::decode_sound_marker(env->value, *to_ref(record));
    radicle_sound_marker_info v{};
    v.descriptor = to_c_key(*env, data.descriptor);
    if (data.repeat) {
      v.has_repeat = 1;
      v.min_time = data.repeat->min_time;
      v.max_time = data.repeat->max_time;
      set_optional(data.repeat->stackable, v.has_stackable, v.stackable);
    }
    *out_info = v;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_music_type_decode(const radicle_env* env, const radicle_record* record,
                                        const radicle_music_type** out_music) {
  clear_error();
  if (out_music) *out_music = nullptr;
  if (!valid(env, record, out_music)) return invalid();
  return guarded("unknown exception while decoding MUSC", [&] {
    const auto data = fo4::env::decode_music_type(env->value, *to_ref(record));
    auto holder = std::make_unique<MusicHolder>();
    auto& v = holder->view;
    set_optional(data.flags, v.has_flags, v.flags);
    set_optional(data.priority, v.has_priority, v.priority);
    v.ducking_hundredths = data.ducking_hundredths.value_or(0);
    set_optional(data.fade_duration, v.has_fade_duration, v.fade_duration);
    for (const auto& key : data.tracks) holder->tracks.push_back(to_c_key(*env, key));
    v.tracks = holder->tracks.data();
    v.track_count = holder->tracks.size();
    v.internal = holder.get();
    *out_music = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_music_type_free(const radicle_music_type* music) {
  if (music) delete static_cast<MusicHolder*>(music->internal);
}

radicle_status radicle_footstep_get_info(const radicle_env* env, const radicle_record* record,
                                       radicle_footstep_info* out_info) {
  clear_error();
  if (!valid(env, record, out_info)) return invalid();
  return guarded("unknown exception while decoding FSTP", [&] {
    const auto data = fo4::env::decode_footstep(env->value, *to_ref(record));
    *out_info = {to_c_key(*env, data.impact_set), stored_text(*env, data.tag)};
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_footstep_set_decode(const radicle_env* env, const radicle_record* record,
                                         const radicle_footstep_set** out_set) {
  clear_error();
  if (out_set) *out_set = nullptr;
  if (!valid(env, record, out_set)) return invalid();
  return guarded("unknown exception while decoding FSTS", [&] {
    const auto data = fo4::env::decode_footstep_set(env->value, *to_ref(record));
    auto holder = std::make_unique<FootstepSetHolder>();
    auto& v = holder->view;
    for (std::size_t i = 0; i < data.categories.size(); ++i) {
      v.counts[i] = static_cast<std::uint32_t>(data.categories[i].size());
      for (const auto& key : data.categories[i]) holder->steps.push_back(to_c_key(*env, key));
    }
    v.steps = holder->steps.data();
    v.step_count = holder->steps.size();
    v.internal = holder.get();
    *out_set = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_footstep_set_free(const radicle_footstep_set* set) {
  if (set) delete static_cast<FootstepSetHolder*>(set->internal);
}

}
