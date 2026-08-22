#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct BtrSubIndexSegment {
    std::uint32_t triangle_offset{};
    std::uint32_t triangle_count{};
    std::uint32_t unknown_hash{};
    std::uint32_t num_segments{};
};

struct BtrGeometryInspection {
    bool valid{};
    std::string error;
    std::string type;
    std::int32_t name_index{-1};
    std::uint16_t av_flags{};
    std::uint16_t av_flags2{};
    float translation[3]{};
    float rotation[9]{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
    };
    float scale{1.0F};
    float center[3]{};
    float radius{};
    std::int32_t skin_instance{-1};
    std::int32_t shader_properties[2]{-1, -1};
    std::uint8_t vertex_size_words{};
    std::uint8_t float_size_words{};
    std::uint8_t vertex_flag3{};
    std::uint8_t vertex_flag4{};
    std::uint8_t vertex_flag5{};
    std::uint16_t vertex_flags{};
    std::uint8_t vertex_flag8{};
    std::uint32_t triangle_count{};
    std::uint16_t vertex_count{};
    std::uint32_t data_size{};
    std::uint32_t vertex_stride{};
    std::uint64_t expected_geometry_bytes{};
    bool full_precision_positions{};
    bool has_position_range{};
    float position_min[3]{};
    float position_max[3]{};
    bool has_uv_range{};
    float uv_min[2]{};
    float uv_max[2]{};
    std::uint32_t subindex_triangle_count{};
    std::uint32_t subindex_a{};
    std::uint32_t subindex_b{};
    std::vector<BtrSubIndexSegment> subindex_segments;
};

[[nodiscard]] BtrGeometryInspection inspect_fallout4_btr_geometry(
    const std::filesystem::path& path,
    std::int32_t block_index);

} // namespace f4lod
