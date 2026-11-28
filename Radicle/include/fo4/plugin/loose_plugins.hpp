#pragma once

#include "fo4/plugin/reader.hpp"
#include "fo4/text/encoding.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace fo4 {

class LoosePlugins {
public:
  void replace(std::string_view name, std::shared_ptr<Plugin> plugin);
  void release(std::string_view name);
  void set_editable(std::string_view name, std::shared_ptr<Plugin> plugin);
  std::shared_ptr<Plugin> get(std::string_view name) const;
  std::shared_ptr<const RecordIndex> index(std::string_view name);
  bool has_index(std::string_view name) const;

private:
  struct NameLess {
    bool operator()(const std::string& a, const std::string& b) const noexcept {
      return text::compare_folded(a, b) < 0;
    }
  };
  std::map<std::string, std::shared_ptr<Plugin>, NameLess> loose_;
  std::map<std::string, std::shared_ptr<Plugin>, NameLess> editable_;
  struct IndexedPlugin {
    explicit IndexedPlugin(const Plugin& plugin) : snapshot(plugin), index(snapshot) {}
    Plugin snapshot;
    RecordIndex index;
  };
  std::map<std::string, std::shared_ptr<IndexedPlugin>, NameLess> indexes_;
};

}
