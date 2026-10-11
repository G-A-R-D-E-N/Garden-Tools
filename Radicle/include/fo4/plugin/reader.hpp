#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <unordered_map>
#include <vector>

namespace fo4 {

Plugin read_plugin(std::span<const std::byte> bytes, std::filesystem::path path = {});
Plugin read_plugin_file(const std::filesystem::path& path);

std::vector<const Record*> collect_records(const Plugin& plugin);
std::vector<const Record*> collect_first_records(const Plugin& plugin);
std::vector<const Group*> collect_groups(const Plugin& plugin);

const Record* find_record(const Plugin& plugin, RawFormId form_id);
const Record* find_record(const Plugin& plugin, const FormKey& form_key);

// Holds pointers into the indexed plugin; rebuild it after any change to that plugin.
class RecordIndex {
public:
  explicit RecordIndex(const Plugin& plugin);
  RecordIndex(const Plugin&&) = delete;

  const Plugin& plugin() const noexcept { return *plugin_; }
  const Record* find(const FormKey& form_key) const;

private:
  struct FormKeyHash {
    std::size_t operator()(const FormKey& form_key) const noexcept;
  };

  const Plugin* plugin_;
  std::unordered_map<FormKey, const Record*, FormKeyHash> records_;
};

PluginFormIdContext plugin_form_id_context(const Plugin& plugin);
void assign_record_form_keys(Plugin& plugin);
void apply_master_kinds(Plugin& plugin, std::span<const MasterDescriptor> known);
void stamp_master_kinds(Plugin& plugin, std::span<const Plugin> loaded_masters);

PluginHeader parse_tes4_header(const Record& tes4);

}  // namespace fo4
