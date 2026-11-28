#include "fo4/hkx/object_graph.hpp"

#include "fo4/hkx/layout.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::string_view kRootClass = "hkRootLevelContainer";
constexpr std::size_t kDepthCeiling = 256;
constexpr std::size_t kDefaultNodes = 2000;

struct Site {
  std::uint32_t source;
  std::uint32_t section;
  std::uint32_t destination;
};

bool has_data_section(const HkxPackfile& packfile) {
  return !packfile.sections.empty() && packfile.data_section < packfile.sections.size();
}

// Requires has_data_section; the result is ordered by source offset so every report is deterministic.
std::vector<Site> sites_of(const HkxPackfile& packfile) {
  const HkxSection& data = packfile.sections[packfile.data_section];
  std::vector<Site> sites;
  sites.reserve(data.local_fixups.size() + data.global_fixups.size());
  for (const HkxLocalFixup& fixup : data.local_fixups) {
    sites.push_back({fixup.source, static_cast<std::uint32_t>(packfile.data_section), fixup.destination});
  }
  for (const HkxRemoteFixup& fixup : data.global_fixups) {
    sites.push_back({fixup.source, fixup.section, fixup.destination});
  }
  std::stable_sort(sites.begin(), sites.end(),
                   [](const Site& left, const Site& right) { return left.source < right.source; });
  return sites;
}

// Requires the object list to be ordered by offset, which read_packfile guarantees.
std::int32_t containing(const HkxPackfile& packfile, std::uint32_t offset) {
  const auto after = std::upper_bound(packfile.objects.begin(), packfile.objects.end(), offset,
                                      [](std::uint32_t value, const HkxObjectView& object) {
                                        return value < object.offset;
                                      });
  if (after == packfile.objects.begin()) return -1;
  const auto found = after - 1;
  if (static_cast<std::uint64_t>(offset) >=
      static_cast<std::uint64_t>(found->offset) + static_cast<std::uint64_t>(found->data.size())) {
    return -1;
  }
  return static_cast<std::int32_t>(found - packfile.objects.begin());
}

std::int32_t starting(const HkxPackfile& packfile, std::uint32_t offset) {
  const auto found = std::lower_bound(packfile.objects.begin(), packfile.objects.end(), offset,
                                      [](const HkxObjectView& object, std::uint32_t value) {
                                        return object.offset < value;
                                      });
  if (found == packfile.objects.end() || found->offset != offset) return -1;
  return static_cast<std::int32_t>(found - packfile.objects.begin());
}

struct ClassMembers {
  const HkxClassLayout* layout;
  std::vector<HkxMemberLayout> members;
};

// Invariant: one flattened member list per class name for the lifetime of the cache, so a per-site label costs a map probe.
class MemberCache {
 public:
  const ClassMembers& of(std::string_view class_name) {
    auto found = rows_.find(class_name);
    if (found == rows_.end()) {
      const HkxClassLayout* layout = find_layout(class_name);
      found = rows_.emplace(std::string(class_name),
                            ClassMembers{layout, layout == nullptr ? std::vector<HkxMemberLayout>{} : all_members(*layout)})
                  .first;
    }
    return found->second;
  }

 private:
  std::map<std::string, ClassMembers, std::less<>> rows_;
};

std::string member_name(MemberCache& cache, const HkxObjectView& object, std::uint32_t relative) {
  const ClassMembers& row = cache.of(object.class_name);
  if (row.layout == nullptr || row.layout->signature != object.signature || relative >= row.layout->size) return {};
  std::string_view best;
  std::uint32_t best_offset = 0;
  bool found = false;
  for (const HkxMemberLayout& member : row.members) {
    if (member.offset <= relative && (!found || member.offset >= best_offset)) {
      best = member.name;
      best_offset = member.offset;
      found = true;
    }
  }
  return std::string(best);
}

HkxObjectLink link_of(const HkxPackfile& packfile, const Site& site, MemberCache& cache) {
  HkxObjectLink link;
  link.source_object = containing(packfile, site.source);
  link.source_offset = site.source;
  if (link.source_object >= 0) {
    const HkxObjectView& owner = packfile.objects[static_cast<std::size_t>(link.source_object)];
    link.member = member_name(cache, owner, site.source - owner.offset);
  }
  link.target_section = site.section;
  link.target_offset = site.destination;
  if (site.section == packfile.data_section) {
    link.target_object = starting(packfile, site.destination);
    if (link.target_object >= 0) {
      link.target_class = detail::printable_text(packfile.objects[static_cast<std::size_t>(link.target_object)].class_name);
    } else {
      link.inner = containing(packfile, site.destination) >= 0;
    }
  }
  return link;
}

struct Child {
  std::size_t object;
  std::string member;
};

struct Edges {
  std::vector<std::vector<Child>> children;
  std::vector<std::size_t> incoming;
};

// Invariant: only a pointer that lands on the start of a different object is an edge.
Edges edges_of(const HkxPackfile& packfile, const std::vector<Site>& sites) {
  Edges edges;
  edges.children.resize(packfile.objects.size());
  edges.incoming.assign(packfile.objects.size(), 0);
  MemberCache cache;
  for (const Site& site : sites) {
    if (site.section != packfile.data_section) continue;
    const std::int32_t source = containing(packfile, site.source);
    const std::int32_t target = starting(packfile, site.destination);
    if (source < 0 || target < 0 || source == target) continue;
    const HkxObjectView& owner = packfile.objects[static_cast<std::size_t>(source)];
    edges.children[static_cast<std::size_t>(source)].push_back(
        {static_cast<std::size_t>(target), member_name(cache, owner, site.source - owner.offset)});
    ++edges.incoming[static_cast<std::size_t>(target)];
  }
  return edges;
}

std::string hex_signature(std::uint32_t signature) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text{"0x"};
  for (int shift = 28; shift >= 0; shift -= 4) text.push_back(kDigits[(signature >> shift) & 0xFu]);
  return text;
}

struct Pending {
  std::size_t object;
  std::int32_t parent;
  std::size_t depth;
  std::string member;
};

}  // namespace

HkxStatus lookup_object(const HkxPackfile& packfile, std::size_t index, HkxObjectLookup& out) {
  out = HkxObjectLookup{};
  if (!has_data_section(packfile)) return HkxStatus::MissingSection;
  if (index >= packfile.objects.size()) return HkxStatus::MissingObject;

  HkxObjectQuery query;
  query.object_index = index;
  query.limit = 1;
  HkxObjectReport report;
  if (const HkxStatus status = inspect_objects(packfile, query, report); status != HkxStatus::Ok) return status;
  if (report.objects.empty()) return HkxStatus::MissingObject;
  out.detail = std::move(report.objects.front());
  out.detail.references.clear();

  const auto self = static_cast<std::int32_t>(index);
  MemberCache cache;
  for (const Site& site : sites_of(packfile)) {
    HkxObjectLink link = link_of(packfile, site, cache);
    if (link.source_object == self) {
      out.outgoing.push_back(std::move(link));
    } else if (site.section == packfile.data_section && link.target_object == self) {
      out.incoming.push_back(std::move(link));
    }
  }
  return HkxStatus::Ok;
}

HkxStatus build_object_tree(const HkxPackfile& packfile, const HkxTreeQuery& query, HkxObjectTree& out) {
  out = HkxObjectTree{};
  if (!has_data_section(packfile)) return HkxStatus::MissingSection;
  const std::size_t count = packfile.objects.size();
  out.object_count = count;
  const Edges edges = edges_of(packfile, sites_of(packfile));
  const std::size_t max_depth = std::min(query.max_depth, kDepthCeiling);
  const std::size_t max_nodes = query.max_nodes == 0 ? kDefaultNodes : query.max_nodes;

  for (std::size_t at = 0; at < count; ++at) {
    if (packfile.objects[at].class_name == kRootClass) out.roots.push_back(at);
  }
  if (out.roots.empty()) {
    for (std::size_t at = 0; at < count; ++at) {
      if (edges.incoming[at] == 0) out.roots.push_back(at);
    }
  }
  if (out.roots.empty() && count != 0) out.roots.push_back(0);

  const HkxFieldDecoder decoder(packfile);
  std::vector<bool> seen(count, false);
  std::vector<Pending> stack;
  for (const std::size_t root : out.roots) {
    stack.push_back({root, -1, 0, {}});
    while (!stack.empty()) {
      Pending item = std::move(stack.back());
      stack.pop_back();
      if (out.nodes.size() >= max_nodes) {
        out.truncated = true;
        stack.clear();
        break;
      }
      HkxTreeNode node;
      node.object = item.object;
      node.parent = item.parent;
      node.depth = item.depth;
      node.member = std::move(item.member);
      const HkxObjectView& view = packfile.objects[item.object];
      if (const HkxClassLayout* layout = find_layout(view.class_name, view.signature)) {
        node.summary = decoder.summary(view, *layout);
      }
      const auto here = static_cast<std::int32_t>(out.nodes.size());
      if (seen[item.object]) {
        node.repeated = true;
        out.nodes.push_back(std::move(node));
        continue;
      }
      seen[item.object] = true;
      const std::vector<Child>& children = edges.children[item.object];
      if (item.depth >= max_depth && !children.empty()) {
        node.cut = true;
        out.nodes.push_back(std::move(node));
        continue;
      }
      out.nodes.push_back(std::move(node));
      for (auto child = children.rbegin(); child != children.rend(); ++child) {
        stack.push_back({child->object, here, item.depth + 1, child->member});
      }
    }
    if (out.truncated) break;
  }
  out.unreachable = static_cast<std::size_t>(std::count(seen.begin(), seen.end(), false));
  return HkxStatus::Ok;
}

HkxStatus diagnose_packfile(const HkxPackfile& packfile, HkxPackfileDiagnostics& out) {
  out = HkxPackfileDiagnostics{};
  if (!has_data_section(packfile)) return HkxStatus::MissingSection;
  out.file_version = packfile.file_version;
  out.user_tag = packfile.user_tag;
  out.pointer_size = packfile.pointer_size;
  out.object_count = packfile.objects.size();

  for (const HkxSection& section : packfile.sections) {
    HkxSectionDiagnostic row;
    row.tag = section.tag;
    row.size = section.data.size();
    row.local_fixups = section.local_fixups.size();
    row.global_fixups = section.global_fixups.size();
    row.virtual_fixups = section.virtual_fixups.size();
    out.sections.push_back(std::move(row));
  }

  if (!packfile.sections[packfile.data_section].virtual_fixups_ascending) {
    out.issues.push_back("the virtual fixup table of the data section is not in ascending offset order");
  }

  std::map<std::pair<std::string, std::uint32_t>, std::size_t> class_rows;
  for (const HkxObjectView& object : packfile.objects) {
    const auto key = std::make_pair(std::string(object.class_name), static_cast<std::uint32_t>(object.signature));
    const auto known = class_rows.find(key);
    auto found = known == class_rows.end() ? out.classes.end() : out.classes.begin() + static_cast<std::ptrdiff_t>(known->second);
    if (found == out.classes.end()) {
      HkxClassDiagnostic row;
      row.name = detail::printable_text(object.class_name);
      row.signature = object.signature;
      row.shortest_span = object.data.size();
      if (const HkxClassLayout* layout = find_layout(object.class_name)) {
        row.layout_size = layout->size;
        row.status = layout->signature == object.signature ? HkxClassLayoutStatus::Match
                                                           : HkxClassLayoutStatus::SignatureMismatch;
      }
      out.classes.push_back(std::move(row));
      found = out.classes.end() - 1;
      class_rows.emplace(key, out.classes.size() - 1);
    }
    ++found->count;
    found->shortest_span = std::min(found->shortest_span, object.data.size());
  }
  for (const HkxClassDiagnostic& row : out.classes) {
    if (row.status == HkxClassLayoutStatus::SignatureMismatch) {
      out.issues.push_back("class " + row.name + " has signature " + hex_signature(row.signature) +
                           ", which differs from the layout table");
    } else if (row.status == HkxClassLayoutStatus::Match && row.shortest_span < row.layout_size) {
      out.issues.push_back("class " + row.name + " has an object span of " + std::to_string(row.shortest_span) +
                           " bytes, shorter than its layout size " + std::to_string(row.layout_size));
    }
  }

  const std::vector<Site> sites = sites_of(packfile);
  for (const Site& site : sites) {
    if (site.section != packfile.data_section) {
      ++out.external_links;
    } else if (starting(packfile, site.destination) >= 0) {
      ++out.object_links;
    } else if (containing(packfile, site.destination) >= 0) {
      ++out.inner_links;
    } else {
      ++out.stray_links;
    }
  }
  if (out.stray_links != 0) {
    out.issues.push_back(std::to_string(out.stray_links) + " pointer(s) target an offset outside every object");
  }

  const Edges edges = edges_of(packfile, sites);
  for (std::size_t at = 0; at < packfile.objects.size(); ++at) {
    if (edges.incoming[at] == 0 && packfile.objects[at].class_name != kRootClass) ++out.unreferenced_objects;
  }
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
