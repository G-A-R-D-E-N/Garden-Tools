#include "fo4/plugin/master_kinds.hpp"

#include "fo4/plugin/reader.hpp"

#include <exception>
#include <vector>

namespace fo4 {

void stamp_master_kinds_from_files(
    Plugin& plugin,
    std::span<const std::filesystem::path> master_paths) {
  std::vector<MasterDescriptor> known;
  known.reserve(master_paths.size());

  for (const auto& path : master_paths) {
    try {
      const auto master = read_plugin_file(path);
      if (master.key.empty()) {
        continue;
      }
      known.push_back({
          master.key,
          classify_plugin_kind(master.header.tes4_flags, master.key),
      });
    } catch (const std::exception&) {
      // Missing or unreadable masters intentionally preserve the filename fallback.
    }
  }

  apply_master_kinds(plugin, known);
}

}  // namespace fo4
