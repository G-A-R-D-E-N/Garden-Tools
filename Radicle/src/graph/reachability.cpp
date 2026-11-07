#include "fo4/graph/reachability.hpp"

#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <variant>

namespace fo4 {
namespace {

constexpr std::uint32_t k_first_non_hardcoded_id = 0x800;
constexpr std::uint16_t k_race_weights_form_version = 109;
constexpr std::string_view k_game_master = "Fallout4.esm";

Signature sig(const char* text) { return Signature::from_string(text); }

struct Link {
  FormKey source;
  Signature source_signature{};
  Signature subrecord{};
};

struct Graph {
  std::map<FormKey, const Record*> winners;
  std::map<FormKey, std::vector<FormKey>> outgoing;
  std::map<FormKey, std::vector<Link>> incoming;
  std::map<FormKey, std::vector<FormKey>> children;
  std::map<FormKey, FormKey> parent;
};

bool is_owned_child_group(const GroupChild& child) {
  const auto* group = std::get_if<Group>(&child);
  if (group == nullptr) return false;
  const auto type = group->header.group_type;
  return type == static_cast<std::int32_t>(GroupType::world_children) ||
         type == static_cast<std::int32_t>(GroupType::cell_children) ||
         type == static_cast<std::int32_t>(GroupType::topic_children);
}

void index_group(const Group& group, const FormKey* owner, Graph& graph) {
  for (std::size_t i = 0; i < group.children.size(); ++i) {
    if (const auto* sub = std::get_if<Group>(&group.children[i])) {
      index_group(*sub, owner, graph);
      continue;
    }
    const auto& record = std::get<Record>(group.children[i]);
    if (owner != nullptr) {
      graph.children[*owner].push_back(record.form_key);
      graph.parent[record.form_key] = *owner;
    }
    if (i + 1 < group.children.size() && is_owned_child_group(group.children[i + 1])) {
      index_group(std::get<Group>(group.children[i + 1]), &record.form_key, graph);
      ++i;
    }
  }
}

const Subrecord* find_sub(const Record& record, Signature signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

std::uint32_t read_le(const Subrecord& sub, std::size_t offset, std::size_t width) {
  if (sub.data.size() < offset + width) return 0;
  std::uint32_t value = 0;
  std::memcpy(&value, sub.data.data() + offset, width);
  return value;
}

bool is_root(const Record& record) {
  static const std::set<Signature> whole_groups{
      sig("ADDN"), sig("ANIO"), sig("AVIF"), sig("CAMS"), sig("COBJ"), sig("CPTH"), sig("DFOB"),
      sig("DLVW"), sig("DOBJ"), sig("GMST"), sig("IDLE"), sig("LSCR"), sig("NAVI"), sig("RADS")};
  const auto signature = record.header.signature;
  if (whole_groups.contains(signature)) return true;
  if (signature == sig("HDPT")) {
    const auto* data = find_sub(record, sig("DATA"));
    return data != nullptr && (read_le(*data, 0, 1) & 0x1) != 0;
  }
  if (signature == sig("PERK")) {
    const auto* data = find_sub(record, sig("DATA"));
    return data != nullptr && data->data.size() >= 4 && read_le(*data, 3, 1) != 0;
  }
  if (signature == sig("NPC_")) {
    const auto* acbs = find_sub(record, sig("ACBS"));
    return acbs != nullptr && (read_le(*acbs, 0, 4) & 0x4) != 0;
  }
  if (signature == sig("QUST")) {
    const auto* dnam = find_sub(record, sig("DNAM"));
    return dnam != nullptr && (read_le(*dnam, 0, 2) & 0x1) != 0;
  }
  if (signature == sig("RACE")) {
    const auto* data = find_sub(record, sig("DATA"));
    const std::size_t offset = record.header.form_version >= k_race_weights_form_version ? 32 : 8;
    return data != nullptr && (read_le(*data, offset, 4) & 0x1) != 0;
  }
  return false;
}

bool is_complex(Signature signature) {
  return signature == sig("DIAL") || signature == sig("WRLD") || signature == sig("CELL") || signature == sig("DOBJ");
}

const Subrecord* nth_sub(const Record& record, Signature signature, std::size_t ordinal) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature && ordinal-- == 0) return &sub;
  }
  return nullptr;
}

bool no_reach(const Record& record, const FormLinkHit& hit) {
  static const std::set<std::pair<Signature, Signature>> whole_subrecords{
      {sig("DIAL"), sig("QNAM")}, {sig("SMBN"), sig("PNAM")}, {sig("SMBN"), sig("SNAM")},
      {sig("SMQN"), sig("PNAM")}, {sig("SMQN"), sig("SNAM")}, {sig("SMEN"), sig("PNAM")},
      {sig("SMEN"), sig("SNAM")}, {sig("DLBR"), sig("QNAM")}, {sig("DLVW"), sig("QNAM")},
      {sig("DLVW"), sig("BNAM")}, {sig("INFO"), sig("TPIC")}, {sig("INFO"), sig("PNAM")},
      {sig("COBJ"), sig("BNAM")}, {sig("REGN"), sig("WNAM")}};
  const auto owner = record.header.signature;
  const auto sub = hit.subrecord;
  if (whole_subrecords.contains({owner, sub})) return true;
  if (sub == sig("CTDA")) return hit.offset == 12 || hit.offset == 16 || hit.offset == 24;
  if (sub == sig("COED") || sub == sig("XOWN")) return hit.offset == 0;
  if (sub == sig("XLOC")) return hit.offset == 4;
  if ((owner == sig("ECZN") && sub == sig("DATA")) || (owner == sig("FACT") && sub == sig("XNAM"))) {
    return hit.offset == 0;
  }
  if ((owner == sig("MISC") && sub == sig("CVPA")) || (owner == sig("COBJ") && sub == sig("FVPA"))) {
    return hit.offset % 8 == 0;
  }
  if (sub == sig("PLDT") || sub == sig("PLVD") || sub == sig("PTDA")) {
    const auto* data = nth_sub(record, sub, hit.subrecord_ordinal);
    if (hit.offset != 4 || data == nullptr || data->data.size() < 4) return false;
    const auto type = read_le(*data, 0, 4);
    return type == 0 || type == 1 || (sub != sig("PTDA") && type == 4);
  }
  return false;
}

Graph build_graph(std::span<const Plugin* const> load_order) {
  Graph graph;
  std::map<FormKey, std::size_t> winner_plugin;
  for (std::size_t index = 0; index < load_order.size(); ++index) {
    if (load_order[index] == nullptr) continue;
    for (const auto* record : collect_first_records(*load_order[index])) {
      if (record->header.signature == sig_tes4) continue;
      graph.winners[record->form_key] = record;
      winner_plugin[record->form_key] = index;
    }
    for (const auto& group : load_order[index]->groups) index_group(group, nullptr, graph);
  }
  for (std::size_t index = 0; index < load_order.size(); ++index) {
    if (load_order[index] == nullptr) continue;
    std::map<FormKey, const Record*> versions;
    for (const auto* record : collect_records(*load_order[index])) versions.try_emplace(record->form_key, record);
    for (const auto& hit : collect_form_links(*load_order[index])) {
      const auto version = versions.find(hit.record);
      const auto owner = winner_plugin.find(hit.record);
      if (version == versions.end() || owner == winner_plugin.end() || hit.key.owner().empty()) continue;
      const auto& record = *version->second;
      if (owner->second != index && !is_complex(record.header.signature)) continue;
      if (!no_reach(record, hit)) graph.outgoing[hit.record].push_back(hit.key);
      graph.incoming[hit.key].push_back({hit.record, record.header.signature, hit.subrecord});
    }
  }
  return graph;
}

void reach_reverse(const Graph& graph, const FormKey& key, Signature subrecord,
                   std::initializer_list<Signature> sources, const auto& reach) {
  const auto it = graph.incoming.find(key);
  if (it == graph.incoming.end()) return;
  for (const auto& link : it->second) {
    if (link.subrecord != subrecord && subrecord != Signature{}) continue;
    if (std::find(sources.begin(), sources.end(), link.source_signature) != sources.end()) reach(link.source);
  }
}

}  // namespace

std::vector<UnreachableRecord> find_unreachable(std::span<const Plugin* const> load_order,
                                                std::size_t plugin_index) {
  if (plugin_index >= load_order.size() || load_order[plugin_index] == nullptr) return {};
  const auto graph = build_graph(load_order);
  const auto game_master = PluginKey::from_filename(k_game_master);

  std::set<FormKey> reached;
  std::deque<FormKey> queue;
  const auto reach = [&](const FormKey& key) {
    if (reached.insert(key).second) queue.push_back(key);
  };
  for (const auto& [key, record] : graph.winners) {
    const bool hardcoded = key.owner() == game_master && key.local().value() < k_first_non_hardcoded_id;
    if (hardcoded || is_root(*record)) reach(key);
  }

  while (!queue.empty()) {
    const auto key = queue.front();
    queue.pop_front();
    const auto winner = graph.winners.find(key);
    if (winner == graph.winners.end()) continue;
    const auto signature = winner->second->header.signature;
    if (const auto out = graph.outgoing.find(key); out != graph.outgoing.end()) {
      for (const auto& target : out->second) reach(target);
    }
    if (const auto up = graph.parent.find(key); up != graph.parent.end()) reach(up->second);
    if (const auto down = graph.children.find(key); down != graph.children.end()) {
      for (const auto& child : down->second) reach(child);
    }
    if (signature == sig("NPC_")) reach_reverse(graph, key, Signature{}, {sig("RELA")}, reach);
    if (signature == sig("QUST")) {
      reach_reverse(graph, key, sig("PNAM"), {sig("SCEN")}, reach);
      reach_reverse(graph, key, sig("QNAM"), {sig("DLBR"), sig("DIAL")}, reach);
    }
    if (signature == sig("SMBN") || signature == sig("SMQN") || signature == sig("SMEN")) {
      reach_reverse(graph, key, sig("PNAM"), {sig("SMBN"), sig("SMQN"), sig("SMEN")}, reach);
    }
    if (signature == sig("FURN")) {
      const auto* wbdt = find_sub(*winner->second, sig("WBDT"));
      if (wbdt != nullptr && read_le(*wbdt, 0, 1) > 0) {
        if (const auto out = graph.outgoing.find(key); out != graph.outgoing.end()) {
          for (const auto& keyword : out->second) {
            reach_reverse(graph, keyword, sig("BNAM"), {sig("COBJ")}, reach);
          }
        }
      }
    }
  }

  std::vector<UnreachableRecord> out;
  for (const auto* record : collect_first_records(*load_order[plugin_index])) {
    if (record->header.signature == sig_tes4 || reached.contains(record->form_key)) continue;
    out.push_back({record->form_key, record->header.signature});
  }
  return out;
}

}  // namespace fo4
