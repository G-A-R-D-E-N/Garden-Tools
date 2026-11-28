#include "fo4/patch/delta_patch.hpp"

#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"

#include <string>
#include <vector>

namespace fo4::patch {
namespace {

bool same_stored_record(const Record& left, const Record& right) {
  if (!(left.header.signature == right.header.signature)) return false;
  if (left.header.form_version != right.header.form_version) return false;
  if (!(left.header.flags == right.header.flags)) return false;
  if (left.subrecords.size() != right.subrecords.size()) return false;
  for (std::size_t index = 0; index < left.subrecords.size(); ++index) {
    const auto& left_subrecord = left.subrecords[index];
    const auto& right_subrecord = right.subrecords[index];
    if (!(left_subrecord.signature == right_subrecord.signature)) return false;
    if (left_subrecord.data != right_subrecord.data) return false;
  }
  return true;
}

std::size_t loaded_index(const env::Environment& env, const PluginKey& key) {
  const auto index = env.find_plugin(key.filename());
  if (!index.has_value()) {
    throw DeltaPatchError("plugin is not loaded: " + std::string(key.filename()));
  }
  return *index;
}

}  // namespace

DeltaPatchResult build_delta_patch(const env::Environment& env,
                                   const PluginKey& older,
                                   const PluginKey& newer) {
  const auto older_index = loaded_index(env, older);
  const auto newer_index = loaded_index(env, newer);
  if (older_index == newer_index) {
    throw DeltaPatchError("the delta patch needs two different plugin files");
  }

  const auto& older_plugin = env.plugin(older_index);
  DeltaPatchResult result;
  result.patch = env.plugin(newer_index);

  std::vector<FormKey> identical;
  for (const auto* record : collect_records(result.patch)) {
    if (record->header.signature == sig_tes4) continue;
    if (record->undecodable) continue;
    const auto previous_key = FormKey{older, record->form_key.local()};
    const auto* previous = find_record(older_plugin, previous_key);
    if (previous == nullptr) continue;
    if (same_stored_record(*record, *previous)) identical.push_back(record->form_key);
  }

  result.counts.removed = remove_records(result.patch, identical);
  for (const auto* record : collect_records(result.patch)) {
    if (record->header.signature != sig_tes4) ++result.counts.kept;
  }
  return result;
}

}  // namespace fo4::patch
