#include "fo4/c_api_plugin_ops.h"

#include "c_api_env_internal.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/loadorder/load_order_files.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/master_usage.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/forms/record_flags.hpp"
#include "fo4/plugin/writer.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

template <class Fn>
radicle_status guarded_mutation(const char* what, Fn&& fn) {
  try {
    return fn();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::FormIdError& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const fo4::forms::PlacedReferenceError& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

fo4::PluginKind to_plugin_kind(radicle_plugin_kind kind) {
  switch (kind) {
    case RADICLE_PLUGIN_FULL:
      return fo4::PluginKind::full;
    case RADICLE_PLUGIN_LIGHT:
      return fo4::PluginKind::light;
  }
  throw std::invalid_argument("invalid Radicle plugin kind");
}

template <class To, class From>
To mirror(From value) {
  return static_cast<To>(static_cast<int>(value));
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_set_hedr_next_form_id(radicle_plugin* plugin, uint32_t next_form_id) {
  clear_error();
  if (plugin == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  }
  return guarded_mutation("unknown exception while setting the HEDR next object ID", [&] {
    auto next = plugin->value;
    fo4::set_hedr_next_form_id(next, next_form_id);
    plugin->value = std::move(next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_compact_to_esl_detailed(radicle_plugin* plugin,
                                                    radicle_esl_compaction_status* out_status,
                                                    size_t* out_remapped,
                                                    uint32_t* out_next_form_id,
                                                    char*** out_blockers,
                                                    size_t* out_blocker_count) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr || out_remapped == nullptr ||
      out_next_form_id == nullptr || out_blockers == nullptr || out_blocker_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and all compaction outputs are required");
  }
  *out_status = RADICLE_ESL_ALREADY_COMPATIBLE;
  *out_remapped = 0;
  *out_next_form_id = 0;
  *out_blockers = nullptr;
  *out_blocker_count = 0;
  return guarded_mutation("unknown exception while compacting to ESL", [&] {
    auto next = plugin->value;
    const auto result = fo4::compact_to_esl(next);
    std::vector<std::string> blockers;
    for (const auto& blocker : result.blockers) {
      blockers.push_back(blocker.key.to_string() + '\t' + blocker.signature.to_string() + '\t' +
                         (blocker.nested ? "1" : "0"));
    }
    if (const auto status = copy_strings(blockers, out_blockers, out_blocker_count);
        status != RADICLE_STATUS_OK) {
      return status;
    }
    plugin->value = std::move(next);
    *out_status = mirror<radicle_esl_compaction_status>(result.status);
    *out_remapped = result.remapped;
    *out_next_form_id = result.next_form_id;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_record_flags(radicle_plugin* plugin,
                                               radicle_form_key record,
                                               uint32_t flags,
                                               radicle_record_flag_status* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (plugin == nullptr || out_status == nullptr || !key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record and out_status are required");
  }
  *out_status = RADICLE_RECORD_FLAGS_UNCHANGED;
  return guarded_mutation("unknown exception while setting record flags", [&] {
    auto next = plugin->value;
    *out_status = mirror<radicle_record_flag_status>(fo4::set_record_flags(next, *key, flags));
    plugin->value = std::move(next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_mark_override_deleted(radicle_plugin* plugin,
                                                    radicle_form_key record,
                                                    radicle_mark_deleted_status* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (plugin == nullptr || out_status == nullptr || !key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record and out_status are required");
  }
  *out_status = RADICLE_MARK_DELETED_RECORD_MISSING;
  return guarded_mutation("unknown exception while marking the override deleted", [&] {
    auto next = plugin->value;
    *out_status = mirror<radicle_mark_deleted_status>(fo4::mark_override_deleted(next, *key));
    plugin->value = std::move(next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_delete_local_record(radicle_plugin* plugin,
                                                  radicle_form_key record,
                                                  radicle_record_delete_status* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (plugin == nullptr || out_status == nullptr || !key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record and out_status are required");
  }
  *out_status = RADICLE_RECORD_DELETE_RECORD_MISSING;
  return guarded_mutation("unknown exception while deleting the record", [&] {
    auto next = plugin->value;
    *out_status = mirror<radicle_record_delete_status>(fo4::delete_local_record(next, *key));
    plugin->value = std::move(next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_undelete_and_disable_reference(radicle_plugin* plugin,
                                                             const radicle_env* env,
                                                             radicle_form_key record,
                                                             radicle_undelete_status* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (plugin == nullptr || env == nullptr || out_status == nullptr || !key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, environment, a valid record and out_status are required");
  }
  *out_status = RADICLE_UNDELETE_RECORD_MISSING;
  return guarded_mutation("unknown exception while undeleting the reference", [&] {
    auto next = plugin->value;
    *out_status = mirror<radicle_undelete_status>(
        fo4::undelete_and_disable_reference(next, env->value, *key));
    plugin->value = std::move(next);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_add_master_detailed(radicle_plugin* plugin,
                                         const char* filename,
                                         radicle_plugin_kind kind,
                                         radicle_master_add_status* out_status) {
  clear_error();
  if (plugin == nullptr || filename == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, filename and out_status are required");
  }
  *out_status = RADICLE_MASTER_ALREADY_PRESENT;
  return guarded_mutation("unknown exception while adding a master", [&] {
    const auto key = fo4::PluginKey::from_filename(filename);
    if (key.empty()) {
      throw std::invalid_argument("master filename is empty");
    }
    if (key == plugin->value.key) {
      throw std::invalid_argument("a plugin cannot declare itself as a master");
    }
    const auto present = std::any_of(
        plugin->value.header.masters.begin(), plugin->value.header.masters.end(),
        [&](const fo4::MasterReference& master) { return master.key == key; });
    if (present) {
      return RADICLE_STATUS_OK;
    }
    std::vector<fo4::MasterDescriptor> masters;
    for (const auto& master : plugin->value.header.masters) {
      masters.push_back({master.key, master.kind});
    }
    masters.push_back({key, to_plugin_kind(kind)});
    plugin->value = fo4::remap_plugin_masters(plugin->value, std::move(masters));
    *out_status = RADICLE_MASTER_ADDED;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_clean_unused_masters(radicle_plugin* plugin,
                                                   radicle_clean_masters_status* out_status,
                                                   char*** out_removed,
                                                   size_t* out_removed_count) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr || out_removed == nullptr || out_removed_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, out_status and removed outputs are required");
  }
  *out_status = RADICLE_CLEAN_MASTERS_NOTHING_UNUSED;
  *out_removed = nullptr;
  *out_removed_count = 0;
  return guarded_mutation("unknown exception while cleaning unused masters", [&] {
    const auto usage = fo4::scan_master_usage(plugin->value);
    if (!usage.complete()) {
      *out_status = RADICLE_CLEAN_MASTERS_INCOMPLETE_USAGE;
      return RADICLE_STATUS_OK;
    }
    const auto kept = fo4::used_masters(plugin->value, usage);
    std::vector<std::string> removed;
    for (const auto& master : plugin->value.header.masters) {
      const bool keep = std::any_of(kept.begin(), kept.end(),
                                    [&](const fo4::MasterDescriptor& item) { return item.key == master.key; });
      if (!keep) {
        removed.emplace_back(master.key.filename());
      }
    }
    if (removed.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto next = fo4::remap_plugin_masters(plugin->value, kept);
    if (const auto status = copy_strings(removed, out_removed, out_removed_count);
        status != RADICLE_STATUS_OK) {
      return status;
    }
    plugin->value = std::move(next);
    *out_status = RADICLE_CLEAN_MASTERS_CLEANED;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_list_write(const char* path,
                                         const char* const* names,
                                         const int32_t* active,
                                         size_t count) {
  clear_error();
  if (path == nullptr || (count != 0 && (names == nullptr || active == nullptr))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path, names and active are required");
  }
  try {
    fo4::loadorder::PluginList list;
    for (size_t i = 0; i < count; ++i) {
      if (names[i] == nullptr) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin name cannot be null");
      }
      list.entries.push_back({names[i], active[i] != 0, i + 1});
    }
    fo4::loadorder::write_plugin_list(utf8_path(path), list);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while writing the plugin list");
  }
}

}  // extern "C"
