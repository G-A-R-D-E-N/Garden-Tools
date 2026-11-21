#include "fo4/hkx/behavior.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/object_view.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fo4::hkx {
namespace {

constexpr std::string_view kBehaviorPrefix = "hkb";

}  // namespace

HkxStatus inspect_behavior(const HkxPackfile& packfile, std::size_t limit, HkxBehaviorGraph& out) {
  out = HkxBehaviorGraph{};
  HkxObjectQuery query;
  query.class_prefix = std::string(kBehaviorPrefix);
  query.limit = limit == 0 ? 4096 : limit;

  HkxObjectReport view;
  if (const auto status = inspect_objects(packfile, query, view); status != HkxStatus::Ok) return status;
  out.object_count = view.object_count;
  out.matched = view.matched;

  for (const std::string& name : view.classes) {
    if (name.rfind(kBehaviorPrefix, 0) == 0) out.classes.push_back(name);
  }

  std::unordered_map<std::size_t, std::size_t> position;
  std::vector<std::string> unknown;
  for (const HkxObjectDetail& object : view.objects) {
    position.emplace(object.index, out.nodes.size());
    out.nodes.push_back({object.index, std::string(object.class_name), object.offset, object.size, object.layout_known, 0, 0});
    if (!object.layout_known && std::find(unknown.begin(), unknown.end(), object.class_name) == unknown.end()) {
      unknown.push_back(std::string(object.class_name));
    }
  }

  std::size_t dangling = 0;
  for (const HkxObjectDetail& object : view.objects) {
    const auto from = position.find(object.index);
    if (from == position.end()) continue;
    for (const HkxObjectReference& reference : object.references) {
      if (reference.target_object < 0) {
        ++dangling;
        continue;
      }
      const auto to = position.find(static_cast<std::size_t>(reference.target_object));
      if (to == position.end()) continue;
      out.links.push_back({from->second, to->second, std::string(reference.member)});
      ++out.nodes[from->second].references_out;
      ++out.nodes[to->second].references_in;
    }
  }

  for (const std::string& name : unknown) {
    out.diagnostics.push_back("no generated layout for " + name + ", so only its links are reported");
  }
  if (dangling > 0) {
    out.diagnostics.push_back(std::to_string(dangling) +
                              " references point outside the behavior objects and are not graphed");
  }
  if (out.nodes.empty()) out.diagnostics.push_back("no behavior objects in this file");
  return HkxStatus::Ok;
}

}  // namespace fo4::hkx
