#include "fo4/previs/previs_data.hpp"

#include "fo4/previs/csg.hpp"
#include "fo4/previs/uvd.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <span>
#include <string_view>
#include <utility>

namespace fo4::previs {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view k_geometry_csg_suffix = " - geometry.csg";
constexpr std::string_view k_oc_nif_suffix = "_oc.nif";

std::string lowered(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char ch : text) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

// Path comparison invariant: one separator convention and ASCII case folding.
std::string normalized(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (const unsigned char ch : path) {
    out.push_back(ch == '/' ? '\\' : static_cast<char>(std::tolower(ch)));
  }
  return out;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::optional<std::uint32_t> hex8(std::string_view text) {
  if (text.size() != 8) return std::nullopt;
  std::uint32_t value = 0;
  for (const char ch : text) {
    std::uint32_t digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = static_cast<std::uint32_t>(ch - 'a' + 10);
    } else {
      return std::nullopt;
    }
    value = (value << 4u) | digit;
  }
  return value;
}

std::vector<std::string_view> split_path(std::string_view path) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto at = path.find('\\', start);
    if (at == std::string_view::npos) {
      parts.push_back(path.substr(start));
      break;
    }
    parts.push_back(path.substr(start, at - start));
    start = at + 1;
  }
  return parts;
}

std::vector<std::byte> read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) throw std::runtime_error("cannot open " + path.string());
  const std::streamoff size = in.tellg();
  if (size < 0) throw std::runtime_error("cannot size " + path.string());
  std::vector<std::byte> out(static_cast<std::size_t>(size));
  in.seekg(0);
  if (!out.empty()) {
    in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in) throw std::runtime_error("cannot read " + path.string());
  }
  return out;
}

bool is_general_archive(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::array<char, 12> header{};
  in.read(header.data(), static_cast<std::streamsize>(header.size()));
  if (!in) return false;
  return header[0] == 'B' && header[1] == 'T' && header[2] == 'D' && header[3] == 'X' &&
         header[8] == 'G' && header[9] == 'N' && header[10] == 'R' && header[11] == 'L';
}

struct UvdPath {
  std::string plugin_lower;
  std::uint32_t value{0};
};

std::optional<UvdPath> parse_uvd_path(std::string_view stored_path) {
  const auto path = normalized(stored_path);
  const auto parts = split_path(path);
  if (parts.size() != 3 || parts[0] != "vis") return std::nullopt;
  const auto file = parts[2];
  if (file.size() != 12 || !ends_with(file, ".uvd")) return std::nullopt;
  const auto value = hex8(file.substr(0, 8));
  if (!value) return std::nullopt;
  return UvdPath{std::string(parts[1]), *value & 0xFFFFFFu};
}

struct OcNifPath {
  std::string plugin_lower;
  std::uint32_t cell{0};
  std::uint32_t id{0};
};

// An OC NIF without a plugin folder must belong to the base game plugin.
std::optional<OcNifPath> parse_oc_nif_path(std::string_view stored_path,
                                           std::string_view base_lower) {
  const auto path = normalized(stored_path);
  const auto parts = split_path(path);
  if (parts.size() != 3 && parts.size() != 4) return std::nullopt;
  if (parts[0] != "meshes" || parts[1] != "precombined") return std::nullopt;
  const std::string plugin = parts.size() == 4 ? std::string(parts[2]) : std::string(base_lower);
  const auto file = parts.back();
  if (file.size() != 24 || !ends_with(file, k_oc_nif_suffix)) return std::nullopt;
  if (file[8] != '_') return std::nullopt;
  const auto cell = hex8(file.substr(0, 8));
  const auto id = hex8(file.substr(9, 8));
  if (!cell || !id) return std::nullopt;
  return OcNifPath{plugin, *cell & 0xFFFFFFu, *id};
}

std::string tome_key(const std::string& plugin_lower, std::uint32_t value) {
  return plugin_lower + '#' + std::to_string(value);
}

}  // namespace

bool PrevisData::encode_in_plugin(const FormKey& key, std::size_t plugin,
                                  std::uint32_t& out) const {
  if (env_ == nullptr || plugin >= env_->plugins().size()) return false;
  const auto& info = env_->plugins()[plugin];
  if (key.owner().empty()) return false;
  std::size_t index = 0;
  bool found = false;
  for (; index < info.masters.size(); ++index) {
    if (info.masters[index] == key.owner()) {
      found = true;
      break;
    }
  }
  if (!found) {
    if (!(info.key == key.owner())) return false;
    index = info.masters.size();
  }
  out = (static_cast<std::uint32_t>(index) << 24u) | (key.local().value() & 0xFFFFFFu);
  return true;
}

std::optional<std::size_t> PrevisData::owner_plugin(const FormKey& cell) const {
  if (cell.owner().empty()) return std::nullopt;
  const auto it = plugin_by_name_.find(lowered(cell.owner().filename()));
  if (it == plugin_by_name_.end()) return std::nullopt;
  return it->second;
}

std::string PrevisData::base_plugin_lower() const {
  if (!base_known_ || env_ == nullptr) return {};
  return lowered(env_->plugins().at(base_plugin_).key.filename());
}

bool PrevisData::plugin_ships_previs(std::size_t plugin) const {
  return cdx_present_.contains(plugin) || csg_present_.contains(plugin);
}

std::optional<bool> PrevisData::cdx_answer(std::size_t plugin,
                                           const CdxCellTarget& target) const {
  if (!cdx_complete_ || cdx_unreadable_.contains(plugin)) return std::nullopt;
  const auto index = cdx_.find(plugin);
  if (index == cdx_.end()) return false;
  std::uint32_t raw = 0;
  if (target.world && target.grid) {
    if (!encode_in_plugin(*target.world, plugin, raw)) return std::nullopt;
    return index->second.has_exterior_row(raw, CdxGrid{target.grid->x, target.grid->y});
  }
  if (!encode_in_plugin(target.cell, plugin, raw)) return std::nullopt;
  return index->second.has_interior_row(raw);
}

std::optional<std::uint32_t> PrevisData::missing_oc_nifs(
    const FormKey& cell, std::span<const std::uint32_t> mesh_ids) const {
  if (!nif_complete_) return std::nullopt;
  const auto plugin = owner_plugin(cell);
  if (!plugin) return std::nullopt;
  const auto it = nifs_.find(*plugin);
  const std::uint32_t cell_low = cell.local().value() & 0xFFFFFFu;
  std::uint32_t missing = 0;
  for (const auto id : mesh_ids) {
    const std::uint64_t key = static_cast<std::uint64_t>(cell_low) << 32u | id;
    if (it == nifs_.end() || !it->second.entries.contains(key)) ++missing;
  }
  return missing;
}

TomeLookup PrevisData::tome_answer(const FormKey& tome_cell) const {
  TomeLookup answer;
  if (!tome_complete_ || tome_cell.owner().empty()) return answer;
  answer.checked = true;
  const std::string key =
      tome_key(lowered(tome_cell.owner().filename()), tome_cell.local().value() & 0xFFFFFFu);
  if (const auto cached = tome_cache_.find(key); cached != tome_cache_.end()) {
    return cached->second;
  }
  const auto found = tomes_.find(key);
  if (found == tomes_.end()) {
    tome_cache_.emplace(key, answer);
    return answer;
  }
  answer.found = true;
  try {
    std::vector<std::byte> bytes =
        found->second.loose ? read_file(found->second.path)
                            : archives_.at(found->second.archive).extract(found->second.entry);
    answer.issues = validate_uvd(bytes);
  } catch (const std::exception& e) {
    answer.issues.push_back(std::string("tome read failed: ") + e.what());
  }
  tome_cache_.emplace(key, answer);
  return answer;
}

LoadOrderIndexes PrevisData::indexes() const {
  LoadOrderIndexes out;
  out.cdx_row = [this](std::size_t plugin, const CdxCellTarget& target) {
    return cdx_answer(plugin, target);
  };
  out.missing_oc_nifs = [this](const FormKey& cell, std::span<const std::uint32_t> mesh_ids) {
    return missing_oc_nifs(cell, mesh_ids);
  };
  out.uvd = [this](const FormKey& tome_cell) { return tome_answer(tome_cell); };
  out.plugin_ships_previs = [this](std::size_t plugin) { return plugin_ships_previs(plugin); };
  return out;
}

void PrevisData::add_tome(const std::string& plugin_lower, std::uint32_t value,
                          TomeLocation location) {
  ++summary_.tome_files;
  if (!plugin_by_name_.contains(plugin_lower)) return;
  tomes_.try_emplace(tome_key(plugin_lower, value), std::move(location));
}

void PrevisData::add_nif(const std::string& plugin_lower, std::uint32_t cell, std::uint32_t id) {
  ++summary_.oc_nif_files;
  const auto plugin = plugin_by_name_.find(plugin_lower);
  if (plugin == plugin_by_name_.end()) return;
  auto& index = nifs_[plugin->second];
  index.cells.insert(cell);
  index.entries.insert(static_cast<std::uint64_t>(cell) << 32u | id);
}

void PrevisData::scan_archives(const fs::path& root) {
  std::error_code error;
  std::vector<fs::path> paths;
  for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
    std::error_code file_error;
    if (!it->is_regular_file(file_error)) continue;
    if (lowered(it->path().extension().string()) != ".ba2") continue;
    paths.push_back(it->path());
  }
  std::sort(paths.begin(), paths.end());
  const std::string base_lower = base_plugin_lower();

  for (const auto& path : paths) {
    if (!is_general_archive(path)) continue;
    try {
      archives_.push_back(archive::Ba2Reader::open(path));
    } catch (const std::exception& e) {
      summary_.problems.push_back("BA2 unreadable: " + path.filename().string() + ": " + e.what());
      nif_complete_ = false;
      tome_complete_ = false;
      continue;
    }
    const auto& reader = archives_.back();
    const std::size_t archive_index = archives_.size() - 1;
    const auto& entries = reader.entries();
    for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
      const auto& stored = entries[entry_index].path;
      if (const auto uvd = parse_uvd_path(stored)) {
        add_tome(uvd->plugin_lower, uvd->value, TomeLocation{false, archive_index, entry_index, {}});
        continue;
      }
      if (const auto nif = parse_oc_nif_path(stored, base_lower)) {
        add_nif(nif->plugin_lower, nif->cell, nif->id);
      }
    }
  }
}

void PrevisData::scan_loose(const fs::path& root) {
  std::error_code error;
  const auto vis_root = root / "Vis";
  if (fs::is_directory(vis_root, error)) {
    for (fs::recursive_directory_iterator it(vis_root, error), end; !error && it != end;
         it.increment(error)) {
      std::error_code file_error;
      if (!it->is_regular_file(file_error)) continue;
      if (lowered(it->path().extension().string()) != ".uvd") continue;
      const auto relative = it->path().lexically_relative(vis_root);
      if (relative.empty()) continue;
      const std::string relative_text = normalized(relative.string());
      const auto parts = split_path(relative_text);
      if (parts.size() < 2) continue;
      const auto file_name = lowered(it->path().filename().string());
      if (file_name.size() != 12) continue;
      const auto value = hex8(file_name.substr(0, 8));
      if (!value) continue;
      add_tome(lowered(parts[0]), *value & 0xFFFFFFu, TomeLocation{true, 0, 0, it->path()});
    }
  }
  const std::string base_lower = base_plugin_lower();
  const auto precombined = root / "Meshes" / "PreCombined";
  if (fs::is_directory(precombined, error)) {
    for (fs::recursive_directory_iterator it(precombined, error), end; !error && it != end;
         it.increment(error)) {
      std::error_code file_error;
      if (!it->is_regular_file(file_error)) continue;
      const auto file_name = lowered(it->path().filename().string());
      if (file_name.size() != 24 || !ends_with(file_name, k_oc_nif_suffix)) continue;
      const auto relative = it->path().lexically_relative(precombined);
      if (relative.empty()) continue;
      const std::string relative_text = normalized(relative.string());
      const auto parts = split_path(relative_text);
      const std::string plugin = parts.size() >= 2 ? lowered(parts[0]) : base_lower;
      if (file_name[8] != '_') continue;
      const auto cell = hex8(file_name.substr(0, 8));
      const auto id = hex8(file_name.substr(9, 8));
      if (!cell || !id) continue;
      add_nif(plugin, *cell & 0xFFFFFFu, *id);
    }
  }
}

void PrevisData::scan_cdx(const fs::path& root) {
  std::error_code error;
  for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
    std::error_code file_error;
    if (!it->is_regular_file(file_error)) continue;
    const auto path = it->path();
    if (lowered(path.extension().string()) != ".cdx") continue;
    ++summary_.cdx_files;
    const auto plugin = plugin_by_stem_.find(lowered(path.stem().string()));
    if (plugin == plugin_by_stem_.end()) continue;
    if (cdx_present_.contains(plugin->second) || cdx_unreadable_.contains(plugin->second)) continue;
    cdx_present_.insert(plugin->second);
    const std::string plugin_name{env_->plugins().at(plugin->second).key.filename()};
    try {
      const auto bytes = read_file(path);
      const auto issues = validate_cdx(bytes);
      if (!issues.empty()) {
        summary_.problems.push_back("CDX invalid for " + plugin_name + ": " + issues.front());
        cdx_unreadable_.insert(plugin->second);
        continue;
      }
      auto file = parse_cdx(bytes);
      summary_.cdx_rows += file.rows.size();
      cdx_.try_emplace(plugin->second, CdxIndex(file));
    } catch (const std::exception& e) {
      summary_.problems.push_back("CDX unreadable for " + plugin_name + ": " + e.what());
      cdx_unreadable_.insert(plugin->second);
    }
  }
}

void PrevisData::scan_csg(const fs::path& root) {
  std::error_code error;
  for (fs::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
    std::error_code file_error;
    if (!it->is_regular_file(file_error)) continue;
    const auto path = it->path();
    const auto name = lowered(path.filename().string());
    if (ends_with(name, ".psg")) {
      summary_.problems.push_back(
          "clean-mode .psg is unsupported; precombine geometry is unmeasured: " +
          path.filename().string());
      continue;
    }
    if (!ends_with(name, k_geometry_csg_suffix)) continue;
    ++summary_.csg_files;
    const auto stem = name.substr(0, name.size() - k_geometry_csg_suffix.size());
    const auto plugin = plugin_by_stem_.find(stem);
    if (plugin == plugin_by_stem_.end()) continue;
    if (csg_decided_.contains(plugin->second)) continue;
    csg_decided_.insert(plugin->second);
    csg_present_.insert(plugin->second);
    const std::string plugin_name{env_->plugins().at(plugin->second).key.filename()};
    try {
      const auto bytes = read_file(path);
      const auto issues = validate_csg(bytes);
      if (!issues.empty()) {
        summary_.problems.push_back("CSG invalid for " + plugin_name + ": " + issues.front());
        continue;
      }
      std::uint32_t header[3]{};
      std::memcpy(header, bytes.data(), sizeof(header));
      summary_.csg_rows += header[1];
      summary_.csg_chunks += header[2];
    } catch (const std::exception& e) {
      summary_.problems.push_back("CSG unreadable for " + plugin_name + ": " + e.what());
    }
  }
}

void PrevisData::check_csg_containers() {
  for (const auto plugin : cdx_present_) {
    if (csg_present_.contains(plugin)) continue;
    const std::string plugin_name{env_->plugins().at(plugin).key.filename()};
    summary_.problems.push_back("CSG missing for " + plugin_name +
                                ": the plugin has a CDX but no matching - Geometry.csg");
  }
}

PrevisData PrevisData::load(std::span<const fs::path> asset_roots, const env::Environment& env) {
  PrevisData data;
  data.env_ = &env;
  const auto& plugins = env.plugins();
  if (plugins.empty()) {
    data.summary_.problems.push_back("the environment has no plugins");
    data.cdx_complete_ = false;
    data.nif_complete_ = false;
    data.tome_complete_ = false;
    return data;
  }
  if (const auto base = env.find_plugin("Fallout4.esm")) {
    data.base_known_ = true;
    data.base_plugin_ = *base;
  } else {
    data.nif_complete_ = false;
    data.summary_.problems.push_back(
        "Fallout4.esm is absent from the scanned load order: OC NIFs without a plugin folder "
        "cannot be attributed");
  }
  for (std::size_t index = 0; index < plugins.size(); ++index) {
    const std::string filename = lowered(plugins[index].key.filename());
    data.plugin_by_name_.emplace(filename, index);
    const auto dot = filename.find_last_of('.');
    data.plugin_by_stem_.emplace(dot == std::string::npos ? filename : filename.substr(0, dot),
                                 index);
  }
  // Root priority invariant: the highest root must be scanned first, and loose must beat archives.
  for (auto root = asset_roots.rbegin(); root != asset_roots.rend(); ++root) {
    if (root->empty()) continue;
    data.scan_loose(*root);
    data.scan_archives(*root);
    data.scan_cdx(*root);
    data.scan_csg(*root);
  }
  data.check_csg_containers();
  return data;
}
}  // namespace fo4::previs
