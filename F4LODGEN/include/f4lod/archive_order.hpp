#pragma once

#include "f4lod/ba2.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

enum class ArchiveOrderSource {
    ini,
    plugin,
};

struct ArchiveOrderEntry {
    std::filesystem::path path;
    ArchiveOrderSource source{ArchiveOrderSource::plugin};
    std::string owner;
    Ba2ArchiveType type{Ba2ArchiveType::unknown};
};

struct ArchiveOrderOptions {
    std::filesystem::path data_root;
    std::filesystem::path primary_ini;
    std::filesystem::path custom_ini;
    std::vector<std::string> enabled_plugins_low_to_high;
};

struct ArchiveOrderResult {
    bool valid{};
    std::string error;
    std::vector<std::string> warnings;
    std::vector<ArchiveOrderEntry> entries_low_to_high;
    // Authoritative combined archive precedence used by AssetResolver.
    std::vector<Ba2Inspection> archives_low_to_high;
    std::size_t general_archive_count{};
    std::size_t texture_archive_count{};
};

// Builds the Fallout 4 BA2 order used by the asset resolver.
//
// Order is low -> high priority:
//   1. [Archive] INI lists in Fallout 4/xEdit order.
//   2. BA2s associated with each enabled plugin basename, in plugin order.
//
// General and DX10 texture BA2s stay in one combined order. Duplicate archives
// keep their first position, matching xEdit's behavior of not re-adding a
// container that was already loaded.
[[nodiscard]] ArchiveOrderResult build_fallout4_archive_order(
    const ArchiveOrderOptions& options);

[[nodiscard]] const char* archive_order_source_name(ArchiveOrderSource source) noexcept;

} // namespace f4lod
