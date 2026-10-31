#pragma once

#include "fo4/archive/ba2_reader.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/detect.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fo4::previs {

// Census of one Data folder scan; any problem must mark the touched index unchecked.
struct PrevisDataSummary {
  std::size_t cdx_files{0};
  std::size_t cdx_rows{0};
  std::size_t csg_files{0};
  std::size_t csg_rows{0};
  std::size_t csg_chunks{0};
  std::size_t tome_files{0};
  std::size_t oc_nif_files{0};
  std::vector<std::string> problems;
};

// File system answers for diagnose_load_order; all logic must stay native, the panel

class PrevisData {
 public:
  // Scanning must cover <plugin>.cdx, `<plugin> - Geometry.csg`, every GNRL BA2 and loose
  // Vis and Meshes/PreCombined files; a .psg requires the CSG answer to fail closed.
  static PrevisData load(const std::filesystem::path& data_dir, const env::Environment& env);

  LoadOrderIndexes indexes() const;

  const PrevisDataSummary& summary() const noexcept { return summary_; }
  const env::Environment& environment() const noexcept { return *env_; }

 private:
  struct TomeLocation {
    bool loose{false};
    std::size_t archive{0};
    std::size_t entry{0};
    std::filesystem::path path;
  };

  struct NifIndex {
    // Key assembly must be (cell low 24 bits << 32) | OC id.
    std::unordered_set<std::uint64_t> entries;
    std::unordered_set<std::uint32_t> cells;
  };

  // The lookup conversions require the load order, so PrevisData borrows it.
  const env::Environment* env_{nullptr};
  std::size_t base_plugin_{0};
  std::unordered_map<std::string, std::size_t> plugin_by_name_;
  std::unordered_map<std::string, std::size_t> plugin_by_stem_;
  std::unordered_map<std::size_t, CdxIndex> cdx_;
  std::unordered_set<std::size_t> cdx_unreadable_;
  bool cdx_complete_{true};
  std::unordered_set<std::size_t> csg_plugins_;
  bool csg_complete_{true};
  std::unordered_map<std::size_t, NifIndex> nifs_;
  bool nif_complete_{true};
  std::unordered_map<std::string, TomeLocation> tomes_;
  bool tome_complete_{true};
  std::vector<archive::Ba2Reader> archives_;
  mutable std::unordered_map<std::string, TomeLookup> tome_cache_;
  PrevisDataSummary summary_;

  std::optional<bool> cdx_answer(std::size_t plugin, const CdxCellTarget& target) const;
  std::optional<bool> csg_answer(std::size_t plugin, const FormKey& cell) const;
  std::optional<std::uint32_t> missing_oc_nifs(const FormKey& cell,
                                               std::span<const std::uint32_t> mesh_ids) const;
  TomeLookup tome_answer(const FormKey& tome_cell) const;

  bool encode_in_plugin(const FormKey& key, std::size_t plugin, std::uint32_t& out) const;
  bool has_oc_nif(const FormKey& cell) const;
  std::optional<std::size_t> owner_plugin(const FormKey& cell) const;
  void scan_archives(const std::filesystem::path& data_dir);
  void scan_loose(const std::filesystem::path& data_dir);
  void scan_cdx(const std::filesystem::path& data_dir);
  void scan_csg(const std::filesystem::path& data_dir);
  void add_tome(const std::string& plugin_lower, std::uint32_t value, TomeLocation location);
  void add_nif(const std::string& plugin_lower, std::uint32_t cell, std::uint32_t id);
};

}  // namespace fo4::previs
