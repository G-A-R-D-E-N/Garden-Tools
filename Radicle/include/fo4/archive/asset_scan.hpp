#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/plugin/container.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fo4::archive {

std::vector<std::string> record_asset_paths(const Record& record);

struct MissingAsset {
  FormKey record;
  std::string path;
};

std::vector<MissingAsset> find_missing_assets(const Plugin& plugin, const AssetResolver& resolver);

struct AssetReference {
  FormKey record;
  std::string path;
};

std::vector<std::string> record_schema_asset_strings(const Record& record);

std::vector<AssetReference> collect_asset_references(const Plugin& plugin);

// Requires: path is accepted by normalize_asset_path, which throws AssetPathError otherwise.
std::vector<std::string> asset_path_candidates(std::string_view path);

}  // namespace fo4::archive
