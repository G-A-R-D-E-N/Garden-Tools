#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/visit.hpp"

namespace fo4::detail {

inline Record* find_mutable_record(Plugin& plugin, const FormKey& record_key) {
  Record* target = nullptr;
  visit_records(plugin, [&](Record& record) {
    if (target == nullptr && record.header.signature != sig_tes4 &&
        record.form_key == record_key) {
      target = &record;
    }
  });
  return target;
}

inline void mark_record_modified(Plugin& plugin, Record& record) {
  record.original_range.clear();
  record.decompressed_payload.clear();
  record.header.data_size = 0;
  record.uncompressed_size = 0;
  plugin.original_range.clear();
}

}  // namespace fo4::detail
