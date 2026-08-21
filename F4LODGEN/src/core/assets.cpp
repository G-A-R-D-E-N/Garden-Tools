#include "f4lod/assets.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace f4lod {
namespace {

constexpr std::uint64_t kMaxAssetSize = 1024ULL * 1024ULL * 1024ULL;

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<std::string> normalize_game_path(const std::string& input) {
    if (input.empty()) return std::nullopt;

    std::string normalized = input;
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    if (normalized.front() == '/' || normalized.find(':') != std::string::npos) return std::nullopt;

    std::stringstream source(normalized);
    std::string component;
    std::vector<std::string> components;
    while (std::getline(source, component, '/')) {
        if (component.empty() || component == ".") continue;
        if (component == "..") return std::nullopt;
        components.push_back(std::move(component));
    }
    if (components.empty()) return std::nullopt;

    std::ostringstream out;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i != 0U) out << '/';
        out << components[i];
    }
    return out.str();
}

std::string lookup_key(const std::string& path) {
    auto normalized = normalize_game_path(path);
    return normalized.has_value() ? lower_ascii(*normalized) : std::string{};
}

bool archive_contains(const Ba2Inspection& archive, const std::string& key) {
    for (const auto& name : archive.files) {
        if (lookup_key(name) == key) return true;
    }
    return false;
}

AssetReadResult read_loose_file(
    const std::filesystem::path& path,
    const std::string& game_path) {
    AssetReadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "Could not open loose asset.";
        return result;
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        result.error = "Could not determine loose asset size.";
        return result;
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > kMaxAssetSize) {
        result.error = "Loose asset exceeds the supported size limit.";
        return result;
    }
    input.seekg(0, std::ios::beg);

    result.data.resize(static_cast<std::size_t>(size));
    if (!result.data.empty()) {
        input.read(reinterpret_cast<char*>(result.data.data()), static_cast<std::streamsize>(result.data.size()));
        if (input.gcount() != static_cast<std::streamsize>(result.data.size())) {
            result.data.clear();
            result.error = "Loose asset is truncated while reading.";
            return result;
        }
    }

    result.valid = true;
    result.source.kind = AssetSourceKind::loose;
    result.source.container = path;
    result.source.game_path = game_path;
    return result;
}

} // namespace

AssetResolver::AssetResolver(
    std::filesystem::path data_root,
    std::vector<Ba2Inspection> archives_low_to_high)
    : data_root_(std::move(data_root)),
      archives_low_to_high_(std::move(archives_low_to_high)) {
}

const std::filesystem::path& AssetResolver::data_root() const noexcept {
    return data_root_;
}

std::span<const Ba2Inspection> AssetResolver::archives() const noexcept {
    return archives_low_to_high_;
}

AssetReadResult AssetResolver::read(const std::string& game_path) const {
    AssetReadResult result;
    const auto normalized = normalize_game_path(game_path);
    if (!normalized.has_value()) {
        result.error = "Asset path is empty, absolute, or contains path traversal.";
        return result;
    }
    const auto key = lower_ascii(*normalized);

    const auto loose_path = data_root_ / std::filesystem::path(*normalized);
    std::error_code ec;
    if (std::filesystem::is_regular_file(loose_path, ec) && !ec) {
        return read_loose_file(loose_path, *normalized);
    }

    for (auto it = archives_low_to_high_.rbegin(); it != archives_low_to_high_.rend(); ++it) {
        if (!it->valid || !archive_contains(*it, key)) continue;

        Ba2Extraction extraction;
        AssetSourceKind source_kind = AssetSourceKind::none;
        if (it->type == Ba2ArchiveType::general) {
            extraction = extract_ba2_general_file(*it, *normalized);
            source_kind = AssetSourceKind::general_ba2;
        } else if (it->type == Ba2ArchiveType::texture) {
            extraction = extract_ba2_texture_file(*it, *normalized);
            source_kind = AssetSourceKind::texture_ba2;
        } else {
            continue;
        }

        if (!extraction.valid) {
            result.error = "Failed to extract winning BA2 asset from " + it->path.string() + ": " + extraction.error;
            return result;
        }

        result.valid = true;
        result.data = std::move(extraction.data);
        result.source.kind = source_kind;
        result.source.container = it->path;
        result.source.game_path = *normalized;
        return result;
    }

    result.error = "Asset was not found as a loose file or in the configured BA2 archive order.";
    result.source.game_path = *normalized;
    return result;
}

AssetArchiveDiscovery discover_asset_sources(
    const std::filesystem::path& data_root,
    const std::string& game_path) {
    AssetArchiveDiscovery result;
    const auto normalized = normalize_game_path(game_path);
    if (!normalized.has_value()) {
        result.error = "Asset path is empty, absolute, or contains path traversal.";
        return result;
    }
    result.game_path = *normalized;

    std::error_code ec;
    if (!std::filesystem::is_directory(data_root, ec) || ec) {
        result.error = "Data directory does not exist or is not readable.";
        return result;
    }

    result.loose_path = data_root / std::filesystem::path(*normalized);
    result.loose_exists = std::filesystem::is_regular_file(result.loose_path, ec) && !ec;
    ec.clear();
    if (result.loose_exists) {
        result.valid = true;
        return result;
    }

    const auto key = lower_ascii(*normalized);
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(data_root, options, ec);
    std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            result.error = "Could not enumerate every Data-directory entry while discovering packed asset sources.";
            return result;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (lower_ascii(it->path().extension().string()) != ".ba2") continue;

        auto archive = inspect_ba2_index(it->path());
        if (!archive.valid) {
            result.error = "Could not index BA2 archive " + it->path().string() + ": " + archive.error;
            return result;
        }
        if (archive_contains(archive, key)) result.matching_archives.push_back(std::move(archive));
    }

    std::sort(
        result.matching_archives.begin(),
        result.matching_archives.end(),
        [](const Ba2Inspection& lhs, const Ba2Inspection& rhs) {
            return lower_ascii(lhs.path.generic_string()) < lower_ascii(rhs.path.generic_string());
        });
    result.valid = true;
    return result;
}

const char* asset_source_kind_name(AssetSourceKind kind) noexcept {
    switch (kind) {
    case AssetSourceKind::none: return "none";
    case AssetSourceKind::loose: return "loose";
    case AssetSourceKind::general_ba2: return "general-ba2";
    case AssetSourceKind::texture_ba2: return "texture-ba2";
    }
    return "none";
}

} // namespace f4lod
