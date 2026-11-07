#include "fo4/forms/faction.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "../plugin/record_edit.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_fact = Signature::from_string("FACT");
constexpr auto k_xnam = Signature::from_string("XNAM");
constexpr auto k_data = Signature::from_string("DATA");
constexpr auto k_rnam = Signature::from_string("RNAM");
constexpr auto k_mnam = Signature::from_string("MNAM");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_inam = Signature::from_string("INAM");
constexpr auto k_vend = Signature::from_string("VEND");
constexpr auto k_venc = Signature::from_string("VENC");
constexpr auto k_venv = Signature::from_string("VENV");
constexpr auto k_plvd = Signature::from_string("PLVD");
constexpr auto k_citc = Signature::from_string("CITC");
constexpr auto k_ctda = Signature::from_string("CTDA");
constexpr auto k_full = Signature::from_string("FULL");
constexpr auto k_crva = Signature::from_string("CRVA");
constexpr auto k_race = Signature::from_string("RACE");

constexpr std::size_t k_relation_size = 12;
constexpr std::size_t k_rank_number_size = 4;

constexpr std::uint32_t k_defined_flags = 0x0001FFFFu;

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void append_u32(std::vector<std::byte>& data, std::uint32_t value) {
  const auto offset = data.size();
  data.resize(offset + sizeof(value));
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

Subrecord make_subrecord(Signature signature, std::vector<std::byte> data) {
  Subrecord subrecord;
  subrecord.signature = signature;
  subrecord.data = std::move(data);
  return subrecord;
}

std::vector<std::byte> zstring(std::string_view value) {
  if (value.find('\0') != std::string_view::npos) {
    throw FactionError("FACT editor ID and full name cannot contain an embedded NUL");
  }
  std::vector<std::byte> data;
  data.reserve(value.size() + 1);
  for (const char ch : value) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  data.push_back(std::byte{0});
  return data;
}

std::vector<std::byte> payload_bytes(std::span<const std::uint8_t> values) {
  std::vector<std::byte> data;
  data.reserve(values.size());
  for (const auto value : values) data.push_back(std::byte{value});
  return data;
}

std::vector<std::byte> generated_default(Signature record, Signature subrecord) {
  const auto constraint = schema::record_authoring_constraint(record);
  if (!constraint.has_value()) {
    throw FactionError(record.to_string() + " authoring constraints are missing");
  }
  for (const auto& entry : schema::authoring_subrecords(*constraint)) {
    if (entry.signature == subrecord && entry.has_default()) {
      return payload_bytes(entry.default_bytes());
    }
  }
  throw FactionError(record.to_string() + " " + subrecord.to_string() + " authoring default is missing");
}

void require_reaction(std::uint32_t value) {
  if (value > static_cast<std::uint32_t>(FactionCombatReaction::friendly)) {
    throw FactionError("FACT XNAM group combat reaction is outside 0..3");
  }
}

void require_index(std::size_t index, std::size_t count, const char* label) {
  if (index >= count) throw FactionError(std::string(label) + " index is out of range");
}

int rank_member_stage(Signature signature) {
  if (signature == k_rnam) return 0;
  if (signature == k_mnam) return 1;
  if (signature == k_fnam) return 2;
  if (signature == k_inam) return 3;
  return -1;
}

bool is_rank_successor(Signature signature) {
  return signature == k_vend || signature == k_venc || signature == k_venv ||
         signature == k_plvd || signature == k_citc || signature == k_ctda;
}

template <typename Predicate>
std::size_t first_index_where(const Record& record, Predicate predicate) {
  std::size_t index = 0;
  while (index < record.subrecords.size() &&
         !predicate(record.subrecords[index].signature)) {
    ++index;
  }
  return index;
}

std::vector<std::size_t> relation_positions(const Record& record) {
  std::vector<std::size_t> positions;
  for (std::size_t index = 0; index < record.subrecords.size(); ++index) {
    if (record.subrecords[index].signature == k_xnam) positions.push_back(index);
  }
  return positions;
}

struct RankSpan {
  std::size_t begin;
  std::size_t end;
};

std::vector<RankSpan> rank_spans(const Record& record) {
  std::vector<RankSpan> spans;
  std::size_t index = 0;
  while (index < record.subrecords.size()) {
    if (rank_member_stage(record.subrecords[index].signature) != 0) {
      ++index;
      continue;
    }
    std::size_t end = index + 1;
    while (end < record.subrecords.size() &&
           rank_member_stage(record.subrecords[end].signature) > 0) {
      ++end;
    }
    spans.push_back({index, end});
    index = end;
  }
  return spans;
}

void insert_subrecords(Record& record, std::size_t at, std::vector<Subrecord> items) {
  record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(at),
                           items.begin(), items.end());
}

void erase_subrecords(Record& record, std::size_t begin, std::size_t end) {
  record.subrecords.erase(record.subrecords.begin() + static_cast<std::ptrdiff_t>(begin),
                          record.subrecords.begin() + static_cast<std::ptrdiff_t>(end));
}

const Record& faction_record(const Plugin& plugin, const FormKey& key) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) throw FactionError("FACT record not found: " + key.to_string());
  if (record->header.signature != k_fact) {
    throw FactionError("record is not FACT: " + key.to_string());
  }
  return *record;
}

void require_editable(const Plugin& plugin, const FormKey& key) {
  require_decodable(faction_record(plugin, key));
}

Record& mutable_faction(Plugin& next, const FormKey& key) {
  assign_record_form_keys(next);
  auto* record = detail::find_mutable_record(next, key);
  if (record == nullptr) throw FactionError("FACT record not found: " + key.to_string());
  require_decodable(*record);
  return *record;
}

void require_relation_target(const Plugin& plugin, const FormKey& faction) {
  if (faction.owner() != plugin.key) return;
  const auto* target = find_record(plugin, faction);
  if (target == nullptr) {
    throw FactionError("FACT XNAM faction must resolve to a record in this plugin: " +
                       faction.to_string());
  }
  if (target->header.signature != k_fact && target->header.signature != k_race) {
    throw FactionError("FACT XNAM faction target " + faction.to_string() + " is " +
                       target->header.signature.to_string() + ", not FACT or RACE");
  }
}

FactionRelation decode_relation(const Subrecord& subrecord,
                                const PluginFormIdContext& context) {
  if (subrecord.data.size() != k_relation_size) {
    throw FactionError("FACT XNAM must be exactly 12 bytes");
  }
  const auto raw = RawFormId{read_u32(subrecord.data, 0)};
  if (raw.is_null()) throw FactionError("FACT XNAM contains a null faction form ID");

  FactionRelation relation;
  try {
    relation.faction = resolve_plugin_form_id(raw, context);
  } catch (const FormIdError& error) {
    throw FactionError(std::string("invalid FACT XNAM faction form ID: ") + error.what());
  }
  relation.modifier = static_cast<std::int32_t>(read_u32(subrecord.data, 4));
  const auto reaction = read_u32(subrecord.data, 8);
  require_reaction(reaction);
  relation.reaction = static_cast<FactionCombatReaction>(reaction);
  return relation;
}

Subrecord encode_relation(const FactionRelation& relation,
                          const PluginFormIdContext& context) {
  require_reaction(static_cast<std::uint32_t>(relation.reaction));
  RawFormId raw{};
  try {
    raw = encode_plugin_form_id(relation.faction, context);
  } catch (const FormIdError& error) {
    throw FactionError(std::string("cannot encode FACT XNAM faction FormKey: ") + error.what());
  }
  if (raw.is_null()) throw FactionError("FACT XNAM faction cannot encode to a null form ID");

  std::vector<std::byte> data;
  data.reserve(k_relation_size);
  append_u32(data, raw.value());
  append_u32(data, static_cast<std::uint32_t>(relation.modifier));
  append_u32(data, static_cast<std::uint32_t>(relation.reaction));
  return make_subrecord(k_xnam, std::move(data));
}

std::vector<FactionRank> decode_ranks(const Plugin& plugin, const Record& record) {
  std::vector<FactionRank> ranks;
  bool in_rank = false;
  int last_stage = 0;
  for (const auto& subrecord : record.subrecords) {
    const auto stage = rank_member_stage(subrecord.signature);
    if (stage < 0) {
      in_rank = false;
      continue;
    }
    if (stage == 0) {
      if (subrecord.data.size() != k_rank_number_size) {
        throw FactionError("FACT RNAM must be exactly 4 bytes");
      }
      FactionRank rank;
      rank.number = read_u32(subrecord.data, 0);
      ranks.push_back(std::move(rank));
      in_rank = true;
      last_stage = 0;
      continue;
    }
    if (!in_rank || stage <= last_stage) {
      throw FactionError("FACT rank member is out of order: " + subrecord.signature.to_string());
    }
    last_stage = stage;
    auto& rank = ranks.back();
    if (stage == 1) {
      rank.male_title = schema::read_localized_string(plugin, k_fact, k_mnam, subrecord);
    } else if (stage == 2) {
      rank.female_title = schema::read_localized_string(plugin, k_fact, k_fnam, subrecord);
    } else {
      rank.insignia = subrecord.data;
    }
  }
  return ranks;
}

void require_string_form(const Plugin& plugin, const schema::LocalizedString& value) {
  if (value.raw_id.has_value()) return;
  if (value.localized() != plugin.header.tes4_flags.is_localized()) {
    throw FactionError(
        "FACT rank title must be a string ID in a localized plugin and inline text otherwise");
  }
  if (value.text.has_value() && value.text->find('\0') != std::string::npos) {
    throw FactionError("FACT rank title text contains a NUL");
  }
}

std::vector<Subrecord> encode_rank(const Plugin& plugin, const FactionRank& rank) {
  std::vector<std::byte> number;
  append_u32(number, rank.number);
  std::vector<Subrecord> encoded;
  encoded.push_back(make_subrecord(k_rnam, std::move(number)));
  if (rank.male_title.has_value()) {
    require_string_form(plugin, *rank.male_title);
    encoded.push_back(make_subrecord(k_mnam, schema::write_localized_string(*rank.male_title)));
  }
  if (rank.female_title.has_value()) {
    require_string_form(plugin, *rank.female_title);
    encoded.push_back(make_subrecord(k_fnam, schema::write_localized_string(*rank.female_title)));
  }
  if (rank.insignia.has_value()) {
    encoded.push_back(make_subrecord(k_inam, *rank.insignia));
  }
  return encoded;
}

}  // namespace

std::vector<FactionRelation> decode_faction_relations(const Plugin& plugin, const FormKey& key) {
  const auto& record = faction_record(plugin, key);
  const auto context = plugin_form_id_context(plugin);
  std::vector<FactionRelation> relations;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_xnam) relations.push_back(decode_relation(subrecord, context));
  }
  return relations;
}

std::vector<FactionRank> decode_faction_ranks(const Plugin& plugin, const FormKey& key) {
  return decode_ranks(plugin, faction_record(plugin, key));
}

FormKey create_faction(Plugin& plugin,
                       std::optional<std::string_view> editor_id,
                       std::optional<std::string_view> full_name,
                       std::uint32_t flags) {
  if ((flags & ~k_defined_flags) != 0) {
    throw FactionError("FACT flags may use only defined bits 0x00000001 through 0x00010000");
  }
  const bool has_full_name = full_name.has_value() && !full_name->empty();
  if (has_full_name && plugin.header.tes4_flags.is_localized()) {
    throw FactionError(
        "FACT full name creation in localized plugins is disabled until string-table authoring is implemented");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  std::vector<Subrecord> subrecords;
  if (editor_id.has_value()) {
    subrecords.push_back(Subrecord{sig_edid, zstring(stored_editor_id(*editor_id)), false});
  }
  if (has_full_name) {
    subrecords.push_back(
        Subrecord{k_full, zstring(stored_inline_text(*full_name, "FACT full name")), false});
  }
  std::vector<std::byte> flag_bytes;
  append_u32(flag_bytes, flags);
  subrecords.push_back(make_subrecord(k_data, std::move(flag_bytes)));
  subrecords.push_back(make_subrecord(k_crva, generated_default(k_fact, k_crva)));
  subrecords.push_back(make_subrecord(k_venv, generated_default(k_fact, k_venv)));

  const auto key = create_local_record(next, k_fact, std::move(subrecords));
  for (const auto& issue : schema::preflight_authoring_record(next, key)) {
    if (issue.severity == schema::PreflightSeverity::error) {
      throw FactionError("FACT authoring preflight failed: " + issue.message);
    }
  }
  plugin = std::move(next);
  return key;
}

void add_faction_relation(Plugin& plugin, const FormKey& key, const FactionRelation& relation) {
  static_cast<void>(decode_faction_relations(plugin, key));
  require_relation_target(plugin, relation.faction);
  auto next = plugin;
  auto& record = mutable_faction(next, key);
  const auto context = plugin_form_id_context(next);
  const auto positions = relation_positions(record);
  const auto at = positions.empty()
                      ? first_index_where(record, [](Signature signature) { return signature == k_data; })
                      : positions.back() + 1;
  insert_subrecords(record, at, {encode_relation(relation, context)});
  detail::mark_record_modified(next, record);
  plugin = std::move(next);
}

void set_faction_relation(Plugin& plugin,
                          const FormKey& key,
                          std::size_t index,
                          const FactionRelation& relation) {
  require_editable(plugin, key);
  require_index(index, decode_faction_relations(plugin, key).size(), "FACT relation");
  require_relation_target(plugin, relation.faction);
  auto next = plugin;
  auto& record = mutable_faction(next, key);
  const auto context = plugin_form_id_context(next);
  record.subrecords[relation_positions(record)[index]] = encode_relation(relation, context);
  detail::mark_record_modified(next, record);
  plugin = std::move(next);
}

void remove_faction_relation(Plugin& plugin, const FormKey& key, std::size_t index) {
  require_editable(plugin, key);
  require_index(index, decode_faction_relations(plugin, key).size(), "FACT relation");
  auto next = plugin;
  auto& record = mutable_faction(next, key);
  const auto position = relation_positions(record)[index];
  erase_subrecords(record, position, position + 1);
  detail::mark_record_modified(next, record);
  plugin = std::move(next);
}

void add_faction_rank(Plugin& plugin, const FormKey& key, const FactionRank& rank) {
  static_cast<void>(decode_faction_ranks(plugin, key));
  auto next = plugin;
  auto& record = mutable_faction(next, key);
  const auto spans = rank_spans(record);
  const auto at = spans.empty() ? first_index_where(record, is_rank_successor) : spans.back().end;
  insert_subrecords(record, at, encode_rank(next, rank));
  detail::mark_record_modified(next, record);
  plugin = std::move(next);
}

void set_faction_rank(Plugin& plugin,
                      const FormKey& key,
                      std::size_t index,
                      const FactionRank& rank) {
  require_editable(plugin, key);
  require_index(index, decode_faction_ranks(plugin, key).size(), "FACT rank");
  auto next = plugin;
  auto& record = mutable_faction(next, key);
  const auto span = rank_spans(record)[index];
  erase_subrecords(record, span.begin, span.end);
  insert_subrecords(record, span.begin, encode_rank(next, rank));
  detail::mark_record_modified(next, record);
  plugin = std::move(next);
}

void remove_faction_rank(Plugin& plugin, const FormKey& key, std::size_t index) {
  require_editable(plugin, key);
  require_index(index, decode_faction_ranks(plugin, key).size(), "FACT rank");
  auto next = plugin;
  auto& record = mutable_faction(next, key);
  const auto span = rank_spans(record)[index];
  erase_subrecords(record, span.begin, span.end);
  detail::mark_record_modified(next, record);
  plugin = std::move(next);
}

}  // namespace fo4::forms
