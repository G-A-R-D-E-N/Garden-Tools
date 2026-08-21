#pragma once

#include "f4lod/nif_writer.hpp"
#include "f4lod/terrain_simplify.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

enum class TerrainBtrVertexPrecision {
    half,
    full,
};

struct TerrainBtrTransform {
    float translation_x{};
    float translation_y{};
    float translation_z{};
    float rotation[9]{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
    };
    float scale{1.0F};
};

struct TerrainBtrBuildOptions {
    TerrainBtrTransform shape_transform;
    float uv_origin_x{};
    float uv_origin_y{};
    float uv_span_x{1.0F};
    float uv_span_y{1.0F};
    TerrainBtrVertexPrecision position_precision{TerrainBtrVertexPrecision::full};
    std::uint16_t av_flags{14U};
    std::uint16_t av_flags2{};
    std::uint8_t vertex_flag3{101U};
    std::uint8_t vertex_flag4{};
    std::uint8_t vertex_flag5{};
    std::uint8_t vertex_flag8{};
    std::int32_t shader_property_0{-1};
    std::int32_t shader_property_1{-1};
    std::string shape_name{"obj"};
    std::vector<std::string> additional_strings;
    std::vector<NifWriteBlock> additional_blocks;
};

struct TerrainBtrDocument {
    bool valid{};
    bool renderable{};
    std::string error;
    std::size_t vertex_count{};
    std::size_t triangle_count{};
    NifWriteDocument nif;
};

struct TerrainBtrWriteResult {
    bool valid{};
    bool renderable{};
    std::string error;
    std::uint64_t bytes_written{};
};

[[nodiscard]] TerrainBtrDocument build_fallout4_terrain_btr_document(
    const TerrainSimplifiedMesh& mesh,
    const TerrainBtrBuildOptions& options);

[[nodiscard]] TerrainBtrWriteResult write_fallout4_terrain_btr(
    const std::filesystem::path& path,
    const TerrainSimplifiedMesh& mesh,
    const TerrainBtrBuildOptions& options);

[[nodiscard]] const char* terrain_btr_vertex_precision_name(
    TerrainBtrVertexPrecision precision) noexcept;

} // namespace f4lod
