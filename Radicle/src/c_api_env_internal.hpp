#pragma once

#include "c_api_internal.hpp"
#include "fo4/c_api_env.h"
#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/graph/conflict_graph.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/text/encoding.hpp"

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <exception>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct radicle_assets {
  fo4::archive::AssetResolver value;
};

struct radicle_env {
  fo4::env::Environment value;
  std::vector<std::string> paths;
  mutable std::mutex text_mutex;
  mutable std::unordered_map<const char*, std::string> text;
  mutable std::once_flag graph_once;
  mutable std::optional<fo4::ConflictGraph> graph;
};

namespace radicle_c_env_detail {

using namespace radicle_c_detail;

inline const char* stored_text(const radicle_env& env, std::string_view stored) {
  if (std::all_of(stored.begin(), stored.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x80; })) {
    return stored.data();
  }
  const std::lock_guard lock(env.text_mutex);
  auto [it, inserted] = env.text.try_emplace(stored.data());
  if (inserted) it->second = fo4::text::windows1252_to_utf8(stored);
  return it->second.c_str();
}

inline const char* stored_text(const radicle_env& env, const std::optional<std::string_view>& stored) {
  return stored ? stored_text(env, *stored) : nullptr;
}

inline const fo4::env::RecordRef* to_ref(const radicle_record* record) {
  return reinterpret_cast<const fo4::env::RecordRef*>(record);
}

inline const radicle_record* to_handle(const fo4::env::RecordRef* ref) {
  return reinterpret_cast<const radicle_record*>(ref);
}

inline bool belongs_to_env(const radicle_env* env, const fo4::env::RecordRef* ref) {
  const auto versions = env->value.versions(ref->record->form_key);
  return std::any_of(versions.begin(), versions.end(),
                     [&](const auto& candidate) { return &candidate == ref; });
}

inline std::optional<fo4::FormKey> to_form_key(radicle_form_key key) {
  if (key.plugin == nullptr || *key.plugin == '\0' || key.local_id > 0x00FFFFFFu) {
    return std::nullopt;
  }
  return fo4::FormKey{fo4::PluginKey::from_filename(key.plugin),
                      fo4::LocalFormId{key.local_id}};
}

inline std::optional<fo4::Signature> to_signature(const char* text) {
  if (text == nullptr || std::strlen(text) != 4) return std::nullopt;
  return fo4::Signature::from_string(text);
}

inline radicle_status copy_records(const std::vector<const fo4::env::RecordRef*>& refs,
                            const radicle_record*** out_records,
                            size_t* out_count) {
  *out_records = nullptr;
  *out_count = 0;
  if (refs.empty()) return RADICLE_STATUS_OK;
  auto** array = static_cast<const radicle_record**>(
      std::malloc(refs.size() * sizeof(const radicle_record*)));
  if (array == nullptr) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate record array");
  }
  for (size_t i = 0; i < refs.size(); ++i) array[i] = to_handle(refs[i]);
  *out_records = array;
  *out_count = refs.size();
  return RADICLE_STATUS_OK;
}

template <class Fn>
radicle_status guarded(const char* what, Fn&& fn) {
  try {
    return fn();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::env::EnvironmentError& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const fo4::FormIdError& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const fo4::archive::Ba2Error& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const fo4::archive::AssetPathError& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

inline radicle_form_key to_c_key(const radicle_env& env, const std::optional<fo4::FormKey>& key) {
  if (!key || key->owner().empty()) return {nullptr, 0};
  const auto* owner = env.value.stable_owner(*key);
  if (owner == nullptr) throw fo4::env::EnvironmentError("FormKey owner is not in the environment");
  return {owner->filename().data(), key->local().value()};
}

inline bool convert_text(const char* utf8, std::optional<std::string>& out) {
  if (utf8 == nullptr) return true;
  auto converted = fo4::text::utf8_to_windows1252(utf8);
  if (!converted) return false;
  out = std::move(*converted);
  return true;
}

template <class Row>
bool to_ctda(const Row& row, fo4::CtdaCondition& out) {
  out.flags_byte = row.flags_byte;
  out.comparison_raw = row.comparison_raw;
  out.function = row.function;
  out.unknown2 = row.unknown2;
  out.param1 = row.param1;
  out.param2 = row.param2;
  out.run_on = row.run_on;
  out.reference = row.reference;
  out.unknown3 = row.unknown3;
  return convert_text(row.parameter1_string, out.parameter1_string) &&
         convert_text(row.parameter2_string, out.parameter2_string);
}

radicle_status load_strings(const radicle_env* env,
                            const char* strings_dir,
                            const char* language,
                            const fo4::archive::AssetResolver* assets,
                            radicle_strings** out_strings);

inline radicle_byte_color to_c_color(const fo4::env::ByteColor& color) {
  return {color.red, color.green, color.blue};
}

template <class T>
void set_optional(const std::optional<T>& value, int32_t& has, T& out) {
  has = value ? 1 : 0;
  if (value) out = *value;
}

inline radicle_cell_lighting to_c_lighting(const fo4::env::CellLighting& in) {
  radicle_cell_lighting out{};
  out.ambient = to_c_color(in.ambient);
  out.directional = to_c_color(in.directional);
  out.fog_near_color = to_c_color(in.fog_near_color);
  out.fog_near = in.fog_near;
  out.fog_far = in.fog_far;
  out.directional_rotation_xy = in.directional_rotation_xy;
  out.directional_rotation_z = in.directional_rotation_z;
  out.directional_fade = in.directional_fade;
  out.fog_clip_distance = in.fog_clip_distance;
  out.fog_power = in.fog_power;
  if (in.fog_far_color) {
    out.has_fog_far_color = 1;
    out.fog_far_color = to_c_color(*in.fog_far_color);
  }
  set_optional(in.fog_max, out.has_fog_max, out.fog_max);
  if (in.light_fade_begin && in.light_fade_end) {
    out.has_light_fade = 1;
    out.light_fade_begin = *in.light_fade_begin;
    out.light_fade_end = *in.light_fade_end;
  }
  set_optional(in.inherits, out.has_inherits, out.inherits);
  return out;
}

inline radicle_cell_lighting_tail to_c_lighting_tail(const fo4::env::CellLighting& in) {
  radicle_cell_lighting_tail out{};
  if (in.near_height_mid && in.near_height_range) {
    out.has_near_height = 1;
    out.near_height_mid = *in.near_height_mid;
    out.near_height_range = *in.near_height_range;
  }
  if (in.fog_color_high_near && in.fog_color_high_far) {
    out.has_high_fog_colors = 1;
    out.fog_color_high_near = to_c_color(*in.fog_color_high_near);
    out.fog_color_high_far = to_c_color(*in.fog_color_high_far);
  }
  set_optional(in.high_density_scale, out.has_high_density_scale, out.high_density_scale);
  if (in.fog_near_scale) {
    out.has_fog_scales = 1;
    out.fog_near_scale = *in.fog_near_scale;
    if (in.fog_far_scale) out.fog_far_scale = *in.fog_far_scale;
    if (in.fog_high_near_scale) out.fog_high_near_scale = *in.fog_high_near_scale;
    if (in.fog_high_far_scale) out.fog_high_far_scale = *in.fog_high_far_scale;
  }
  if (in.far_height_mid && in.far_height_range) {
    out.has_far_height = 1;
    out.far_height_mid = *in.far_height_mid;
    out.far_height_range = *in.far_height_range;
  }
  return out;
}

}  // namespace radicle_c_env_detail
