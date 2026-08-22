#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct NifBlockInfo {
    std::string type;
    std::uint32_t size{};
    std::uint64_t offset{};
};

struct NifInspection {
    std::filesystem::path path;
    bool valid{};
    std::string error;
    std::string header_string;
    std::uint32_t version{};
    std::uint8_t endian_type{};
    std::uint32_t user_version{};
    std::uint32_t user_version2{};
    std::string creator;
    std::vector<std::string> block_types;
    std::vector<NifBlockInfo> blocks;
    std::vector<std::string> strings;
    std::uint64_t data_offset{};
    std::uint64_t footer_offset{};
    std::vector<std::int32_t> roots;

    [[nodiscard]] bool is_fallout4() const noexcept;
};

struct BtrInspection {
    bool valid{};
    std::string error;
    NifInspection nif;
    std::int32_t root_name_index{-1};
    std::vector<std::int32_t> root_children;
    std::int32_t root_multibound{-1};
    std::uint32_t root_cull_mode{};
    std::int32_t bounds_data{-1};
    float bounds_position[3]{};
    float bounds_extent[3]{};
    std::int32_t terrain_shape{-1};
    std::int32_t water_node{-1};
};

[[nodiscard]] NifInspection inspect_nif(const std::filesystem::path& path);
[[nodiscard]] BtrInspection inspect_fallout4_btr(const std::filesystem::path& path);

} // namespace f4lod
