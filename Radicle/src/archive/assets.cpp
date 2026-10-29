#include "fo4/archive/assets.hpp"

#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>

namespace fo4::archive {
namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::filesystem::path from_utf8(std::string_view text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

using io::utf8;

std::string root_key(const std::filesystem::path& root) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(root, ec);
  const auto key = utf8(ec ? root : canonical);
#if defined(_WIN32)
  return text::fold(key);
#else
  return key;
#endif
}

std::vector<std::pair<std::string, std::filesystem::path>> list_files(const std::filesystem::path& dir) {
  std::vector<std::pair<std::string, std::filesystem::path>> files;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file(ec)) files.emplace_back(text::fold(utf8(entry.path().filename())), entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::optional<std::filesystem::path> child(const std::filesystem::path& dir, std::string_view name, bool want_dir) {
  std::error_code ec;
  const auto exact = dir / from_utf8(name);
  if (want_dir ? std::filesystem::is_directory(exact, ec) : std::filesystem::is_regular_file(exact, ec)) {
    return exact;
  }
  if (!std::filesystem::is_directory(dir, ec)) return std::nullopt;
  std::optional<std::filesystem::path> found;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const bool kind = want_dir ? entry.is_directory(ec) : entry.is_regular_file(ec);
    if (!kind || !text::equals_folded(utf8(entry.path().filename()), name)) continue;
    if (!found || entry.path() < *found) found = entry.path();
  }
  return found;
}

void walk_loose(const std::filesystem::path& dir, const std::filesystem::path& root, int depth,
                std::vector<std::pair<std::string, std::filesystem::path>>& out,
                std::set<std::string>& ancestors) {
  constexpr int k_max_depth = 64;
  std::error_code ec;
  std::vector<std::filesystem::path> children;
  for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    children.push_back(it->path());
  }
  std::sort(children.begin(), children.end());
  for (const auto& path : children) {
    std::error_code entry_ec;
    if (std::filesystem::is_directory(path, entry_ec)) {
      // Constraint: only the ancestor chain is deduped, and a sibling link must list its own keys.
      const auto key = root_key(path);
      if (depth < k_max_depth && ancestors.insert(key).second) {
        walk_loose(path, root, depth + 1, out, ancestors);
        ancestors.erase(key);
      }
      continue;
    }
    if (!std::filesystem::is_regular_file(path, entry_ec)) continue;
    const auto relative = path.lexically_relative(root);
    if (text::equals_folded(utf8(*relative.begin()), "data")) continue;
    try {
      out.emplace_back(normalize_asset_path(utf8(relative)), path);
    } catch (const AssetPathError&) {
    }
  }
}

}  // namespace

std::string normalize_asset_path(std::string_view path) {
  if (path.find(':') != std::string_view::npos) throw AssetPathError("asset path must not contain a drive or stream name");
  auto text = lower(path);
  std::replace(text.begin(), text.end(), '\\', '/');
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = std::min(text.find('/', start), text.size());
    const auto part = text.substr(start, end - start);
    if (part == "." || part == "..") throw AssetPathError("asset path must not contain . or .. components");
    if (!part.empty()) parts.push_back(part);
    start = end + 1;
  }
  if (!parts.empty() && parts.front() == "data") parts.erase(parts.begin());
  if (parts.empty()) throw AssetPathError("asset path is empty");
  std::string out;
  for (const auto& part : parts) {
    if (!out.empty()) out.push_back('/');
    out += part;
  }
  return out;
}

std::vector<std::filesystem::path> archive_load_order(const std::filesystem::path& data_dir,
                                                      std::span<const std::string> ini_archives,
                                                      std::span<const std::string> plugins) {
  const auto files = list_files(data_dir);
  std::vector<std::filesystem::path> order;
  std::set<std::string> added;
  const auto add = [&](const std::string& name, const std::filesystem::path& path) {
    if (added.insert(name).second) order.push_back(path);
  };
  for (const auto& listed : ini_archives) {
    const auto wanted = text::fold(listed);
    const auto it = std::lower_bound(files.begin(), files.end(), std::make_pair(wanted, std::filesystem::path{}));
    if (it != files.end() && it->first == wanted) add(it->first, it->second);
  }
  for (const auto& plugin : plugins) {
    auto stem = text::fold(plugin);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
    for (const auto& [name, path] : files) {
      if (name == stem + ".ba2") add(name, path);
    }
    for (const auto& [name, path] : files) {
      if (!name.ends_with(".ba2")) continue;
      const std::string_view base(name.data(), name.size() - 4);
      const auto separator = base.rfind(" - ");
      if (separator != std::string_view::npos && separator + 3 < base.size() && base.substr(0, separator) == stem) {
        add(name, path);
      }
    }
  }
  return order;
}

AssetResolver AssetResolver::open(const std::filesystem::path& data_dir,
                                  std::span<const std::filesystem::path> archives) {
  if (data_dir.empty()) return open_roots({}, archives);
  return open_roots(std::span<const std::filesystem::path>(&data_dir, 1), archives);
}

AssetResolver AssetResolver::open_roots(std::span<const std::filesystem::path> roots,
                                        std::span<const std::filesystem::path> archives) {
  AssetResolver resolver;
  std::set<std::string> seen;
  for (const auto& root : roots) {
    if (!root.empty() && seen.insert(root_key(root)).second) resolver.roots_.push_back(root);
  }
  resolver.archives_.reserve(archives.size());
  for (const auto& path : archives) resolver.archives_.push_back(Ba2Reader::open(path));
  return resolver;
}

std::optional<AssetLocation> AssetResolver::locate(std::string_view data_relative_path) const {
  const auto key = normalize_asset_path(data_relative_path);
  for (const auto& root : roots_) {
    std::optional<std::filesystem::path> current = root;
    std::size_t start = 0;
    while (current) {
      const auto slash = key.find('/', start);
      const bool last = slash == std::string::npos;
      current = child(*current, std::string_view(key).substr(start, last ? std::string::npos : slash - start), !last);
      if (last) break;
      start = slash + 1;
    }
    if (current) return AssetLocation{AssetOrigin::loose, *current, 0, 0};
  }
  return locate_in_archives(key);
}

std::optional<AssetLocation> AssetResolver::locate_in_archives(std::string_view data_relative_path) const {
  const auto key = normalize_asset_path(data_relative_path);
  for (std::size_t i = archives_.size(); i-- > 0;) {
    if (const auto entry = archives_[i].find(key)) {
      return AssetLocation{AssetOrigin::archive, {}, i, *entry};
    }
  }
  return std::nullopt;
}

std::vector<AssetListing> AssetResolver::list(std::string_view prefix, std::string_view extension,
                                              bool include_shadowed) const {
  const auto wanted_prefix = prefix.empty() ? std::string() : normalize_asset_path(prefix);
  auto wanted_extension = lower(extension);
  if (!wanted_extension.empty() && wanted_extension.front() != '.') {
    wanted_extension.insert(wanted_extension.begin(), '.');
  }
  const auto wanted = [&](const std::string& path) {
    if (!wanted_prefix.empty() && path != wanted_prefix &&
        !(path.size() > wanted_prefix.size() && path.starts_with(wanted_prefix) && path[wanted_prefix.size()] == '/')) {
      return false;
    }
    if (wanted_extension.empty()) return true;
    // Constraint: the filter must match as a whole suffix starting on a dot.
    return path.ends_with(wanted_extension);
  };
  std::vector<AssetListing> found;
  std::set<std::string> keys;
  for (const auto& root : roots_) {
    std::vector<std::pair<std::string, std::filesystem::path>> loose;
    std::set<std::string> ancestors{root_key(root)};
    walk_loose(root, root, 0, loose, ancestors);
    for (auto& [key, path] : loose) {
      if (!wanted(key)) continue;
      std::error_code ec;
      const auto size = std::filesystem::file_size(path, ec);
      keys.insert(key);
      found.push_back({std::move(key), {AssetOrigin::loose, std::move(path), 0, 0}, ec ? 0 : size, false});
    }
  }
  std::vector<std::vector<AssetListing>> per_archive(archives_.size());
  for (std::size_t i = 0; i < archives_.size(); ++i) {
    if (!archives_[i].has_name_table()) continue;
    const auto& entries = archives_[i].entries();
    for (std::size_t e = 0; e < entries.size(); ++e) {
      if (entries[e].path.empty()) continue;
      std::string key;
      try {
        key = normalize_asset_path(entries[e].path);
      } catch (const AssetPathError&) {
        continue;
      }
      if (!wanted(key)) continue;
      const auto resolved = archives_[i].find(key);
      if (!resolved || *resolved != e) continue;
      keys.insert(key);
      per_archive[i].push_back({std::move(key), {AssetOrigin::archive, {}, i, e}, entries[e].unpacked_size(), false});
    }
  }
  for (std::size_t i = 0; i < archives_.size(); ++i) {
    if (archives_[i].has_name_table()) continue;
    for (const auto& key : keys) {
      if (const auto entry = archives_[i].find(key)) {
        per_archive[i].push_back({key, {AssetOrigin::archive, {}, i, *entry}, archives_[i].entries()[*entry].unpacked_size(), false});
      }
    }
  }
  for (std::size_t i = archives_.size(); i-- > 0;) {
    for (auto& item : per_archive[i]) found.push_back(std::move(item));
  }
  std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.path < y.path; });
  std::vector<AssetListing> out;
  for (auto& item : found) {
    const bool shadowed = !out.empty() && out.back().path == item.path;
    if (shadowed && !include_shadowed) continue;
    item.shadowed = shadowed;
    out.push_back(std::move(item));
  }
  return out;
}

std::vector<std::byte> AssetResolver::read(const AssetLocation& location) const {
  if (location.origin == AssetOrigin::archive) return archives_.at(location.archive).extract(location.entry);
  std::ifstream stream(location.loose_path, std::ios::binary | std::ios::ate);
  if (!stream) throw Ba2Error("could not open loose asset: " + utf8(location.loose_path));
  const auto end = stream.tellg();
  if (end < 0) throw Ba2Error("could not size loose asset: " + utf8(location.loose_path));
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  stream.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw Ba2Error("could not read loose asset: " + utf8(location.loose_path));
  }
  return bytes;
}

}  // namespace fo4::archive
