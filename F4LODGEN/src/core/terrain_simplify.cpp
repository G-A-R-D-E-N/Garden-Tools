#include "f4lod/terrain_simplify.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

namespace f4lod {

TerrainSimplifiedMesh simplify_terrain_block(
    const TerrainBlockMesh& source,
    const TerrainSimplifyOptions& options) {
    TerrainSimplifiedMesh result;
    result.source_vertices = source.vertices.size();
    result.source_triangles = source.indices.size() / 3U;
    result.target_triangles = options.target_triangles;

    static_assert(std::is_standard_layout_v<TerrainVertex>);
    static_assert(sizeof(TerrainVertex) == sizeof(float) * 3U);
    static_assert(offsetof(TerrainVertex, x) == 0U);

    if (!source.valid) {
        result.error = "Terrain source block is invalid: " + source.error;
        return result;
    }
    if (source.vertices.empty() || source.indices.empty() || (source.indices.size() % 3U) != 0U) {
        result.error = "Terrain source block does not contain a valid triangle list.";
        return result;
    }
    if (source.outer_border.size() != source.vertices.size() ||
        source.cell_border.size() != source.vertices.size()) {
        result.error = "Terrain source block boundary metadata does not match its vertex count.";
        return result;
    }
    if (options.target_triangles == 0U) {
        result.error = "Terrain simplification target must contain at least one triangle.";
        return result;
    }
    if (!std::isfinite(options.target_error) || options.target_error < 0.0F || options.target_error > 1.0F) {
        result.error = "Terrain simplification target error must be finite and between 0 and 1.";
        return result;
    }
    if (options.max_vertices == 0U || options.max_vertices > 65535U) {
        result.error = "Terrain simplification max vertex count must be between 1 and 65535.";
        return result;
    }

    for (const auto index : source.indices) {
        if (index >= source.vertices.size()) {
            result.error = "Terrain source block contains an out-of-range index.";
            return result;
        }
    }

    const auto requested_indices = std::min(
        source.indices.size(),
        options.target_triangles > (std::numeric_limits<std::size_t>::max() / 3U)
            ? source.indices.size()
            : options.target_triangles * 3U);
    const auto target_index_count = requested_indices - (requested_indices % 3U);
    if (target_index_count < 3U) {
        result.error = "Terrain simplification target index count is too small.";
        return result;
    }

    std::vector<unsigned char> vertex_lock(source.vertices.size(), 0U);
    for (std::size_t i = 0; i < vertex_lock.size(); ++i) {
        if ((options.lock_outer_border && source.outer_border[i] != 0U) ||
            (options.lock_cell_borders && source.cell_border[i] != 0U)) {
            vertex_lock[i] = meshopt_SimplifyVertex_Lock;
        }
    }

    std::vector<unsigned int> simplified(source.indices.begin(), source.indices.end());
    unsigned int simplify_options = 0U;
    if (options.regularize) {
        simplify_options |= meshopt_SimplifyRegularize;
    }

    float result_error = 0.0F;
    const auto simplified_count = meshopt_simplifyWithAttributes(
        simplified.data(),
        source.indices.data(),
        source.indices.size(),
        reinterpret_cast<const float*>(source.vertices.data()),
        source.vertices.size(),
        sizeof(TerrainVertex),
        nullptr,
        0U,
        nullptr,
        0U,
        vertex_lock.data(),
        target_index_count,
        options.target_error,
        simplify_options,
        &result_error);

    if (simplified_count == 0U || (simplified_count % 3U) != 0U) {
        result.error = "Terrain simplifier returned an invalid triangle list.";
        return result;
    }
    simplified.resize(simplified_count);

    std::vector<TerrainVertex> compacted(source.vertices.size());
    const auto compacted_vertex_count = meshopt_optimizeVertexFetch(
        compacted.data(),
        simplified.data(),
        simplified.size(),
        source.vertices.data(),
        source.vertices.size(),
        sizeof(TerrainVertex));
    compacted.resize(compacted_vertex_count);

    if (compacted.empty()) {
        result.error = "Terrain simplification removed all vertices.";
        return result;
    }
    if (compacted.size() > options.max_vertices) {
        result.error = "Terrain simplification cannot satisfy the configured 16-bit vertex limit at the requested error budget.";
        return result;
    }
    for (const auto index : simplified) {
        if (index >= compacted.size()) {
            result.error = "Terrain simplifier produced an out-of-range compacted index.";
            return result;
        }
    }

    result.result_error = result_error;
    result.vertices = std::move(compacted);
    result.indices.assign(simplified.begin(), simplified.end());
    result.valid = true;
    return result;
}

} // namespace f4lod
