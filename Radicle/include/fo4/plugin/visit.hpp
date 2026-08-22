#pragma once

#include "fo4/plugin/container.hpp"

#include <variant>

namespace fo4 {

template <class GroupT, class Fn>
void visit_group_records(GroupT& group, Fn&& fn) {
  for (auto& child : group.children) {
    if (auto* record = std::get_if<Record>(&child)) {
      fn(*record);
    } else {
      visit_group_records(std::get<Group>(child), fn);
    }
  }
}

template <class PluginT, class Fn>
void visit_records(PluginT& plugin, Fn&& fn) {
  fn(plugin.tes4);
  for (auto& group : plugin.groups) {
    visit_group_records(group, fn);
  }
}

template <class GroupT, class Fn>
void visit_groups_in(GroupT& group, Fn&& fn) {
  fn(group);
  for (auto& child : group.children) {
    if (auto* nested = std::get_if<Group>(&child)) {
      visit_groups_in(*nested, fn);
    }
  }
}

template <class PluginT, class Fn>
void visit_groups(PluginT& plugin, Fn&& fn) {
  for (auto& group : plugin.groups) {
    visit_groups_in(group, fn);
  }
}

}  // namespace fo4
