#include "fo4/c_api_pet_write.h"

#include "c_api_env_internal.hpp"
#include "fo4/formid.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"

#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status fail_first(const std::string& message, const fo4::FormKey& key) {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT,
              (key.owner().empty() ? message : message + key.to_string()).c_str());
}

template <class Fn>
radicle_status checked(const char* what, Fn&& fn) {
  return guarded(what, [&] {
    try {
      return fn();
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_compact_to_esl(radicle_plugin* plugin,
                                             size_t* out_remapped,
                                             uint32_t* out_next_id) {
  clear_error();
  if (plugin == nullptr || out_remapped == nullptr || out_next_id == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, out_remapped and out_next_id are required");
  }
  *out_remapped = 0;
  *out_next_id = 0;
  return checked("unknown exception while compacting into the light range", [&] {
    const auto result = fo4::compact_to_esl(plugin->value);
    switch (result.status) {
      case fo4::EslCompactionStatus::compacted:
      case fo4::EslCompactionStatus::already_compatible:
        *out_remapped = result.remapped;
        *out_next_id = result.next_form_id;
        return RADICLE_STATUS_OK;
      case fo4::EslCompactionStatus::capacity_exceeded:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin has more native records than the light range holds");
      case fo4::EslCompactionStatus::blocked_nested_records:
        return fail_first("records nested in cell groups block the compaction, first is ",
                          result.blockers.empty() ? fo4::FormKey{} : result.blockers.front().key);
      case fo4::EslCompactionStatus::rekey_failed:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a record could not be rekeyed into the light range");
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown ESL compaction status");
  });
}

radicle_status radicle_plugin_set_light_flag(radicle_plugin* plugin, int32_t light) {
  clear_error();
  if (plugin == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  return checked("unknown exception while setting the light flag", [&] {
    plugin->value = fo4::set_plugin_light_flag(plugin->value, light != 0);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_clean_deleted(radicle_plugin* plugin,
                                            size_t* out_undeleted,
                                            size_t* out_skipped) {
  clear_error();
  if (plugin == nullptr || out_undeleted == nullptr || out_skipped == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, out_undeleted and out_skipped are required");
  }
  *out_undeleted = 0;
  *out_skipped = 0;
  return checked("unknown exception while cleaning deleted records", [&] {
    const auto result = fo4::clean_deleted_records(plugin->value);
    *out_undeleted = result.undeleted;
    *out_skipped = result.skipped;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_remove_record(radicle_plugin* plugin, radicle_form_key key) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return checked("unknown exception while removing a record", [&] {
    fo4::assign_record_form_keys(plugin->value);
    if (fo4::remove_records(plugin->value, std::span<const fo4::FormKey>(&*form_key, 1)) == 0) {
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_renumber_form_id(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               uint32_t new_local_id,
                                               int32_t repoint_links,
                                               radicle_form_key* out_key) {
  clear_error();
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || out_key == nullptr || new_local_id > fo4::k_full_local_mask) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, a 24-bit new id and out_key are required");
  }
  *out_key = {nullptr, 0};
  return checked("unknown exception while renumbering a record", [&] {
    fo4::assign_record_form_keys(plugin->value);
    switch (fo4::renumber_local_record(plugin->value, *form_key, fo4::LocalFormId{new_local_id},
                                       repoint_links != 0)) {
      case fo4::RecordRenumberStatus::record_missing:
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      case fo4::RecordRenumberStatus::not_local:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "only records the plugin owns can be renumbered");
      case fo4::RecordRenumberStatus::target_exists:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "another record already holds the new id");
      case fo4::RecordRenumberStatus::renumbered:
        *out_key = {plugin->value.key.filename().data(), new_local_id};
        return RADICLE_STATUS_OK;
      case fo4::RecordRenumberStatus::unchanged:
        *out_key = {plugin->value.key.filename().data(), form_key->local().value()};
        return RADICLE_STATUS_OK;
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown renumber status");
  });
}

radicle_status radicle_plugin_renumber_all(radicle_plugin* plugin,
                                           uint32_t start_local_id,
                                           size_t* out_count) {
  clear_error();
  if (plugin == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_count are required");
  }
  *out_count = 0;
  return checked("unknown exception while renumbering the plugin", [&] {
    const auto result = fo4::renumber_all_local_records(plugin->value, start_local_id);
    switch (result.status) {
      case fo4::RenumberAllStatus::renumbered:
        *out_count = result.renumbered;
        return RADICLE_STATUS_OK;
      case fo4::RenumberAllStatus::unchanged:
      case fo4::RenumberAllStatus::no_records:
        return RADICLE_STATUS_OK;
      case fo4::RenumberAllStatus::blocked_nested_records:
        return fail_first("records nested in cell groups block the renumbering, first is ",
                          result.blockers.empty() ? fo4::FormKey{} : result.blockers.front());
      case fo4::RenumberAllStatus::out_of_range:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the renumbered ids would leave the plugin's local id range");
      case fo4::RenumberAllStatus::rekey_failed:
        return fail_first("a record could not be renumbered, first is ",
                          result.blockers.empty() ? fo4::FormKey{} : result.blockers.front());
    }
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown renumber-all status");
  });
}

radicle_status radicle_plugin_copy_record_version(radicle_plugin* plugin,
                                                  const radicle_env* env,
                                                  const radicle_record* version,
                                                  int32_t overwrite,
                                                  radicle_record_copy_status* out_status) {
  clear_error();
  if (plugin == nullptr || env == nullptr || version == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, version and out_status are required");
  }
  if (!belongs_to_env(env, to_ref(version))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  const auto& ref = *to_ref(version);
  return checked("unknown exception while copying a record version", [&] {
    auto next = plugin->value;
    radicle_record_copy_status copied = RADICLE_RECORD_COPY_INSERTED;
    const auto status = fo4::copy_record_as_override(next, env->value.plugin(ref.plugin),
                                                     ref.record->form_key, overwrite != 0);
    switch (status) {
      case fo4::RecordCopyStatus::inserted: copied = RADICLE_RECORD_COPY_INSERTED; break;
      case fo4::RecordCopyStatus::replaced: copied = RADICLE_RECORD_COPY_REPLACED; break;
      case fo4::RecordCopyStatus::already_exists: copied = RADICLE_RECORD_COPY_ALREADY_EXISTS; break;
      case fo4::RecordCopyStatus::source_missing:
        return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
      case fo4::RecordCopyStatus::source_containment_unsupported:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "source record's containment cannot be reproduced");
    }
    plugin->value = std::move(next);
    *out_status = copied;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_retarget_links(radicle_plugin* plugin,
                                             radicle_form_key record,
                                             radicle_form_key from_key,
                                             radicle_form_key to_key,
                                             size_t* out_changed) {
  clear_error();
  const auto record_key = to_form_key(record);
  const auto from = to_form_key(from_key);
  const auto to = to_form_key(to_key);
  if (plugin == nullptr || !record_key || !from || !to || out_changed == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a record, from, to and out_changed are required");
  }
  *out_changed = 0;
  return checked("unknown exception while retargeting links", [&] {
    fo4::assign_record_form_keys(plugin->value);
    if (fo4::find_record(plugin->value, *record_key) == nullptr) {
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    }
    *out_changed = fo4::rekey_record_form_links(plugin->value, *record_key, *from, *to);
    return RADICLE_STATUS_OK;
  });
}

}
