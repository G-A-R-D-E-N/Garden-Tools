#include "c_api_env_internal.hpp"
#include "fo4/c_api_sound.h"
#include "fo4/environment/sound.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/sound_writer.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/text/encoding.hpp"

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

struct FileListHolder {
  radicle_sound_file_list view{};
  std::vector<radicle_sound_file> files;
};

bool valid(const radicle_env* env, const radicle_record* record, const void* output) {
  return env && record && output;
}

radicle_status invalid() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and output are required"); }

radicle_status foreign() { return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment"); }

}

namespace {

radicle_status sound_structure_status(fo4::SoundDescriptorMutationStatus status, const char* target) {
  const auto message = [&](const char* tail) { return std::string(target) + tail; };
  switch (status) {
    case fo4::SoundDescriptorMutationStatus::updated:
    case fo4::SoundDescriptorMutationStatus::unchanged:
      return RADICLE_STATUS_OK;
    case fo4::SoundDescriptorMutationStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::SoundDescriptorMutationStatus::record_mismatch: {
      const auto text = message(" is not an SNDR record");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
    case fo4::SoundDescriptorMutationStatus::discriminator_unproven: {
      const auto text = message(" has no proven AutoWeapon discriminator for the BNAM base link");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
    case fo4::SoundDescriptorMutationStatus::already_present: {
      const auto text = message(" is already present and a second copy is refused");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
    case fo4::SoundDescriptorMutationStatus::subrecord_missing: {
      const auto text = message(" has no such subrecord");
      return fail(RADICLE_STATUS_NOT_FOUND, text.c_str());
    }
    case fo4::SoundDescriptorMutationStatus::index_out_of_range: {
      const auto text = message(" index is out of range");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
    case fo4::SoundDescriptorMutationStatus::payload_unproven: {
      const auto text = message(" payload does not match the proven BNAM arm");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown sound structure status");
}

template <class Mutate>
radicle_status sound_structure_edit(radicle_plugin* plugin, radicle_form_key key, const char* target,
                                    Mutate mutate) {
  clear_error();
  const auto record_key = to_form_key(key);
  if (plugin == nullptr || !record_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while editing a sound descriptor", [&] {
    try {
      return sound_structure_status(mutate(*record_key), target);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

void require_file_name(const char* file, const char* what) {
  if (file == nullptr || file[0] == '\0') {
    throw fo4::schema::ScalarError(std::string(what) + " is required");
  }
  if (!fo4::text::is_valid_utf8(file)) {
    throw fo4::schema::ScalarError(std::string(what) + " must be valid UTF-8");
  }
}

}

extern "C" {

radicle_status radicle_sound_descriptor_decode(const radicle_env* env, const radicle_record* record,
                                                const radicle_sound_descriptor** out_descriptor) {
  clear_error();
  if (out_descriptor) *out_descriptor = nullptr;
  if (!valid(env, record, out_descriptor)) return invalid();
  if (!belongs_to_env(env, to_ref(record))) return foreign();
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
  if (!belongs_to_env(env, to_ref(record))) return foreign();
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
  if (!belongs_to_env(env, to_ref(record))) return foreign();
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
  if (!belongs_to_env(env, to_ref(record))) return foreign();
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
  if (!belongs_to_env(env, to_ref(record))) return foreign();
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

radicle_status radicle_sound_resolve_files(const radicle_env* env, const radicle_record* record,
                                           const radicle_sound_file_list** out_list) {
  clear_error();
  if (out_list) *out_list = nullptr;
  if (!valid(env, record, out_list)) return invalid();
  if (!belongs_to_env(env, to_ref(record))) return foreign();
  return guarded("unknown exception while resolving sound files", [&] {
    const auto paths = fo4::env::resolve_sound_files(env->value, *to_ref(record));
    auto holder = std::make_unique<FileListHolder>();
    for (const auto path : paths) holder->files.push_back({stored_text(*env, path)});
    holder->view.files = holder->files.data();
    holder->view.file_count = holder->files.size();
    holder->view.internal = holder.get();
    *out_list = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_sound_file_list_free(const radicle_sound_file_list* list) {
  if (list) delete static_cast<FileListHolder*>(list->internal);
}

radicle_status radicle_sound_select_rate_file(const radicle_sound_rate_file* rate_files, size_t rate_file_count,
                                              uint32_t rpm, int32_t* out_has_index, size_t* out_index) {
  clear_error();
  if (out_has_index) *out_has_index = 0;
  if (out_index) *out_index = 0;
  if ((!rate_files && rate_file_count != 0) || !out_has_index || !out_index) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "rate_files, out_has_index and out_index are required");
  }
  return guarded("unknown exception while selecting a rate file", [&] {
    std::vector<fo4::env::SoundRateFile> entries;
    for (size_t i = 0; i < rate_file_count; ++i) entries.push_back({rate_files[i].rpm, {}});
    const auto index = fo4::env::select_rate_file_index(entries, rpm);
    if (index) {
      *out_has_index = 1;
      *out_index = *index;
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_sound_select_standard_file(size_t count, size_t draw_index, int32_t has_previous,
                                                  size_t previous_index, size_t* out_index) {
  clear_error();
  if (out_index) *out_index = 0;
  if (!out_index) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_index is required");
  if (draw_index >= count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "draw_index must be below count");
  return guarded("unknown exception while selecting a standard file", [&] {
    const std::optional<size_t> previous = has_previous ? std::optional<size_t>{previous_index} : std::nullopt;
    *out_index = fo4::env::select_standard_file_index(count, draw_index, previous);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_sound_frequency_factor(int8_t frequency_shift, double* out_factor) {
  clear_error();
  if (!out_factor) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_factor is required");
  *out_factor = fo4::env::sound_frequency_factor(frequency_shift);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_sound_is_looping(uint8_t loop_control, int32_t* out_looping) {
  clear_error();
  if (!out_looping) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_looping is required");
  *out_looping = fo4::env::sound_is_looping(loop_control) ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_sound_sidechain_send(uint8_t sidechain_control, double* out_send) {
  clear_error();
  if (!out_send) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_send is required");
  *out_send = fo4::env::sound_sidechain_send(sidechain_control);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_sound_small_rumble_send(uint8_t rumble_control, double* out_send) {
  clear_error();
  if (!out_send) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_send is required");
  *out_send = fo4::env::sound_small_rumble_send(rumble_control);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_sound_big_rumble_send(uint8_t rumble_control, double* out_send) {
  clear_error();
  if (!out_send) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_send is required");
  *out_send = fo4::env::sound_big_rumble_send(rumble_control);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_plugin_add_sound_file(radicle_plugin* plugin, radicle_form_key key,
                                             const char* file, size_t* out_file_index) {
  if (out_file_index != nullptr) *out_file_index = 0;
  return sound_structure_edit(plugin, key, "SNDR file", [&](const fo4::FormKey& record) {
    require_file_name(file, "SNDR file name");
    std::size_t added = 0;
    const auto status = fo4::add_sound_descriptor_file(plugin->value, record, file, &added);
    if (status == fo4::SoundDescriptorMutationStatus::updated && out_file_index != nullptr) {
      *out_file_index = added;
    }
    return status;
  });
}

radicle_status radicle_plugin_remove_sound_file(radicle_plugin* plugin, radicle_form_key key,
                                                size_t file_index) {
  return sound_structure_edit(plugin, key, "SNDR file", [&](const fo4::FormKey& record) {
    return fo4::remove_sound_descriptor_file(plugin->value, record, file_index);
  });
}

radicle_status radicle_plugin_set_sound_file(radicle_plugin* plugin, radicle_form_key key,
                                             size_t file_index, const char* file) {
  return sound_structure_edit(plugin, key, "SNDR file", [&](const fo4::FormKey& record) {
    require_file_name(file, "SNDR file name");
    return fo4::set_sound_descriptor_file(plugin->value, record, file_index, file);
  });
}

radicle_status radicle_plugin_add_sound_base_link(radicle_plugin* plugin, radicle_form_key key,
                                                  radicle_form_key target) {
  return sound_structure_edit(plugin, key, "SNDR base link", [&](const fo4::FormKey& record) {
    const auto form_key = to_form_key(target);
    if (!form_key) throw fo4::schema::ScalarError("invalid SNDR base link target");
    return fo4::add_sound_descriptor_base(plugin->value, record, *form_key);
  });
}

radicle_status radicle_plugin_remove_sound_base_link(radicle_plugin* plugin, radicle_form_key key) {
  return sound_structure_edit(plugin, key, "SNDR base link", [&](const fo4::FormKey& record) {
    return fo4::remove_sound_descriptor_base(plugin->value, record);
  });
}

radicle_status radicle_plugin_create_sound_record(radicle_plugin* plugin, const char* signature,
                                                  const char* editor_id_utf8,
                                                  radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || signature == nullptr || editor_id_utf8 == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, signature, editor ID and out_key are required");
  }
  const std::string_view name(signature);
  if (name != "SOUN" && name != "FSTS") {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "only SOUN and FSTS records are created by this export");
  }
  return guarded("unknown exception while creating a sound record", [&] {
    using Status = fo4::RecordCreateStatus;
    const auto stored = fo4::text::utf8_to_windows1252(editor_id_utf8);
    if (!stored) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID is not valid UTF-8 representable in Windows-1252");
    }
    const auto result = fo4::create_record(plugin->value, fo4::Signature::from_string(name), *stored);
    switch (result.status) {
      case Status::created:
        *out_key = {plugin->value.key.filename().data(), result.key.local().value()};
        return RADICLE_STATUS_OK;
      case Status::not_authorable:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type has no authoring schema");
      case Status::no_default:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a required subrecord has no registered default payload");
      case Status::invalid_editor_id:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID cannot be stored for this record type");
      case Status::preflight_failed:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the default record failed authoring preflight");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown record create status");
  });
}
}
