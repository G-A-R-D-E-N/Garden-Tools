#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/previs/cdx.hpp"
#include "fo4/previs/csg.hpp"
#include "fo4/previs/detect.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <map>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fo4::previs {

// Census of one asset scan; a problem must mark the touched index unchecked or fail closed.
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

// File system answers for diagnose_load_order; all logic must stay native, the panel only displays.
class PrevisData {
 public:
  // Asset roots must be lowest priority first; later roots win and loose must beat archives.
  // A scan must cover <plugin>.cdx, `<plugin> - Geometry.csg`, every GNRL BA2 and loose Vis and
  // Meshes/PreCombined files; a .psg must fail the CSG container check closed.
  static PrevisData load(std::span<const std::filesystem::path> asset_roots,
                         const env::Environment& env);

  LoadOrderIndexes indexes() const;

  const PrevisDataSummary& summary() const noexcept { return summary_; }
  const env::Environment& environment() const noexcept { return *env_; }

 private:
  struct TomeLocation {
    std::size_t resolver{0};
    archive::AssetLocation location;
  };

  struct NifIndex {
    // Key assembly must be (cell low 24 bits << 32) | OC id.
    std::unordered_set<std::uint64_t> entries;
    std::unordered_set<std::uint32_t> cells;
  };

  struct NifLocation {
    std::size_t resolver{0};
    archive::AssetLocation location;
  };

  // The lookup conversions require the load order, so PrevisData borrows it.
  const env::Environment* env_{nullptr};
  std::size_t base_plugin_{0};
  bool base_known_{false};
  std::unordered_map<std::string, std::size_t> plugin_by_name_;
  std::unordered_map<std::string, std::size_t> plugin_by_stem_;
  std::unordered_map<std::size_t, CdxIndex> cdx_;
  std::unordered_set<std::size_t> cdx_present_;
  std::unordered_set<std::size_t> cdx_unreadable_;
  bool cdx_complete_{true};
  std::unordered_set<std::size_t> csg_present_;
  // Invariant: filled once at load, after every CSG root is scanned, from csg_present_.
  std::vector<std::pair<std::uint32_t, std::size_t>> csg_hashed_containers_;
  // A decided plugin must not be re-read from a lower-priority root.
  std::unordered_set<std::size_t> csg_decided_;
  std::unordered_map<std::size_t, NifIndex> nifs_;
  std::map<std::pair<std::size_t, std::uint64_t>, NifLocation> nif_locations_;
  bool nif_complete_{true};
  // Winning container per plugin, plus caches: building an index must not decompress.
  std::unordered_map<std::size_t, std::filesystem::path> csg_paths_;
  std::unordered_set<std::size_t> csg_unreadable_;
  mutable std::unordered_map<std::size_t, std::optional<CsgRowIndex>> csg_index_;
  // Invariant: a csg_failures_ entry is why that plugin's index is unavailable.
  mutable std::unordered_map<std::size_t, std::string> csg_failures_;
  mutable std::map<std::pair<std::size_t, std::uint64_t>, std::optional<std::vector<CsgMeshSpan>>> nif_meshes_;
  std::unordered_map<std::string, TomeLocation> tomes_;
  bool tome_complete_{true};
  std::vector<archive::AssetResolver> resolvers_;
  mutable std::unordered_map<std::string, TomeLookup> tome_cache_;
  PrevisDataSummary summary_;

  std::optional<bool> cdx_answer(std::size_t plugin, const CdxCellTarget& target) const;
  std::optional<std::uint32_t> missing_oc_nifs(const FormKey& cell,
                                               std::span<const std::uint32_t> mesh_ids) const;
  CellCsgAnswer csg_answer(const FormKey& cell, std::span<const std::uint32_t> mesh_ids) const;
  const CsgRowIndex* csg_index(std::size_t plugin) const;
  std::optional<std::vector<CsgMeshSpan>> nif_meshes(std::size_t plugin, std::uint64_t key) const;
  TomeLookup tome_answer(const FormKey& tome_cell) const;

  bool encode_in_plugin(const FormKey& key, std::size_t plugin, std::uint32_t& out) const;
  std::optional<std::size_t> owner_plugin(const FormKey& cell) const;
  std::string base_plugin_lower() const;
  bool plugin_ships_previs(std::size_t plugin) const;
  void check_csg_containers();
  void scan_assets(const std::filesystem::path& root);
  void scan_cdx(const std::filesystem::path& root);
  void scan_csg(const std::filesystem::path& root);
  void add_tome(const std::string& plugin_lower, std::uint32_t value, TomeLocation location);
  void add_nif(const std::string& plugin_lower, std::uint32_t cell, std::uint32_t id,
               NifLocation location);
};

}  // namespace fo4::previs
