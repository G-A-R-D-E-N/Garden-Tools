#include "fo4/graph/conflict_graph.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"

#include <utility>

namespace fo4 {
namespace {

const std::vector<RecordVersion> k_empty_versions;
const std::vector<ReferencedByHit> k_empty_references;
const std::vector<OutgoingReferenceHit> k_empty_outgoing_references;

bool has_identity(const FormKey& key) {
  return !key.owner().empty();
}

}  // namespace

ConflictGraph ConflictGraph::build(std::span<const Plugin* const> load_order) {
  ConflictGraph graph;
  graph.plugins_.assign(load_order.begin(), load_order.end());

  for (std::size_t index = 0; index < load_order.size(); ++index) {
    const auto* plugin = load_order[index];
    if (plugin == nullptr) {
      continue;
    }

    for (const auto* record : collect_first_records(*plugin)) {
      if (!has_identity(record->form_key)) {
        continue;
      }
      graph.records_[record->form_key].push_back({
          record->form_key,
          plugin->key,
          record->header.signature,
          index,
          is_partial_form(*record),
      });
    }
  }

  return graph;
}

void ConflictGraph::ensure_references() const {
  std::scoped_lock lock(*referencesMutex_);
  if (referencesBuilt_) return;
  decltype(inbound_) inbound;
  decltype(outbound_) outbound;
  for (std::size_t index = 0; index < plugins_.size(); ++index) {
    const auto* plugin = plugins_[index];
    if (plugin == nullptr) {
      continue;
    }
    for (const auto& link : collect_form_links(*plugin)) {
      if (!has_identity(link.key) || !has_identity(link.record)) {
        continue;
      }
      inbound[link.key].push_back({
          link.record,
          plugin->key,
          link.subrecord,
          link.offset,
          index,
      });
      outbound[link.record].push_back({
          link.key,
          plugin->key,
          link.subrecord,
          link.offset,
          index,
      });
    }
  }
  inbound_ = std::move(inbound);
  outbound_ = std::move(outbound);
  referencesBuilt_ = true;
}

void ConflictGraph::release_references() const {
  std::scoped_lock lock(*referencesMutex_);
  inbound_.clear();
  outbound_.clear();
  referencesBuilt_ = false;
}

bool ConflictGraph::references_built() const {
  std::scoped_lock lock(*referencesMutex_);
  return referencesBuilt_;
}

const std::vector<RecordVersion>& ConflictGraph::versions(const FormKey& key) const noexcept {
  const auto it = records_.find(key);
  return it == records_.end() ? k_empty_versions : it->second;
}

const RecordVersion* ConflictGraph::winner(const FormKey& key) const noexcept {
  const auto& entries = versions(key);
  if (entries.empty()) return nullptr;
  for (auto it = entries.rbegin(); it + 1 != entries.rend(); ++it) {
    if (!it->partial_form) return &*it;
  }
  return &entries.front();
}

const std::vector<ReferencedByHit>& ConflictGraph::referenced_by(const FormKey& key) const {
  ensure_references();
  const auto it = inbound_.find(key);
  return it == inbound_.end() ? k_empty_references : it->second;
}

const std::vector<OutgoingReferenceHit>& ConflictGraph::references_from(
    const FormKey& key) const {
  ensure_references();
  const auto it = outbound_.find(key);
  return it == outbound_.end() ? k_empty_outgoing_references : it->second;
}

std::vector<FormKey> ConflictGraph::conflicts() const {
  std::vector<FormKey> out;
  for (const auto& [key, versions] : records_) {
    if (versions.size() > 1) {
      out.push_back(key);
    }
  }
  return out;
}

std::vector<FormKey> ConflictGraph::all_keys() const {
  std::vector<FormKey> out;
  for (const auto& entry : records_) {
    out.push_back(entry.first);
  }
  return out;
}

}  // namespace fo4
