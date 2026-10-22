#include "fo4/environment/package_procedure.hpp"

#include "../plugin/record_edit.hpp"
#include "fo4/environment/environment.hpp"
#include "record_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

constexpr std::array<std::string_view, 5> branch_types{"Procedure", "Sequence", "Stacked", "Simultaneous", "Random"};
constexpr std::array<std::string_view, 30> procedures{"Travel", "Sandbox", "Sit", "Sleep", "Follow", "FollowTo",
    "Patrol", "UseWeapon", "Headtrack", "ForceGreet", "Activate", "HoldPosition", "UnlockDoors", "LockDoors", "Range",
    "Escort", "Eat", "Find", "Flee", "Guard", "GuardArea", "Hover", "Orbit", "KeepAnEyeOn", "Acquire", "UseIdleMarker",
    "UseMagic", "Wander", "Wait", "Say"};

struct LeafInputCount { std::string_view name; std::uint32_t count; };
constexpr std::array<LeafInputCount, 7> leaf_input_counts{{{"Travel", 8}, {"Sandbox", 16}, {"Sit", 1}, {"Sleep", 1},
    {"Follow", 6}, {"Wait", 2}, {"Say", 5}}};

struct InputValueLayout {
  Signature tag;
  std::uint8_t size;
};

struct InputLayout {
  std::string_view name;
  PackageInputValueKind kind;
  std::array<InputValueLayout, 2> values;
  std::uint8_t value_count;
};

constexpr InputLayout input_layouts[] = {
    {"Bool", PackageInputValueKind::boolean, {{{sig("CNAM"), 1}, {}}}, 1},
    {"Int", PackageInputValueKind::integer, {{{sig("CNAM"), 4}, {}}}, 1},
    {"Float", PackageInputValueKind::real, {{{sig("CNAM"), 4}, {}}}, 1},
    {"Ref", PackageInputValueKind::reference, {{{sig("CNAM"), 4}, {}}}, 1},
    {"SingleRef", PackageInputValueKind::target, {{{sig("PTDA"), 12}, {sig("PTDT"), 16}}}, 2},
    {"TargetSelector", PackageInputValueKind::target, {{{sig("PTDA"), 12}, {sig("PTDT"), 16}}}, 2},
    {"Location", PackageInputValueKind::location, {{{sig("PLDT"), 16}, {sig("PLDT"), 12}}}, 2},
    {"LocationSelector", PackageInputValueKind::location, {{{sig("PLDT"), 16}, {sig("PLDT"), 12}}}, 2},
    {"LocWrapper", PackageInputValueKind::none, {{{}, {}}}, 0},
    {"ObjectList", PackageInputValueKind::object_list, {{{sig("CNAM"), 4}, {}}}, 1},
    {"Topic", PackageInputValueKind::topic, {{{sig("PDTO"), 8}, {sig("TPIC"), 4}}}, 2},
};

constexpr std::uint8_t max_input_id = 0xFE;
constexpr std::size_t input_id_count = 0xFF;

const InputLayout* layout_for(std::string_view name) {
  for (const auto& layout : input_layouts) {
    if (layout.name == name) return &layout;
  }
  return nullptr;
}

constexpr const char* location_names[] = {"near reference", "in cell", "near package start", "near editor location",
    "resolved from search result", "resolved from search result", "near linked reference",
    "at package location (unproven: no type 7 arm found, RE-015)", "quest alias", "location handle (derived)",
    "named special reference", "input ID matched against the data-input set (RE-015)", "near self",
    "entire editor location cell", "alias collection item"};
constexpr const char* target_names[] = {"specific reference", "object id", "object type", "linked reference",
    "quest reference alias", "special reference", "self", "keyword", "alias collection item"};

constexpr std::size_t max_depth = 64;

struct Unreconciled {};

struct Scan {
  const std::vector<Subrecord>& subs;
  std::size_t at;

  bool done() const noexcept { return at >= subs.size(); }
  bool is(Signature tag) const noexcept { return !done() && subs[at].signature == tag; }
  void skip(Signature tag) noexcept { if (is(tag)) ++at; }
  std::size_t take(Signature tag, std::optional<std::size_t> size = std::nullopt) {
    if (!is(tag) || (size && subs[at].data.size() != *size)) throw Unreconciled{};
    return at++;
  }
};

std::string_view name_in(const Subrecord& sub, std::span<const std::string_view> names) {
  const auto text = zstring_view(sub);
  if (!text || std::find(names.begin(), names.end(), *text) == names.end()) throw Unreconciled{};
  return *text;
}

const InputLayout* input_layout_of(const Subrecord& sub) {
  const auto text = zstring_view(sub);
  return text ? layout_for(*text) : nullptr;
}

bool ends_leaf(Signature tag) {
  return tag == sig("ANAM") || tag == sig("PFO2") || tag == sig("PFOR") || tag == sig("POBA") ||
      tag == sig("POEA") || tag == sig("POCA") || tag == sig("UNAM") || tag == sig("XNAM");
}

bool is_marker(Signature tag) {
  return tag == sig("POBA") || tag == sig("POEA") || tag == sig("POCA");
}

std::uint8_t byte_of(const Subrecord& sub) { return std::to_integer<std::uint8_t>(sub.data[0]); }

std::uint32_t dword_of(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

float float_of(std::span<const std::byte> data, std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

void skip_header(Scan& scan) {
  scan.skip(sig("BNAM"));
  if (scan.is(sig("PNAM"))) scan.take(sig("PNAM"), 4);
}

void skip_flag_override(Scan& scan) {
  if (scan.is(sig("PFO2")) || scan.is(sig("PFOR"))) ++scan.at;
}

const InputValueLayout* match_value(const InputLayout& layout, const Subrecord& value) {
  for (std::uint8_t i = 0; i < layout.value_count; ++i) {
    if (layout.values[i].tag == value.signature &&
        static_cast<std::size_t>(layout.values[i].size) == value.data.size()) {
      return &layout.values[i];
    }
  }
  return nullptr;
}

void read_input_value(Scan& scan, const InputLayout& layout, PackageDataInput& input) {
  if (layout.value_count == 0) return;
  if (scan.done()) throw Unreconciled{};
  const auto* matched = match_value(layout, scan.subs[scan.at]);
  if (matched == nullptr) throw Unreconciled{};
  const auto& value = scan.subs[scan.at];
  input.value_index = scan.at;
  input.value_tag = value.signature;
  switch (layout.kind) {
    case PackageInputValueKind::boolean:
      input.boolean = byte_of(value) != 0;
      break;
    case PackageInputValueKind::integer:
      input.integer_raw = dword_of(value.data, 0);
      input.integer = static_cast<std::int32_t>(input.integer_raw);
      break;
    case PackageInputValueKind::real:
      input.real = float_of(value.data, 0);
      break;
    default:
      break;
  }
  ++scan.at;
}

void read_inputs(Scan& scan, PackageProcedureTree& out) {
  while (scan.is(sig("ANAM"))) {
    const auto* layout = input_layout_of(scan.subs[scan.at]);
    if (layout == nullptr) break;
    PackageDataInput input;
    input.subrecord_begin = scan.at;
    input.type_name = std::string{layout->name};
    input.kind = layout->kind;
    ++scan.at;
    read_input_value(scan, *layout, input);
    input.subrecord_end = scan.at;
    out.inputs.push_back(std::move(input));
  }
  for (auto& input : out.inputs) {
    input.id_index = scan.take(sig("UNAM"), 1);
    input.index = byte_of(scan.subs[input.id_index]);
    skip_header(scan);
    input.id_end = scan.at;
  }
  if (scan.is(sig("XNAM"))) out.next_input_id = byte_of(scan.subs[scan.take(sig("XNAM"), 1)]);
}

std::uint32_t read_conditions(Scan& scan) {
  const auto count = Reader::u32(scan.subs[scan.take(sig("CITC"), 4)], 0);
  for (std::uint32_t i = 0; i < count; ++i) {
    scan.take(sig("CTDA"), 32);
    scan.skip(sig("CIS1"));
    scan.skip(sig("CIS2"));
  }
  return count;
}

std::size_t read_node(Scan& scan, std::vector<PackageProcedureNode>& nodes, std::size_t depth) {
  if (depth > max_depth) throw Unreconciled{};
  const auto branch = name_in(scan.subs[scan.take(sig("ANAM"))], branch_types);
  const auto index = nodes.size();
  nodes.emplace_back();
  nodes[index].kind = branch == "Procedure" ? PackageProcedureKind::leaf : PackageProcedureKind::container;
  nodes[index].name = std::string{branch};
  nodes[index].condition_count = read_conditions(scan);
  if (nodes[index].kind == PackageProcedureKind::leaf) {
    nodes[index].name = std::string{name_in(scan.subs[scan.take(sig("PNAM"))], procedures)};
    nodes[index].opaque_begin = scan.at;
    while (scan.at < scan.subs.size() && !ends_leaf(scan.subs[scan.at].signature)) ++scan.at;
    nodes[index].opaque_end = scan.at;
    skip_flag_override(scan);
    return index;
  }
  const auto& prcb = scan.subs[scan.take(sig("PRCB"), 8)];
  nodes[index].child_count = Reader::u32(prcb, 0);
  nodes[index].flags = Reader::u32(prcb, 4);
  for (std::uint32_t i = 0; i < nodes[index].child_count; ++i) {
    const auto child = read_node(scan, nodes, depth + 1);
    nodes[index].children.push_back(child);
  }
  skip_flag_override(scan);
  return index;
}

void read_tree(Scan& scan, PackageProcedureTree& out) {
  if (scan.is(sig("ANAM"))) read_node(scan, out.nodes, 0);
  while (scan.is(sig("UNAM"))) {
    scan.take(sig("UNAM"), 1);
    skip_header(scan);
  }
  if (!scan.done() && !is_marker(scan.subs[scan.at].signature)) throw Unreconciled{};
}

std::size_t find_pkcu(const Record& record) {
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature == sig("PKCU") && record.subrecords[i].data.size() == 12) return i;
  }
  return record.subrecords.size();
}

void write_dword(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    data[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
  }
}

std::uint8_t step_input_id(std::uint8_t id) {
  return id >= max_input_id ? std::uint8_t{0} : static_cast<std::uint8_t>(id + 1);
}

std::uint8_t clamped_input_id(std::uint8_t id) {
  return id > max_input_id ? std::uint8_t{0} : id;
}

std::optional<std::uint8_t> next_free_input_id(const std::array<bool, input_id_count>& used, std::uint8_t after) {
  auto candidate = step_input_id(after);
  for (std::size_t i = 0; i < input_id_count; ++i) {
    if (!used[candidate]) return candidate;
    candidate = step_input_id(candidate);
  }
  return std::nullopt;
}

bool scalar_kind(PackageInputValueKind kind) {
  return kind == PackageInputValueKind::boolean || kind == PackageInputValueKind::integer ||
      kind == PackageInputValueKind::real;
}

enum class Serialized { ok, kind_mismatch, size_invalid, undetermined };

struct SerializedValue {
  Serialized status{Serialized::size_invalid};
  Signature tag{};
  std::vector<std::byte> bytes;
};

SerializedValue serialize_value(const InputLayout& layout, const PackageInputValue& value) {
  SerializedValue out;
  if (value.kind != layout.kind) {
    out.status = Serialized::kind_mismatch;
    return out;
  }
  if (layout.kind == PackageInputValueKind::none) {
    out.status = Serialized::undetermined;
    return out;
  }
  if (scalar_kind(layout.kind)) {
    if (!value.payload.empty()) return out;
    out.tag = layout.values[0].tag;
    out.bytes.assign(static_cast<std::size_t>(layout.values[0].size), std::byte{0});
    if (layout.kind == PackageInputValueKind::boolean) {
      out.bytes[0] = std::byte{static_cast<std::uint8_t>(value.boolean ? 1 : 0)};
    } else if (layout.kind == PackageInputValueKind::integer) {
      write_dword(out.bytes, 0, static_cast<std::uint32_t>(value.integer));
    } else {
      const auto raw = value.real;
      std::memcpy(out.bytes.data(), &raw, sizeof(raw));
    }
    out.status = Serialized::ok;
    return out;
  }
  for (std::uint8_t i = 0; i < layout.value_count; ++i) {
    if (static_cast<std::size_t>(layout.values[i].size) != value.payload.size()) continue;
    if (layout.values[i].tag == sig("TPIC")) {
      out.status = Serialized::undetermined;
      return out;
    }
    out.tag = layout.values[i].tag;
    out.bytes = value.payload;
    out.status = Serialized::ok;
    return out;
  }
  return out;
}

PackageInputMutationStatus status_for(Serialized status) {
  switch (status) {
    case Serialized::ok:
      return PackageInputMutationStatus::updated;
    case Serialized::kind_mismatch:
      return PackageInputMutationStatus::kind_mismatch;
    case Serialized::undetermined:
      return PackageInputMutationStatus::payload_layout_undetermined;
    case Serialized::size_invalid:
      break;
  }
  return PackageInputMutationStatus::payload_size_invalid;
}

const PackageDataInput* find_input(const PackageProcedureTree& tree, std::uint8_t id, bool& duplicate) {
  const PackageDataInput* found = nullptr;
  duplicate = false;
  for (const auto& input : tree.inputs) {
    if (input.index != id) continue;
    if (found != nullptr) duplicate = true;
    if (found == nullptr) found = &input;
  }
  return found;
}

}

const char* package_location_type_name(PackageLocationType type) {
  return location_names[static_cast<std::size_t>(type)];
}

const char* package_target_type_name(PackageTargetType type) {
  return target_names[static_cast<std::size_t>(type)];
}

std::optional<PackageInputValueKind> package_input_value_kind(std::string_view type_name) {
  const auto* layout = layout_for(type_name);
  if (layout == nullptr) return std::nullopt;
  return layout->kind;
}

PackageProcedureTree decode_package_procedure(const Record& record) {
  const auto& subs = record.subrecords;
  PackageProcedureTree out;
  const auto counter = std::find_if(subs.begin(), subs.end(), [](const Subrecord& sub) { return sub.signature == sig("PKCU"); });
  if (counter == subs.end()) return out;
  const auto begin = static_cast<std::size_t>(counter - subs.begin()) + 1;
  std::size_t trailer = begin;
  while (trailer < subs.size() && !is_marker(subs[trailer].signature)) ++trailer;
  out.region_begin = begin;
  out.region_end = trailer;
  try {
    Scan scan{subs, begin};
    read_inputs(scan, out);
    if (counter->data.size() != 12 ||
        static_cast<std::size_t>(Reader::u32(*counter, 0)) != out.inputs.size()) {
      throw Unreconciled{};
    }
    read_tree(scan, out);
    out.region_end = scan.at;
    out.decoded = true;
  } catch (const Unreconciled&) {
    out = PackageProcedureTree{};
    out.region_begin = begin;
    out.region_end = trailer;
  }
  return out;
}

std::optional<std::uint32_t> expected_input_count(std::string_view leaf_name) {
  for (const auto& row : leaf_input_counts) {
    if (row.name == leaf_name) return row.count;
  }
  return std::nullopt;
}

PackageInputMutationStatus add_package_data_input(Plugin& plugin,
                                                  const FormKey& record_key,
                                                  std::string_view type_name,
                                                  const PackageInputValue& value) {
  const auto* layout = layout_for(type_name);
  if (layout == nullptr) return PackageInputMutationStatus::unknown_input_type;
  const auto serialized = serialize_value(*layout, value);
  if (serialized.status != Serialized::ok) return status_for(serialized.status);
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return PackageInputMutationStatus::record_missing;
  if (record->header.signature != sig("PACK")) return PackageInputMutationStatus::not_a_package;
  const auto tree = decode_package_procedure(*record);
  if (!tree.decoded) return PackageInputMutationStatus::undecodable;
  const auto pkcu = find_pkcu(*record);
  if (pkcu == record->subrecords.size()) return PackageInputMutationStatus::undecodable;
  if (tree.inputs.size() >= input_id_count) return PackageInputMutationStatus::no_free_input_id;

  std::array<bool, input_id_count> used{};
  std::uint8_t highest = 0;
  for (const auto& input : tree.inputs) {
    if (input.index <= max_input_id) {
      if (used[input.index]) return PackageInputMutationStatus::duplicate_input_id;
      used[input.index] = true;
    }
    highest = std::max(highest, input.index);
  }
  std::optional<std::uint8_t> new_id = tree.inputs.empty() ? std::optional<std::uint8_t>(std::uint8_t{0}) : next_free_input_id(used, highest);
  if (tree.next_input_id && !used[clamped_input_id(*tree.next_input_id)]) new_id = clamped_input_id(*tree.next_input_id);
  if (!new_id) return PackageInputMutationStatus::no_free_input_id;
  used[*new_id] = true;
  const auto next_id = next_free_input_id(used, *new_id);

  const auto groups_end = tree.inputs.empty() ? tree.region_begin : tree.inputs.back().subrecord_end;
  const auto ids_end = tree.inputs.empty() ? tree.region_begin : tree.inputs.back().id_end;
  Subrecord id_subrecord;
  id_subrecord.signature = sig("UNAM");
  id_subrecord.data.push_back(std::byte{*new_id});
  std::vector<Subrecord> group;
  Subrecord name_subrecord;
  name_subrecord.signature = sig("ANAM");
  name_subrecord.data.reserve(layout->name.size() + 1);
  for (const auto character : layout->name) {
    name_subrecord.data.push_back(std::byte{static_cast<std::uint8_t>(character)});
  }
  name_subrecord.data.push_back(std::byte{0});
  group.push_back(std::move(name_subrecord));
  if (layout->value_count > 0) {
    Subrecord value_subrecord;
    value_subrecord.signature = serialized.tag;
    value_subrecord.data = serialized.bytes;
    group.push_back(std::move(value_subrecord));
  }
  record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(ids_end), std::move(id_subrecord));
  record->subrecords.insert(record->subrecords.begin() + static_cast<std::ptrdiff_t>(groups_end),
                            std::make_move_iterator(group.begin()), std::make_move_iterator(group.end()));
  write_dword(record->subrecords[pkcu].data, 0, static_cast<std::uint32_t>(tree.inputs.size() + 1));
  if (tree.next_input_id) {
    const auto counter = std::find_if(record->subrecords.begin() + static_cast<std::ptrdiff_t>(pkcu) + 1,
                                      record->subrecords.end(),
                                      [](const Subrecord& sub) { return sub.signature == sig("XNAM") && sub.data.size() == 1; });
    if (counter != record->subrecords.end()) counter->data[0] = std::byte{next_id.value_or(*new_id)};
  }
  fo4::detail::mark_record_modified(plugin, *record);
  return PackageInputMutationStatus::updated;
}

PackageInputMutationStatus set_package_data_input(Plugin& plugin,
                                                  const FormKey& record_key,
                                                  std::uint8_t id,
                                                  const PackageInputValue& value) {
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return PackageInputMutationStatus::record_missing;
  if (record->header.signature != sig("PACK")) return PackageInputMutationStatus::not_a_package;
  const auto tree = decode_package_procedure(*record);
  if (!tree.decoded) return PackageInputMutationStatus::undecodable;
  bool duplicate = false;
  const auto* input = find_input(tree, id, duplicate);
  if (duplicate) return PackageInputMutationStatus::duplicate_input_id;
  if (input == nullptr) return PackageInputMutationStatus::input_id_missing;
  const auto* layout = layout_for(input->type_name);
  if (layout == nullptr) return PackageInputMutationStatus::undecodable;
  const auto serialized = serialize_value(*layout, value);
  if (serialized.status != Serialized::ok) return status_for(serialized.status);
  if (input->value_tag == sig("TPIC")) return PackageInputMutationStatus::payload_layout_undetermined;
  if (serialized.tag != input->value_tag) return PackageInputMutationStatus::payload_tag_mismatch;
  if (input->value_index >= record->subrecords.size()) return PackageInputMutationStatus::undecodable;
  auto& payload = record->subrecords[input->value_index].data;
  if (payload == serialized.bytes) return PackageInputMutationStatus::unchanged;
  payload = serialized.bytes;
  fo4::detail::mark_record_modified(plugin, *record);
  return PackageInputMutationStatus::updated;
}

PackageInputMutationStatus remove_package_data_input(Plugin& plugin,
                                                     const FormKey& record_key,
                                                     std::uint8_t id) {
  auto* record = fo4::detail::locate_mutable_record(plugin, record_key);
  if (record == nullptr) return PackageInputMutationStatus::record_missing;
  if (record->header.signature != sig("PACK")) return PackageInputMutationStatus::not_a_package;
  const auto tree = decode_package_procedure(*record);
  if (!tree.decoded) return PackageInputMutationStatus::undecodable;
  bool duplicate = false;
  const auto* input = find_input(tree, id, duplicate);
  if (duplicate) return PackageInputMutationStatus::duplicate_input_id;
  if (input == nullptr) return PackageInputMutationStatus::input_id_missing;
  const auto pkcu = find_pkcu(*record);
  if (pkcu == record->subrecords.size()) return PackageInputMutationStatus::undecodable;
  if (Reader::u32(record->subrecords[pkcu], 4) == 0 || !tree.nodes.empty()) return PackageInputMutationStatus::procedure_tree_present;

  record->subrecords.erase(record->subrecords.begin() + static_cast<std::ptrdiff_t>(input->id_index),
                           record->subrecords.begin() + static_cast<std::ptrdiff_t>(input->id_end));
  record->subrecords.erase(record->subrecords.begin() + static_cast<std::ptrdiff_t>(input->subrecord_begin),
                           record->subrecords.begin() + static_cast<std::ptrdiff_t>(input->subrecord_end));
  write_dword(record->subrecords[pkcu].data, 0, static_cast<std::uint32_t>(tree.inputs.size() - 1));
  fo4::detail::mark_record_modified(plugin, *record);
  return PackageInputMutationStatus::updated;
}

}
