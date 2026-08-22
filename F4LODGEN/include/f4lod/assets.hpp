#pragma once

#include "f4lod/ba2.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace f4lod {

enum class AssetSourceKind {
    none,
    loose,
    general_ba2,
    texture_ba2,
};

struct AssetSource {
    AssetSourceKind kind{AssetSourceKind::none};
    std::filesystem::path container;
    std::string game_path;
};

struct AssetReadResult {
    bool valid{};
    std::string error;
    AssetSource source;
    std::vector<unsigned char> data;
};

struct AssetResolution {
    bool valid{};
    std::string error;
    std::string game_path;
    std::vector<AssetSource> candidates_low_to_high;
    std::optional<std::size_t> winner_index;

    [[nodiscard]] const AssetSource* winner() const noexcept;
};

struct AssetArchiveDiscovery {
    bool valid{};
    std::string error;
    std::string game_path;
    bool loose_exists{};
    std::filesystem::path loose_path;
    std::vector<Ba2Inspection> matching_archives;
};

class AssetResolver {
public:
    // Archives are supplied in effective low -> high priority order and may be
    // either General or DX10 texture BA2s. Exactly one copy of each indexed
    // archive is retained so large modlists do not duplicate filename/chunk
    // tables simply for type-specific views.
    AssetResolver(
        std::filesystem::path data_root,
        std::vector<Ba2Inspection> archives_low_to_high);

    [[nodiscard]] const std::filesystem::path& data_root() const noexcept;
    [[nodiscard]] std::span<const Ba2Inspection> archives() const noexcept;
    [[nodiscard]] AssetResolution resolve(const std::string& game_path) const;
    [[nodiscard]] AssetReadResult read(const std::string& game_path) const;

private:
    std::filesystem::path data_root_;
    std::vector<Ba2Inspection> archives_low_to_high_;
};

[[nodiscard]] AssetArchiveDiscovery discover_asset_sources(
    const std::filesystem::path& data_root,
    const std::string& game_path);

[[nodiscard]] const char* asset_source_kind_name(AssetSourceKind kind) noexcept;

} // namespace f4lod
