#pragma once

#include "fo4/archive/ba2_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::archive {

class AssetPathError : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

std::vector<std::filesystem::path> archive_load_order(const std::filesystem::path& data_dir,
                                                      std::span<const std::string> ini_archives,
                                                      std::span<const std::string> plugins);

std::string normalize_asset_path(std::string_view path);

enum class AssetOrigin : std::uint8_t {
  loose,
  archive,
};

struct AssetLocation {
  AssetOrigin origin{AssetOrigin::loose};
  std::filesystem::path loose_path;
  std::size_t archive{0};
  std::size_t entry{0};
};

struct AssetListing {
  std::string path;
  AssetLocation location;
  std::uint64_t size{0};
  bool shadowed{false};
};

// Immutable after open; const members are thread-safe.
class AssetResolver {
public:
  static AssetResolver open(const std::filesystem::path& data_dir,
                            std::span<const std::filesystem::path> archives);
  static AssetResolver open_roots(std::span<const std::filesystem::path> roots,
                                  std::span<const std::filesystem::path> archives);

  const std::vector<std::filesystem::path>& roots() const noexcept { return roots_; }
  const std::vector<Ba2Reader>& archives() const noexcept { return archives_; }

  std::optional<AssetLocation> locate(std::string_view data_relative_path) const;
  std::optional<AssetLocation> locate_in_archives(std::string_view data_relative_path) const;
  std::vector<std::byte> read(const AssetLocation& location) const;
  std::vector<AssetListing> list(std::string_view prefix, std::string_view extension, bool include_shadowed) const;

private:
  std::vector<std::filesystem::path> roots_;
  std::vector<Ba2Reader> archives_;
};

}  // namespace fo4::archive
