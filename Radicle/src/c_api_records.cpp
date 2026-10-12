#include "c_api_env_internal.hpp"
#include "fo4/c_api_records.h"

using namespace radicle_c_env_detail;

namespace {

bool find_topic_group(const fo4::Group& group, const fo4::Record* record,
                      const fo4::Group*& out) {
  for (size_t i = 0; i < group.children.size(); ++i) {
    if (const auto* child = std::get_if<fo4::Group>(&group.children[i])) {
      if (find_topic_group(*child, record, out)) return true;
    } else if (std::get_if<fo4::Record>(&group.children[i]) == record) {
      if (i + 1 < group.children.size()) {
        const auto* next = std::get_if<fo4::Group>(&group.children[i + 1]);
        if (next != nullptr && next->header.group_type == static_cast<int32_t>(fo4::GroupType::topic_children)) out = next;
      }
      return true;
    }
  }
  return false;
}

}

extern "C" {

radicle_status radicle_record_topic_children(
    const radicle_env* env, const radicle_record* record,
    const radicle_record*** out_records, size_t* out_count) {
  clear_error();
  if (out_records != nullptr) *out_records = nullptr;
  if (out_count != nullptr) *out_count = 0;
  if (env == nullptr || record == nullptr || out_records == nullptr || out_count == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and outputs are required");
  return guarded("unknown exception while reading topic children", [&] {
    const auto& ref = *to_ref(record);
    if (ref.record->header.signature != fo4::Signature::from_string("DIAL"))
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record must be a DIAL");
    if (!belongs_to_env(env, &ref))
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
    const fo4::Group* children = nullptr;
    bool found = false;
    for (const auto& group : env->value.plugin(ref.plugin).groups) {
      if (find_topic_group(group, ref.record, children)) { found = true; break; }
    }
    if (!found) return fail(RADICLE_STATUS_NOT_FOUND, "topic record is absent from its plugin hierarchy");
    if (children == nullptr) return RADICLE_STATUS_OK;
    const fo4::RawFormId raw{children->header.label_as_u32()};
    if (raw.is_none() || raw.is_created() || raw.index_byte() > env->value.plugins().at(ref.plugin).masters.size() ||
        env->value.resolve_link(ref, raw) != ref.record->form_key)
      return fail(RADICLE_STATUS_PARSE_ERROR, "topic child group owner does not match the selected DIAL");
    std::vector<const fo4::env::RecordRef*> refs;
    refs.reserve(children->children.size());
    for (const auto& child : children->children) {
      const auto* info = std::get_if<fo4::Record>(&child);
      if (info == nullptr || info->header.signature != fo4::Signature::from_string("INFO"))
        return fail(RADICLE_STATUS_PARSE_ERROR, "topic child group must contain direct INFO records");
      const auto info_versions = env->value.versions(info->form_key);
      const auto selected = std::find_if(info_versions.begin(), info_versions.end(), [&](const auto& version) {
        return version.plugin == ref.plugin && version.record == info;
      });
      if (selected == info_versions.end()) return fail(RADICLE_STATUS_NOT_FOUND, "topic child is not indexed");
      refs.push_back(&*selected);
    }
    return copy_records(refs, out_records, out_count);
  });
}

radicle_status radicle_record_payload_info(
    const radicle_env* env, const radicle_record* record,
    uint16_t* out_form_version, size_t* out_subrecord_count) {
  clear_error();
  if (out_form_version != nullptr) *out_form_version = 0;
  if (out_subrecord_count != nullptr) *out_subrecord_count = 0;
  if (env == nullptr || record == nullptr || out_form_version == nullptr || out_subrecord_count == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and outputs are required");
  const auto* ref = to_ref(record);
  if (!belongs_to_env(env, ref))
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  const auto& value = *ref->record;
  *out_form_version = value.header.form_version;
  *out_subrecord_count = value.subrecords.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_record_copy_subrecord(
    const radicle_env* env, const radicle_record* record, size_t index,
    uint8_t* out_signature, uint8_t** out_bytes, size_t* out_size,
    int32_t* out_extended_size) {
  clear_error();
  if (out_bytes != nullptr) *out_bytes = nullptr;
  if (out_size != nullptr) *out_size = 0;
  if (out_extended_size != nullptr) *out_extended_size = 0;
  if (out_signature != nullptr) std::memset(out_signature, 0, 4);
  if (env == nullptr || record == nullptr || out_signature == nullptr || out_bytes == nullptr ||
      out_size == nullptr || out_extended_size == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and outputs are required");
  const auto* ref = to_ref(record);
  if (!belongs_to_env(env, ref))
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  return guarded("unknown exception while copying subrecord", [&] {
    const auto& subs = ref->record->subrecords;
    if (index >= subs.size()) return fail(RADICLE_STATUS_NOT_FOUND, "subrecord index is out of range");
    const auto& sub = subs[index];
    if (!sub.data.empty()) {
      auto* copy = static_cast<uint8_t*>(std::malloc(sub.data.size()));
      if (copy == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to copy subrecord bytes");
      std::memcpy(copy, sub.data.data(), sub.data.size());
      *out_bytes = copy;
    }
    std::memcpy(out_signature, sub.signature.bytes().data(), 4);
    *out_size = sub.data.size();
    *out_extended_size = sub.extended_size ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_resolve_form_id(
    const radicle_env* env, const radicle_record* record, uint32_t raw_form_id,
    radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (env == nullptr || record == nullptr || out_key == nullptr)
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_key are required");
  return guarded("unknown exception while resolving record FormID", [&] {
    const auto& ref = *to_ref(record);
    if (!belongs_to_env(env, &ref))
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
    const fo4::RawFormId raw{raw_form_id};
    if (raw.is_none()) return RADICLE_STATUS_OK;
    if (raw.is_created() || raw.index_byte() > env->value.plugins().at(ref.plugin).masters.size())
      return fail(RADICLE_STATUS_PARSE_ERROR, "FormID index is outside the record plugin's master table");
    *out_key = to_c_key(*env, env->value.resolve_link(ref, raw));
    return RADICLE_STATUS_OK;
  });
}

}
