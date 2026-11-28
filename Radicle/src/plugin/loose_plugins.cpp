#include "fo4/plugin/loose_plugins.hpp"

#include <stdexcept>
#include <utility>

namespace fo4 {

void LoosePlugins::replace(std::string_view name, std::shared_ptr<Plugin> plugin) {
  if (!plugin) throw std::invalid_argument("plugin is required");
  const auto key = std::string(name);
  indexes_.erase(key);
  loose_.insert_or_assign(key, std::move(plugin));
}

void LoosePlugins::release(std::string_view name) {
  const auto key = std::string(name);
  indexes_.erase(key);
  loose_.erase(key);
}

void LoosePlugins::set_editable(std::string_view name, std::shared_ptr<Plugin> plugin) {
  const auto key = std::string(name);
  if (plugin) editable_.insert_or_assign(key, std::move(plugin));
  else editable_.erase(key);
}

std::shared_ptr<Plugin> LoosePlugins::get(std::string_view name) const {
  const auto it = loose_.find(std::string(name));
  return it == loose_.end() ? nullptr : it->second;
}

std::shared_ptr<const RecordIndex> LoosePlugins::index(std::string_view name) {
  const auto key = std::string(name);
  const auto plugin = loose_.find(key);
  if (plugin == loose_.end()) return nullptr;
  auto replacement = std::make_shared<IndexedPlugin>(*plugin->second);
  auto result = std::shared_ptr<const RecordIndex>(replacement, &replacement->index);
  indexes_.insert_or_assign(key, std::move(replacement));
  return result;
}

bool LoosePlugins::has_index(std::string_view name) const {
  return indexes_.contains(std::string(name));
}

}
