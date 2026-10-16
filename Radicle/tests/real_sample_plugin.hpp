#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test {

using Subs = std::vector<std::pair<std::string, std::vector<std::byte>>>;

inline Subs subs_of(const Plugin& plugin, const FormKey& key) {
  Subs out;
  if (const auto* record = find_record(plugin, key)) {
    for (const auto& sub : record->subrecords) { out.emplace_back(sub.signature.to_string(), sub.data); }
  }
  return out;
}

inline std::map<FormKey, std::vector<std::byte>> snapshot(const Plugin& plugin) {
  std::map<FormKey, std::vector<std::byte>> out;
  visit_records(plugin, [&](const Record& record) {
    if (record.header.signature == Signature::from_string("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

template <typename Pick>
Plugin make_sample(const Plugin& master, Pick&& pick, std::size_t bystander_stride) {
  auto sample = make_empty_plugin(master.key.filename());
  sample.header = master.header;
  sample.tes4 = master.tes4;
  sample.original_data = master.original_data;
  std::map<std::string, Group> groups;
  std::size_t other = 0;
  for (const auto* record : collect_records(master)) {
    if (!pick(*record) && (bystander_stride == 0 || other++ % bystander_stride != 0)) { continue; }
    const auto type = record->header.signature.to_string();
    auto& group = groups[type];
    for (std::size_t i = 0; i < group.header.label.size(); ++i) {
      group.header.label[i] = std::byte{static_cast<unsigned char>(type[i])};
    }
    group.children.emplace_back(*record);
  }
  for (auto& [type, group] : groups) { sample.groups.push_back(std::move(group)); }
  return read_plugin(write_plugin(sample), master.path);
}

}
