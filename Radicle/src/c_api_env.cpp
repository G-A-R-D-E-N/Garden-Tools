#include "fo4/c_api_env.h"

#include "c_api_env_internal.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

const fo4::ConflictGraph& reference_graph(const radicle_env& env) {
  std::call_once(env.graph_once, [&] {
    std::vector<const fo4::Plugin*> plugins;
    for (size_t i = 0; i < env.value.plugins().size(); ++i) plugins.push_back(&env.value.plugin(i));
    env.graph = fo4::ConflictGraph::build(plugins);
  });
  return *env.graph;
}

template <class Hit>
radicle_status copy_reference_hits(const radicle_env& env,
                                   const std::vector<Hit>& hits,
                                   fo4::FormKey Hit::*key,
                                   radicle_reference_hit** out_hits,
                                   size_t* out_count) {
  *out_hits = nullptr;
  *out_count = 0;
  if (hits.empty()) return RADICLE_STATUS_OK;
  std::vector<radicle_reference_hit> mapped;
  mapped.reserve(hits.size());
  for (const auto& hit : hits) {
    radicle_reference_hit out{};
    out.record = to_c_key(env, hit.*key);
    out.plugin_index = hit.load_order;
    const auto signature = hit.subrecord.view();
    std::memcpy(out.subrecord, signature.data(), 4);
    out.subrecord[4] = '\0';
    out.offset = hit.offset;
    mapped.push_back(out);
  }
  auto* array = static_cast<radicle_reference_hit*>(
      std::malloc(mapped.size() * sizeof(radicle_reference_hit)));
  if (array == nullptr) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate reference hit array");
  }
  std::memcpy(array, mapped.data(), mapped.size() * sizeof(radicle_reference_hit));
  *out_hits = array;
  *out_count = mapped.size();
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_env_load(const char* const* paths, size_t count, radicle_env** out_env) {
  clear_error();
  if (out_env == nullptr || (paths == nullptr && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "paths and out_env are required");
  }
  *out_env = nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (paths[i] == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin paths must not be NULL");
    }
  }
  try {
    std::vector<std::filesystem::path> ordered;
    ordered.reserve(count);
    for (size_t i = 0; i < count; ++i) ordered.push_back(utf8_path(paths[i]));
    auto env = std::make_unique<radicle_env>();
    env->value = fo4::env::Environment::load(ordered);
    for (const auto& info : env->value.plugins()) {
      const auto utf8 = info.path.u8string();
      env->paths.emplace_back(utf8.begin(), utf8.end());
    }
    *out_env = env.release();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::env::EnvironmentError& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while loading environment");
  }
}

void radicle_env_destroy(radicle_env* env) {
  clear_error();
  delete env;
}

size_t radicle_env_plugin_count(const radicle_env* env) {
  clear_error();
  return env == nullptr ? 0 : env->value.plugins().size();
}

radicle_status radicle_env_get_plugin_info(const radicle_env* env,
                                           size_t index,
                                           radicle_env_plugin_info* out_info) {
  clear_error();
  if (env == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_info are required");
  }
  const auto& plugins = env->value.plugins();
  if (index >= plugins.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin index is out of range");
  }
  const auto& info = plugins[index];
  out_info->filename = info.key.filename().data();
  out_info->path = env->paths[index].c_str();
  out_info->kind = info.kind == fo4::PluginKind::light ? RADICLE_PLUGIN_LIGHT : RADICLE_PLUGIN_FULL;
  out_info->master_flag = info.master_flag ? 1 : 0;
  out_info->localized = info.localized ? 1 : 0;
  out_info->master_count = info.masters.size();
  out_info->unresolved_master_count = info.unresolved_masters.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_env_plugin_master(const radicle_env* env,
                                         size_t plugin_index,
                                         size_t master_index,
                                         const char** out_filename,
                                         int32_t* out_resolved) {
  clear_error();
  if (env == nullptr || out_filename == nullptr || out_resolved == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_filename and out_resolved are required");
  }
  const auto& plugins = env->value.plugins();
  if (plugin_index >= plugins.size() || master_index >= plugins[plugin_index].masters.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin or master index is out of range");
  }
  const auto& info = plugins[plugin_index];
  const auto& master = info.masters[master_index];
  bool resolved = true;
  for (const auto& missing : info.unresolved_masters) {
    if (missing == master) resolved = false;
  }
  *out_filename = master.filename().data();
  *out_resolved = resolved ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_env_find_plugin(const radicle_env* env,
                                       const char* filename,
                                       size_t* out_index) {
  clear_error();
  if (env == nullptr || filename == nullptr || out_index == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, filename and out_index are required");
  }
  const auto index = env->value.find_plugin(filename);
  if (!index) return fail(RADICLE_STATUS_NOT_FOUND, "plugin is not loaded");
  *out_index = *index;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_env_winner(const radicle_env* env,
                                  radicle_form_key key,
                                  const radicle_record** out_record) {
  clear_error();
  if (env == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_record are required");
  }
  *out_record = nullptr;
  return guarded("unknown exception while resolving record", [&] {
    const auto form_key = to_form_key(key);
    if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    const auto* ref = env->value.winner(*form_key);
    if (ref == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "record is not in the load order");
    *out_record = to_handle(ref);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_versions(const radicle_env* env,
                                    radicle_form_key key,
                                    const radicle_record*** out_records,
                                    size_t* out_count) {
  clear_error();
  if (env == nullptr || out_records == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_records and out_count are required");
  }
  *out_records = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing record versions", [&] {
    const auto form_key = to_form_key(key);
    if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    std::vector<const fo4::env::RecordRef*> refs;
    for (const auto& ref : env->value.versions(*form_key)) refs.push_back(&ref);
    return copy_records(refs, out_records, out_count);
  });
}

radicle_status radicle_env_find_editor_id(const radicle_env* env,
                                          const char* editor_id,
                                          const char* signature,
                                          const radicle_record** out_record) {
  clear_error();
  if (env == nullptr || editor_id == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, editor_id and out_record are required");
  }
  *out_record = nullptr;
  return guarded("unknown exception while resolving EditorID", [&] {
    std::optional<fo4::Signature> filter;
    if (signature != nullptr) {
      filter = to_signature(signature);
      if (!filter) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature must be four characters");
    }
    const auto stored = fo4::text::utf8_to_windows1252(editor_id);
    if (!stored) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "EditorID is not valid UTF-8 text representable in Windows-1252");
    const auto* ref = env->value.find_by_editor_id(*stored, filter);
    if (ref == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "EditorID is not in the load order");
    *out_record = to_handle(ref);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_winners_of(const radicle_env* env,
                                      const char* signature,
                                      const radicle_record*** out_records,
                                      size_t* out_count) {
  clear_error();
  if (env == nullptr || out_records == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_records and out_count are required");
  }
  *out_records = nullptr;
  *out_count = 0;
  return guarded("unknown exception while enumerating records", [&] {
    const auto filter = to_signature(signature);
    if (!filter) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature must be four characters");
    return copy_records(env->value.winners_of(*filter), out_records, out_count);
  });
}

void radicle_free_record_array(const radicle_record** records) {
  clear_error();
  std::free(static_cast<void*>(records));
}

radicle_status radicle_env_referenced_by(const radicle_env* env,
                                         radicle_form_key key,
                                         radicle_reference_hit** out_hits,
                                         size_t* out_count) {
  clear_error();
  if (env == nullptr || out_hits == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_hits and out_count are required");
  }
  *out_hits = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing references", [&] {
    const auto form_key = to_form_key(key);
    if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    return copy_reference_hits(*env, reference_graph(*env).referenced_by(*form_key),
                               &fo4::ReferencedByHit::source, out_hits, out_count);
  });
}

radicle_status radicle_env_references_from(const radicle_env* env,
                                           radicle_form_key key,
                                           radicle_reference_hit** out_hits,
                                           size_t* out_count) {
  clear_error();
  if (env == nullptr || out_hits == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_hits and out_count are required");
  }
  *out_hits = nullptr;
  *out_count = 0;
  return guarded("unknown exception while listing outgoing references", [&] {
    const auto form_key = to_form_key(key);
    if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid FormKey");
    return copy_reference_hits(*env, reference_graph(*env).references_from(*form_key),
                               &fo4::OutgoingReferenceHit::target, out_hits, out_count);
  });
}

void radicle_free_reference_hits(radicle_reference_hit* hits) {
  clear_error();
  std::free(static_cast<void*>(hits));
}

radicle_status radicle_record_get_info(const radicle_env* env,
                                       const radicle_record* record,
                                       radicle_record_info* out_info) {
  clear_error();
  if (env == nullptr || record == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_info are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while reading record metadata", [&] {
    const auto meta = env->value.metadata(*to_ref(record));
    // Views point into immutable plugin data and are NUL-terminated wherever one was produced.
    const auto& owner = to_ref(record)->record->form_key.owner();
    *out_info = radicle_record_info{};
    out_info->owner_plugin = owner.filename().data();
    out_info->local_id = meta.form_key.local().value();
    const auto signature = meta.signature.view();
    std::memcpy(out_info->signature, signature.data(), 4);
    out_info->signature[4] = '\0';
    out_info->plugin_index = meta.plugin;
    out_info->record_flags = meta.flags.value();
    out_info->deleted = meta.deleted ? 1 : 0;
    out_info->editor_id = stored_text(*env, meta.editor_id);
    switch (meta.name.kind) {
      case fo4::env::NameKind::absent:
        out_info->name_kind = RADICLE_NAME_ABSENT;
        break;
      case fo4::env::NameKind::inline_text:
        out_info->name_kind = RADICLE_NAME_INLINE;
        out_info->name_text = stored_text(*env, meta.name.text);
        break;
      case fo4::env::NameKind::string_id:
        out_info->name_kind = RADICLE_NAME_STRING_ID;
        out_info->name_string_id = meta.name.string_id;
        break;
      case fo4::env::NameKind::malformed:
        out_info->name_kind = RADICLE_NAME_MALFORMED;
        break;
    }
    out_info->model_path = stored_text(*env, meta.model);
    if (meta.bounds) {
      out_info->has_bounds = 1;
      for (int i = 0; i < 3; ++i) {
        out_info->bounds_min[i] = meta.bounds->min[static_cast<size_t>(i)];
        out_info->bounds_max[i] = meta.bounds->max[static_cast<size_t>(i)];
      }
    }
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
