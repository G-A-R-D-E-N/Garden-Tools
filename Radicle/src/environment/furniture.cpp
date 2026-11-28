#include "fo4/environment/furniture.hpp"

#include "record_reader.hpp"

namespace fo4::env {
namespace {

using detail::Reader;
}  // namespace

std::vector<RaceSubgraphRow> decode_race_subgraphs(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref, sig("RACE"));
  std::vector<RaceSubgraphRow> rows;
  RaceSubgraphRow row;
  bool open = false;
  for (const auto& sub : ref.record->subrecords) {
    const auto tag = sub.signature;
    if (tag == sig("SAKD") || tag == sig("STKD")) {
      if (sub.data.size() != 4) r.fail(tag, "must be 4 bytes");
      (tag == sig("SAKD") ? row.actor_keywords : row.target_keywords).push_back(r.link_at(sub, 0));
      open = true;
    } else if (tag == sig("SGNM")) {
      const auto value = zstring_view(sub);
      if (!value || row.behavior_graph) r.fail(tag, "invalid or duplicate stored string");
      row.behavior_graph = *value;
      open = true;
    } else if (tag == sig("SAPT")) {
      const auto value = zstring_view(sub);
      if (!value) r.fail(tag, "invalid stored string");
      row.path_prefixes.push_back(*value);
      open = true;
    } else if (tag == sig("SRAF")) {
      if (sub.data.size() != 4) r.fail(tag, "must be 4 bytes");
      row.role = Reader::u16(sub, 0);
      row.perspective = Reader::u16(sub, 2);
      rows.push_back(std::move(row));
      row = {};
      open = false;
    }
  }
  if (open) r.fail(sig("SRAF"), "subgraph block is not closed");
  return rows;
}

std::vector<FormKey> decode_furniture_keywords(const Environment& env, const RecordRef& ref) {
  static_cast<void>(Reader(env, ref, sig("FURN")));
  return decode_record_keywords(env, ref);
}

std::vector<FormKey> decode_record_keywords(const Environment& env, const RecordRef& ref) {
  const Reader r(env, ref);
  const auto* kwda = r.unique(sig("KWDA"));
  const auto* ksiz = r.sized(sig("KSIZ"), 4);
  std::vector<FormKey> out;
  if (kwda == nullptr) {
    if (ksiz != nullptr && Reader::u32(*ksiz, 0) != 0) r.fail(sig("KSIZ"), "count without KWDA");
    return out;
  }
  if (kwda->data.size() % 4 != 0 || (ksiz != nullptr && Reader::u32(*ksiz, 0) != kwda->data.size() / 4)) {
    r.fail(sig("KWDA"), "size does not match KSIZ");
  }
  for (std::size_t at = 0; at < kwda->data.size(); at += 4) out.push_back(r.link_at(*kwda, at));
  return out;
}

}  // namespace fo4::env
