#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct PluginRecordCounts {
    std::size_t total_records{};
    std::size_t groups{};
    std::size_t worldspaces{};
    std::size_t cells{};
    std::size_t landscapes{};
    std::size_t landscape_textures{};
    std::size_t texture_sets{};
    std::size_t references{};
    std::size_t statics{};
};

struct PluginInspection {
    std::filesystem::path path;
    bool valid{};
    std::string error;
    std::vector<std::string> masters;
    PluginRecordCounts records;
};

struct PluginRecordHeader {
    std::string type;
    std::uint32_t flags{};
    std::uint32_t raw_form_id{};
    std::uint32_t data_size{};
    std::uint64_t file_offset{};
    std::uint32_t parent_world_raw_form_id{};
    std::uint32_t parent_cell_raw_form_id{};

    [[nodiscard]] bool compressed() const noexcept {
        return (flags & 0x00040000U) != 0;
    }
};

struct PluginRecordIndex {
    PluginInspection inspection;
    // LOD-critical records include terrain/object records and their direct
    // terrain-texture dependencies (LTEX/TXST).
    std::vector<PluginRecordHeader> lod_records;
};

[[nodiscard]] PluginInspection inspect_plugin(const std::filesystem::path& path);
[[nodiscard]] PluginRecordIndex index_lod_records(const std::filesystem::path& path);
[[nodiscard]] std::string format_plugin_inspection_text(const PluginInspection& inspection);
[[nodiscard]] std::string format_plugin_inspection_json(const PluginInspection& inspection);

} // namespace f4lod
