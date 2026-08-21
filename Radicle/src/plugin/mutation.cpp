#include "fo4/plugin/mutation.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

std::vector<std::byte> zstring(std::string_view value) {
  std::vector<std::byte> bytes;
  bytes.reserve(value.size() + 1);
  for (const char ch : value) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

}

bool set_record_editor_id(Plugin& plugin,
                          const FormKey& record_key,
                          std::string_view editor_id) {
  assign_record_form_keys(plugin);
  bool found = false;
  bool changed = false;

  visit_records(plugin, [&](Record& record) {
    if (found || record.header.signature == sig_tes4 || record.form_key != record_key) {
      return;
    }
    found = true;

    for (auto& subrecord : record.subrecords) {
      if (subrecord.signature != sig_edid) continue;
      const auto next = zstring(editor_id);
      if (subrecord.data == next) return;
      subrecord.data = next;
      record.original_bytes.clear();
      changed = true;
      return;
    }

    Subrecord edid;
    edid.signature = sig_edid;
    edid.data = zstring(editor_id);
    record.subrecords.insert(record.subrecords.begin(), std::move(edid));
    record.original_bytes.clear();
    changed = true;
  });

  if (changed) {
    plugin.original_bytes.clear();
    visit_groups(plugin, [](Group& group) { group.original_bytes.clear(); });
  }
  return found;
}

}  // namespace fo4
