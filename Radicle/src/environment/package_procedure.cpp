#include "fo4/environment/package_procedure.hpp"

#include "fo4/environment/environment.hpp"
#include "record_reader.hpp"

#include <algorithm>
#include <array>
#include <span>

namespace fo4::env {
namespace {

using detail::Reader;
constexpr Signature sig(const char (&s)[5]) { return Signature{s[0], s[1], s[2], s[3]}; }

constexpr std::array<std::string_view, 11> input_types{"Bool", "Int", "Float", "Ref", "Location", "LocWrapper",
    "LocationSelector", "TargetSelector", "ObjectList", "SingleRef", "Topic"};
constexpr std::array<std::string_view, 5> branch_types{"Procedure", "Sequence", "Stacked", "Simultaneous", "Random"};
constexpr std::array<std::string_view, 30> procedures{"Travel", "Sandbox", "Sit", "Sleep", "Follow", "FollowTo",
    "Patrol", "UseWeapon", "Headtrack", "ForceGreet", "Activate", "HoldPosition", "UnlockDoors", "LockDoors", "Range",
    "Escort", "Eat", "Find", "Flee", "Guard", "GuardArea", "Hover", "Orbit", "KeepAnEyeOn", "Acquire", "UseIdleMarker",
    "UseMagic", "Wander", "Wait", "Say"};

struct LeafInputCount { std::string_view name; std::uint32_t count; };
constexpr std::array<LeafInputCount, 7> leaf_input_counts{{{"Travel", 8}, {"Sandbox", 16}, {"Sit", 1}, {"Sleep", 1},
    {"Follow", 6}, {"Wait", 2}, {"Say", 5}}};

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

bool is_input_name(const Subrecord& sub) {
  const auto text = zstring_view(sub);
  return text && std::find(input_types.begin(), input_types.end(), *text) != input_types.end();
}

bool ends_input(Signature tag) {
  return tag == sig("ANAM") || tag == sig("UNAM") || tag == sig("XNAM");
}

bool ends_leaf(Signature tag) {
  return tag == sig("ANAM") || tag == sig("PFO2") || tag == sig("PFOR") || tag == sig("POBA") ||
      tag == sig("POEA") || tag == sig("POCA") || tag == sig("UNAM") || tag == sig("XNAM");
}

bool is_marker(Signature tag) {
  return tag == sig("POBA") || tag == sig("POEA") || tag == sig("POCA");
}

std::uint8_t byte_of(const Subrecord& sub) { return std::to_integer<std::uint8_t>(sub.data[0]); }

void skip_header(Scan& scan) {
  scan.skip(sig("BNAM"));
  if (scan.is(sig("PNAM"))) scan.take(sig("PNAM"), 4);
}

void skip_flag_override(Scan& scan) {
  if (scan.is(sig("PFO2")) || scan.is(sig("PFOR"))) ++scan.at;
}

void read_inputs(Scan& scan, PackageProcedureTree& out) {
  while (scan.is(sig("ANAM")) && is_input_name(scan.subs[scan.at])) {
    PackageDataInput input;
    input.subrecord_begin = scan.at;
    input.type_name = std::string{*zstring_view(scan.subs[scan.at])};
    ++scan.at;
    while (scan.at < scan.subs.size() && !ends_input(scan.subs[scan.at].signature)) ++scan.at;
    input.subrecord_end = scan.at;
    out.inputs.push_back(std::move(input));
  }
  for (auto& input : out.inputs) {
    input.index = byte_of(scan.subs[scan.take(sig("UNAM"), 1)]);
    skip_header(scan);
  }
  if (scan.is(sig("XNAM"))) out.marker = byte_of(scan.subs[scan.take(sig("XNAM"), 1)]);
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

}

const char* package_location_type_name(PackageLocationType type) {
  return location_names[static_cast<std::size_t>(type)];
}

const char* package_target_type_name(PackageTargetType type) {
  return target_names[static_cast<std::size_t>(type)];
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

}
