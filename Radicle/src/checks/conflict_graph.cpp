#include "fo4/checks/conflict_graph.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace fo4 {
namespace {

const std::vector<RecordVersion> k_empty_versions;

bool has_identity(const FormKey& key) {
  return !key.owner().empty();
}

std::uint32_t checked_u32(std::size_t value) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("reference index exceeds 32-bit limits");
  }
  return static_cast<std::uint32_t>(value);
}

// Invariant: stable, so entries of one key keep link position order.
template <class Links, class KeyOf>
void group_by_key(const Links& links, std::size_t key_count, KeyOf key_of,
                  std::vector<std::uint32_t>& offsets, std::vector<std::uint32_t>& order) {
  offsets.assign(key_count + 1, 0);
  for (const auto& link : links) ++offsets[static_cast<std::size_t>(key_of(link)) + 1];
  std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
  order.resize(links.size());
  std::vector<std::uint32_t> cursor(offsets.begin(), offsets.end() - 1);
  for (std::size_t i = 0; i < links.size(); ++i) {
    order[cursor[key_of(links[i])]++] = static_cast<std::uint32_t>(i);
  }
}

// Requires plugin_begin to end with links.size(), so every link position has a plugin slot.
template <class Index>
std::size_t plugin_of(const Index& index, std::uint32_t position) {
  const auto it = std::upper_bound(index.plugin_begin.begin(), index.plugin_begin.end(), position);
  return static_cast<std::size_t>(it - index.plugin_begin.begin()) - 1;
}

template <class Hit, class Index, class MakeHit>
std::vector<Hit> hits_for(const Index& index, const FormKey& key,
                          const std::vector<std::uint32_t>& offsets,
                          const std::vector<std::uint32_t>& order, MakeHit make_hit) {
  std::vector<Hit> hits;
  const auto found = std::lower_bound(index.keys.begin(), index.keys.end(), key);
  if (found == index.keys.end() || !(*found == key)) return hits;
  const auto id = static_cast<std::size_t>(found - index.keys.begin());
  hits.reserve(offsets[id + 1] - offsets[id]);
  for (std::size_t i = offsets[id]; i < offsets[id + 1]; ++i) {
    const auto position = order[i];
    hits.push_back(make_hit(index.links[position], plugin_of(index, position)));
  }
  return hits;
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

ConflictGraph::ReferenceIndex ConflictGraph::build_reference_index(
    std::span<const Plugin* const> plugins) {
  ReferenceIndex index;
  std::vector<FormKey> interned;
  index.plugin_begin.reserve(plugins.size() + 1);
  {
    std::unordered_map<FormKey, std::uint32_t> ids;
    const auto intern = [&](const FormKey& key) {
      const auto [it, inserted] = ids.try_emplace(key, 0u);
      if (inserted) {
        it->second = checked_u32(interned.size());
        interned.push_back(key);
      }
      return it->second;
    };
    for (const auto* plugin : plugins) {
      index.plugin_begin.push_back(checked_u32(index.links.size()));
      if (plugin == nullptr) {
        continue;
      }
      for (const auto& hit : collect_form_links(*plugin)) {
        if (!has_identity(hit.key) || !has_identity(hit.record)) {
          continue;
        }
        const auto source = intern(hit.record);
        const auto target = intern(hit.key);
        index.links.push_back({source, target, hit.subrecord.packed(), checked_u32(hit.offset)});
      }
    }
  }
  index.plugin_begin.push_back(checked_u32(index.links.size()));
  index.links.shrink_to_fit();

  std::vector<std::uint32_t> rank(interned.size());
  {
    std::vector<std::uint32_t> order(interned.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(),
              [&](std::uint32_t a, std::uint32_t b) { return interned[a] < interned[b]; });
    index.keys.reserve(interned.size());
    for (std::size_t sorted = 0; sorted < order.size(); ++sorted) {
      rank[order[sorted]] = static_cast<std::uint32_t>(sorted);
      index.keys.push_back(interned[order[sorted]]);
    }
  }
  std::vector<FormKey>().swap(interned);
  for (auto& link : index.links) {
    link.source = rank[link.source];
    link.target = rank[link.target];
  }
  std::vector<std::uint32_t>().swap(rank);

  group_by_key(index.links, index.keys.size(), [](const Link& link) { return link.target; },
               index.inbound_offsets, index.inbound_links);
  group_by_key(index.links, index.keys.size(), [](const Link& link) { return link.source; },
               index.outbound_offsets, index.outbound_links);
  return index;
}

void ConflictGraph::ensure_references() const {
  if (referencesBuilt_) return;
  references_ = build_reference_index(plugins_);
  referencesBuilt_ = true;
}

void ConflictGraph::release_references() const {
  std::scoped_lock lock(*referencesMutex_);
  references_ = ReferenceIndex{};
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

std::vector<ReferencedByHit> ConflictGraph::referenced_by(const FormKey& key) const {
  std::scoped_lock lock(*referencesMutex_);
  ensure_references();
  return hits_for<ReferencedByHit>(
      references_, key, references_.inbound_offsets, references_.inbound_links,
      [&](const Link& link, std::size_t plugin_index) {
        return ReferencedByHit{references_.keys[link.source], plugins_[plugin_index]->key,
                               Signature::from_bytes(link.subrecord), link.offset, plugin_index};
      });
}

bool ConflictGraph::has_references(const FormKey& key) const {
  std::scoped_lock lock(*referencesMutex_);
  ensure_references();
  const auto found = std::lower_bound(references_.keys.begin(), references_.keys.end(), key);
  if (found == references_.keys.end() || !(*found == key)) return false;
  const auto id = static_cast<std::size_t>(found - references_.keys.begin());
  return references_.inbound_offsets[id + 1] != references_.inbound_offsets[id];
}

std::vector<OutgoingReferenceHit> ConflictGraph::references_from(const FormKey& key) const {
  std::scoped_lock lock(*referencesMutex_);
  ensure_references();
  return hits_for<OutgoingReferenceHit>(
      references_, key, references_.outbound_offsets, references_.outbound_links,
      [&](const Link& link, std::size_t plugin_index) {
        return OutgoingReferenceHit{references_.keys[link.target], plugins_[plugin_index]->key,
                                    Signature::from_bytes(link.subrecord), link.offset,
                                    plugin_index};
      });
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
