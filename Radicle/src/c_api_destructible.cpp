#include "c_api_env_internal.hpp"
#include "fo4/c_api_destructible.h"
#include "fo4/plugin/destructible_stage.hpp"

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status missing_record() {
  return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
}

radicle_status record_mismatch() {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type cannot carry destructible stages");
}

radicle_status map_header(fo4::DestructibleHeaderStatus status,
                          radicle_destructible_header_status& out) {
  switch (status) {
    case fo4::DestructibleHeaderStatus::updated:
      out = RADICLE_DESTRUCTIBLE_HEADER_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleHeaderStatus::record_missing:
      return missing_record();
    case fo4::DestructibleHeaderStatus::record_mismatch:
      return record_mismatch();
    case fo4::DestructibleHeaderStatus::header_present:
      out = RADICLE_DESTRUCTIBLE_HEADER_PRESENT;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleHeaderStatus::header_missing:
      out = RADICLE_DESTRUCTIBLE_HEADER_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleHeaderStatus::unknown_order:
      out = RADICLE_DESTRUCTIBLE_HEADER_UNKNOWN_ORDER;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleHeaderStatus::too_many_stages:
      out = RADICLE_DESTRUCTIBLE_HEADER_TOO_MANY_STAGES;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected destructible header status");
}

radicle_status map_stage(fo4::DestructibleStageMutationStatus status,
                         radicle_destructible_stage_status& out) {
  switch (status) {
    case fo4::DestructibleStageMutationStatus::updated:
      out = RADICLE_DESTRUCTIBLE_STAGE_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMutationStatus::record_missing:
      return missing_record();
    case fo4::DestructibleStageMutationStatus::record_mismatch:
      return record_mismatch();
    case fo4::DestructibleStageMutationStatus::header_missing:
      out = RADICLE_DESTRUCTIBLE_STAGE_HEADER_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMutationStatus::index_out_of_range:
      out = RADICLE_DESTRUCTIBLE_STAGE_INDEX_OUT_OF_RANGE;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMutationStatus::too_many_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_TOO_MANY_STAGES;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMutationStatus::interleaved_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_INTERLEAVED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMutationStatus::malformed_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_MALFORMED;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected destructible stage status");
}

radicle_status map_member(fo4::DestructibleStageMemberStatus status,
                          radicle_destructible_stage_member_status& out) {
  switch (status) {
    case fo4::DestructibleStageMemberStatus::updated:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::record_missing:
      return missing_record();
    case fo4::DestructibleStageMemberStatus::record_mismatch:
      return record_mismatch();
    case fo4::DestructibleStageMemberStatus::header_missing:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_HEADER_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::stage_index_out_of_range:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_STAGE_INDEX_OUT_OF_RANGE;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::not_optional_member:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_NOT_OPTIONAL;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::no_default:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_NO_DEFAULT;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::member_present:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_PRESENT;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::member_absent:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_ABSENT;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::model_required:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_MODEL_REQUIRED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::interleaved_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_INTERLEAVED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleStageMemberStatus::malformed_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_MEMBER_MALFORMED;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected destructible stage member status");
}

radicle_status map_group_read(fo4::DestructibleGroupReadStatus status,
                              radicle_destructible_stage_status& out) {
  switch (status) {
    case fo4::DestructibleGroupReadStatus::present:
      out = RADICLE_DESTRUCTIBLE_STAGE_PRESENT;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleGroupReadStatus::record_missing:
      return missing_record();
    case fo4::DestructibleGroupReadStatus::record_mismatch:
      return record_mismatch();
    case fo4::DestructibleGroupReadStatus::header_missing:
      out = RADICLE_DESTRUCTIBLE_STAGE_HEADER_MISSING;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleGroupReadStatus::interleaved_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_INTERLEAVED;
      return RADICLE_STATUS_OK;
    case fo4::DestructibleGroupReadStatus::malformed_stages:
      out = RADICLE_DESTRUCTIBLE_STAGE_MALFORMED;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected destructible group status");
}

char* copy_plugin_name(std::string_view name) {
  if (name.empty()) return nullptr;
  auto* copy = static_cast<char*>(std::malloc(name.size() + 1));
  if (copy == nullptr) return nullptr;
  std::memcpy(copy, name.data(), name.size());
  copy[name.size()] = '\0';
  return copy;
}

void free_group(radicle_destructible_group& group) {
  if (group.stages != nullptr) {
    for (size_t stage_index = 0; stage_index < group.stage_count; ++stage_index) {
      std::free(const_cast<radicle_destructible_member*>(group.stages[stage_index].members));
    }
    std::free(const_cast<radicle_destructible_stage*>(group.stages));
  }
  if (group.resistances != nullptr) {
    for (size_t index = 0; index < group.resistance_count; ++index) {
      std::free(const_cast<char*>(group.resistances[index].damage_type.plugin));
    }
    std::free(const_cast<radicle_destructible_resistance*>(group.resistances));
  }
  group = {};
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_destructible_group_read(
    const radicle_plugin* plugin, radicle_form_key record,
    radicle_destructible_group* out_group, radicle_destructible_stage_status* out_status) {
  clear_error();
  if (plugin == nullptr || out_group == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, out_group and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  *out_group = {};
  *out_status = RADICLE_DESTRUCTIBLE_STAGE_PRESENT;
  return guarded("unknown exception while reading a destructible group", [&] {
    fo4::DestructibleGroupView view;
    const auto status = fo4::read_destructible_group(plugin->value, *key, view);
    if (status != fo4::DestructibleGroupReadStatus::present) {
      return map_group_read(status, *out_status);
    }

    radicle_destructible_group result{};
    // Constraint: calloc may answer a zero-size request with a live block, so an empty list allocates nothing; dropping that block would leak it on every read.
    auto* stages = view.stages.empty()
                       ? nullptr
                       : static_cast<radicle_destructible_stage*>(
                             std::calloc(view.stages.size(), sizeof(radicle_destructible_stage)));
    if (!view.stages.empty() && stages == nullptr) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate destructible stages");
    }
    result.stages = stages;
    result.stage_count = view.stages.size();
    for (size_t stage_index = 0; stage_index < view.stages.size(); ++stage_index) {
      const auto& source = view.stages[stage_index].members;
      if (source.empty()) continue;
      auto* members = static_cast<radicle_destructible_member*>(
          std::calloc(source.size(), sizeof(radicle_destructible_member)));
      if (members == nullptr) {
        free_group(result);
        return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate destructible stage members");
      }
      stages[stage_index].members = members;
      stages[stage_index].member_count = source.size();
      for (size_t member_index = 0; member_index < source.size(); ++member_index) {
        const auto& member = source[member_index];
        const auto signature = member.signature.view();
        std::memcpy(members[member_index].signature, signature.data(), 4);
        members[member_index].signature[4] = '\0';
        members[member_index].data = reinterpret_cast<const uint8_t*>(member.data.data());
        members[member_index].size = member.data.size();
      }
    }

    auto* resistances =
        view.resistances.empty()
            ? nullptr
            : static_cast<radicle_destructible_resistance*>(std::calloc(
                  view.resistances.size(), sizeof(radicle_destructible_resistance)));
    if (!view.resistances.empty() && resistances == nullptr) {
      free_group(result);
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate destructible resistances");
    }
    result.resistances = resistances;
    result.resistance_count = view.resistances.size();
    for (size_t index = 0; index < view.resistances.size(); ++index) {
      const auto& source = view.resistances[index];
      auto* name = copy_plugin_name(source.damage_type.owner().filename());
      if (!source.damage_type.owner().empty() && name == nullptr) {
        free_group(result);
        return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate destructible resistance owner");
      }
      auto& resistance = resistances[index];
      resistance.damage_type.plugin = name;
      resistance.damage_type.local_id = source.damage_type.local().value();
      resistance.value = source.value;
    }

    result.header.health = view.health;
    result.header.stage_count = view.stage_count;
    result.header.flags = view.flags;
    *out_group = result;
    return RADICLE_STATUS_OK;
  });
}

void radicle_free_destructible_group(radicle_destructible_group* group) {
  clear_error();
  if (group != nullptr) free_group(*group);
}

radicle_status radicle_plugin_insert_destructible_header(
    radicle_plugin* plugin, radicle_form_key record,
    radicle_destructible_header_status* out_status) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  return guarded("unknown exception while inserting a destructible header", [&] {
    return map_header(fo4::insert_destructible_header(plugin->value, *key), *out_status);
  });
}

radicle_status radicle_plugin_remove_destructible_header(
    radicle_plugin* plugin, radicle_form_key record,
    radicle_destructible_header_status* out_status) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  return guarded("unknown exception while removing a destructible header", [&] {
    return map_header(fo4::remove_destructible_header(plugin->value, *key), *out_status);
  });
}

radicle_status radicle_plugin_insert_destructible_stage(
    radicle_plugin* plugin, radicle_form_key record, size_t index,
    radicle_destructible_stage_status* out_status) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  return guarded("unknown exception while inserting a destructible stage", [&] {
    return map_stage(fo4::insert_destructible_stage(plugin->value, *key, index), *out_status);
  });
}

radicle_status radicle_plugin_remove_destructible_stage(
    radicle_plugin* plugin, radicle_form_key record, size_t index,
    radicle_destructible_stage_status* out_status) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  return guarded("unknown exception while removing a destructible stage", [&] {
    return map_stage(fo4::remove_destructible_stage(plugin->value, *key, index), *out_status);
  });
}

radicle_status radicle_plugin_insert_destructible_stage_member(
    radicle_plugin* plugin, radicle_form_key record, size_t stage_index, const char* member,
    radicle_destructible_stage_member_status* out_status) {
  clear_error();
  if (plugin == nullptr || member == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, member and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  const auto signature = to_signature(member);
  if (!signature) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "member must be a four-character subrecord code");
  }
  return guarded("unknown exception while inserting a destructible stage member", [&] {
    return map_member(
        fo4::insert_destructible_stage_member(plugin->value, *key, stage_index, *signature),
        *out_status);
  });
}

radicle_status radicle_plugin_remove_destructible_stage_member(
    radicle_plugin* plugin, radicle_form_key record, size_t stage_index, const char* member,
    radicle_destructible_stage_member_status* out_status) {
  clear_error();
  if (plugin == nullptr || member == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, member and out_status are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  const auto signature = to_signature(member);
  if (!signature) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "member must be a four-character subrecord code");
  }
  return guarded("unknown exception while removing a destructible stage member", [&] {
    return map_member(
        fo4::remove_destructible_stage_member(plugin->value, *key, stage_index, *signature),
        *out_status);
  });
}

}
