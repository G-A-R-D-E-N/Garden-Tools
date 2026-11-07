#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/nif/nif.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

struct UvTransform {
    float du{0.0f};
    float dv{0.0f};
    float sx{1.0f};
    float sy{1.0f};
    float rotation_degrees{0.0f};
};

namespace uv_detail {

constexpr float k_max_half = 65504.0f;

inline std::vector<std::uint32_t> island_roots(std::size_t vertex_count, const std::vector<std::uint16_t>& tris) {
    std::vector<std::uint32_t> parent(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) parent[i] = static_cast<std::uint32_t>(i);
    const auto root = [&parent](std::uint32_t start) {
        std::uint32_t i = start;
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t a = tris[t + k];
            const std::uint32_t b = tris[t + (k + 1) % 3];
            if (a >= vertex_count || b >= vertex_count || a == b) continue;
            const std::uint32_t ra = root(a);
            const std::uint32_t rb = root(b);
            if (ra != rb) parent[rb] = ra;
        }
    }
    std::vector<std::uint32_t> roots(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) roots[i] = root(static_cast<std::uint32_t>(i));
    return roots;
}

inline double uv_triangle_area(const std::vector<float>& uvs, std::uint16_t a, std::uint16_t b, std::uint16_t c) {
    const double ax = uvs[2 * a], ay = uvs[2 * a + 1];
    const double bx = uvs[2 * b], by = uvs[2 * b + 1];
    const double cx = uvs[2 * c], cy = uvs[2 * c + 1];
    return std::abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) / 2.0;
}

inline nlohmann::json uv_set_summary(const std::vector<float>& uvs, const std::vector<std::uint16_t>& tris) {
    nlohmann::json out;
    out["present"] = !uvs.empty();
    out["values"] = uvs.size();
    out["islands"] = nlohmann::json::array();
    if (uvs.empty()) {
        out["island_count"] = 0;
        return out;
    }
    const std::size_t vertex_count = uvs.size() / 2;
    const auto roots = island_roots(vertex_count, tris);
    std::map<std::uint32_t, std::size_t> vertex_counts;
    std::map<std::uint32_t, double> areas;
    for (std::size_t i = 0; i < vertex_count; ++i) vertex_counts[roots[i]] += 1;
    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
        const auto a = tris[t], b = tris[t + 1], c = tris[t + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;
        areas[roots[a]] += uv_triangle_area(uvs, a, b, c);
    }
    for (const auto& [island, verts] : vertex_counts) {
        out["islands"].push_back({{"island", island}, {"vertices", verts}, {"uv_area", areas[island]}});
    }
    out["island_count"] = vertex_counts.size();
    return out;
}

}  // namespace uv_detail

inline nlohmann::json uvSummaryJson(const std::vector<fo4::nif::Shape>& shapes) {
    nlohmann::json out;
    out["shapes"] = nlohmann::json::array();
    for (const auto& shape : shapes) {
        const auto vertex_count = shape.verts.size() / 3;
        nlohmann::json item;
        item["block"] = shape.block;
        item["name"] = shape.name;
        item["vertices"] = vertex_count;
        item["triangles"] = shape.tris.size() / 3;
        item["uv0"] = uv_detail::uv_set_summary(shape.uvs, shape.tris);
        item["uv2"] = uv_detail::uv_set_summary(shape.uvs2, shape.tris);
        out["shapes"].push_back(std::move(item));
    }
    out["shape_count"] = shapes.size();
    return out;
}

inline Result<std::vector<float>> transformedUvs(const std::vector<float>& uvs, const UvTransform& transform) {
    if (uvs.empty() || uvs.size() % 2 != 0) {
        return std::unexpected(Error{"UvTransformFailed", "the shape has no UV set on this channel"});
    }
    if (!std::isfinite(transform.du) || !std::isfinite(transform.dv) || !std::isfinite(transform.sx) ||
        !std::isfinite(transform.sy) || !std::isfinite(transform.rotation_degrees)) {
        return std::unexpected(Error{"UvTransformFailed", "the transform must be finite"});
    }
    const double radians = static_cast<double>(transform.rotation_degrees) * 3.14159265358979323846 / 180.0;
    const double cos_r = std::cos(radians);
    const double sin_r = std::sin(radians);
    std::vector<float> out(uvs.size());
    for (std::size_t i = 0; i + 1 < uvs.size(); i += 2) {
        const double u = static_cast<double>(uvs[i]) - 0.5;
        const double v = static_cast<double>(uvs[i + 1]) - 0.5;
        const double ru = u * cos_r - v * sin_r;
        const double rv = u * sin_r + v * cos_r;
        const double su = ru * static_cast<double>(transform.sx);
        const double sv = rv * static_cast<double>(transform.sy);
        const double fu = su + 0.5 + static_cast<double>(transform.du);
        const double fv = sv + 0.5 + static_cast<double>(transform.dv);
        if (!std::isfinite(fu) || !std::isfinite(fv) || std::fabs(fu) > uv_detail::k_max_half ||
            std::fabs(fv) > uv_detail::k_max_half) {
            return std::unexpected(Error{"UvTransformFailed", "the transformed UVs would leave the half-float range"});
        }
        out[i] = static_cast<float>(fu);
        out[i + 1] = static_cast<float>(fv);
    }
    return out;
}

}  // namespace fo4ide::assets
