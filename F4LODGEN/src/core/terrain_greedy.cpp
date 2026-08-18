#include "f4lod/terrain_greedy.hpp"

#include "f4lod/landscape.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace f4lod {
namespace {

struct EdgeKey {
    std::uint32_t a{};
    std::uint32_t b{};

    bool operator==(const EdgeKey&) const noexcept = default;
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& edge) const noexcept {
        return (static_cast<std::size_t>(edge.a) << 32U) ^ edge.b;
    }
};

EdgeKey edge_key(std::uint32_t a, std::uint32_t b) noexcept {
    return a < b ? EdgeKey{a, b} : EdgeKey{b, a};
}

struct EdgeAdjacency {
    std::int32_t first{-1};
    std::int32_t second{-1};
};

struct GridPoint {
    std::int32_t x{};
    std::int32_t y{};
    float z_local{};
    std::uint32_t source_index{};
};

struct HeightGrid {
    bool valid{};
    std::string error;
    std::uint32_t side{};
    std::vector<GridPoint> points;
};

std::int64_t orient2d(
    const GridPoint& a,
    const GridPoint& b,
    const GridPoint& c) noexcept {
    return static_cast<std::int64_t>(b.x - a.x) * static_cast<std::int64_t>(c.y - a.y) -
           static_cast<std::int64_t>(b.y - a.y) * static_cast<std::int64_t>(c.x - a.x);
}

bool point_on_segment(
    const GridPoint& a,
    const GridPoint& b,
    const GridPoint& p) noexcept {
    if (orient2d(a, b, p) != 0) return false;
    return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
           p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

HeightGrid make_height_grid(const TerrainBlockMesh& source) {
    HeightGrid grid;
    if (!source.valid || source.lod_level == 0U) {
        grid.error = "Greedy terrain reduction requires a valid terrain block and LOD level.";
        return grid;
    }

    const std::uint64_t side64 = static_cast<std::uint64_t>(source.lod_level) * 32ULL + 1ULL;
    if (side64 > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        grid.error = "Greedy terrain heightfield side exceeds the 32-bit working range.";
        return grid;
    }
    grid.side = static_cast<std::uint32_t>(side64);
    const std::uint64_t expected64 = side64 * side64;
    if (expected64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        grid.error = "Greedy terrain heightfield size exceeds the platform size range.";
        return grid;
    }
    const auto expected = static_cast<std::size_t>(expected64);
    if (source.vertices.size() != expected) {
        grid.error = "Greedy terrain reduction requires a complete rectangular heightfield; "
                     "missing-cell fallback must be synthesized before reduction.";
        return grid;
    }

    grid.points.resize(expected);
    std::vector<unsigned char> seen(expected, 0U);
    const auto origin_gx = static_cast<std::int64_t>(source.block_x) * 32LL;
    const auto origin_gy = static_cast<std::int64_t>(source.block_y) * 32LL;
    const float inverse_level = 1.0F / static_cast<float>(source.lod_level);

    for (std::size_t source_index = 0; source_index < source.vertices.size(); ++source_index) {
        const auto& vertex = source.vertices[source_index];
        const auto global_x = static_cast<std::int64_t>(std::llround(
            static_cast<double>(vertex.x) / static_cast<double>(kLandscapeVertexSpacing)));
        const auto global_y = static_cast<std::int64_t>(std::llround(
            static_cast<double>(vertex.y) / static_cast<double>(kLandscapeVertexSpacing)));
        const auto local_x = global_x - origin_gx;
        const auto local_y = global_y - origin_gy;
        if (local_x < 0 || local_y < 0 ||
            local_x >= static_cast<std::int64_t>(grid.side) ||
            local_y >= static_cast<std::int64_t>(grid.side)) {
            grid.error = "Terrain source vertex lies outside its declared LOD block grid.";
            return grid;
        }

        const auto flat = static_cast<std::size_t>(local_y) * grid.side +
                          static_cast<std::size_t>(local_x);
        if (seen[flat] != 0U) {
            grid.error = "Terrain source contains duplicate samples at one heightfield coordinate.";
            return grid;
        }
        seen[flat] = 1U;
        grid.points[flat] = GridPoint{
            static_cast<std::int32_t>(local_x),
            static_cast<std::int32_t>(local_y),
            vertex.z * inverse_level,
            static_cast<std::uint32_t>(source_index),
        };
    }

    if (std::find(seen.begin(), seen.end(), static_cast<unsigned char>(0U)) != seen.end()) {
        grid.error = "Terrain source heightfield has an unpopulated grid coordinate.";
        return grid;
    }

    grid.valid = true;
    return grid;
}

struct RawGreedyResult {
    bool valid{};
    std::string error;
    float max_error_local{};
    bool point_limit_reached{};
    std::size_t selected_points{};
    std::size_t insertions{};
    std::vector<std::array<std::uint32_t, 3>> triangles_source_indices;
};

class RegionReducer {
public:
    RegionReducer(
        const HeightGrid& grid,
        std::uint32_t x0,
        std::uint32_t y0,
        std::uint32_t side,
        float threshold,
        std::size_t point_limit,
        std::uint32_t boundary_step,
        const std::vector<unsigned char>& forced_source,
        const std::vector<unsigned char>& ignored_source)
        : grid_(grid), x0_(x0), y0_(y0), side_(side), threshold_(threshold),
          point_limit_(point_limit), boundary_step_(boundary_step),
          selected_(static_cast<std::size_t>(side) * side, 0U),
          forced_(static_cast<std::size_t>(side) * side, 0U),
          ignored_(static_cast<std::size_t>(side) * side, 0U) {
        for (std::uint32_t y = 0; y < side_; ++y) {
            for (std::uint32_t x = 0; x < side_; ++x) {
                const auto local = local_index(x, y);
                const auto source_index = point(local).source_index;
                if (!forced_source.empty() && forced_source[source_index] != 0U) forced_[local] = 1U;
                if (!ignored_source.empty() && ignored_source[source_index] != 0U) ignored_[local] = 1U;
                if (forced_[local] != 0U) ignored_[local] = 0U;
            }
        }
    }

    RawGreedyResult run() {
        RawGreedyResult result;
        if (side_ < 2U || x0_ + side_ > grid_.side || y0_ + side_ > grid_.side) {
            result.error = "Greedy terrain region lies outside the source heightfield.";
            return result;
        }
        if (!std::isfinite(threshold_) || threshold_ < 0.0F || point_limit_ < 4U) {
            result.error = "Greedy terrain region has invalid Quality or point-limit settings.";
            return result;
        }

        if (!initialize_boundary(result.error)) return result;
        if (!apply_forced_points(result.error)) return result;
        heap_ = {};
        legalize_all();
        for (std::size_t id = 0; id < triangles_.size(); ++id) {
            if (triangles_[id].active) push_candidate(static_cast<std::uint32_t>(id));
        }

        float stopping_error = 0.0F;
        while (true) {
            HeapEntry entry;
            if (!pop_candidate(entry)) {
                stopping_error = 0.0F;
                break;
            }

            stopping_error = entry.error;
            if (entry.error < threshold_) break;
            // Reference Terrain.GoalMet stops only after PointCount exceeds the
            // configured limit, not when it merely reaches it.
            if (selected_count_ > point_limit_) {
                result.point_limit_reached = true;
                break;
            }

            if (!insert_candidate(entry.triangle, entry.point, result.error)) return result;
            ++result.insertions;
        }

        result.max_error_local = stopping_error;
        result.selected_points = selected_count_;
        if (!emit(result.triangles_source_indices, result.error)) return result;
        result.valid = true;
        return result;
    }

private:
    struct Triangle {
        std::uint32_t a{};
        std::uint32_t b{};
        std::uint32_t c{};
        bool active{};
    };

    struct HeapEntry {
        float error{};
        std::uint32_t triangle{};
        std::uint32_t point{};
    };

    struct HeapLess {
        bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const noexcept {
            if (lhs.error != rhs.error) return lhs.error < rhs.error;
            // The reference heap swaps only on strict importance comparisons.
            // Triangle creation order is the closest stable analogue to its
            // token position; point index must not break equal-error ties.
            return lhs.triangle > rhs.triangle;
        }
    };

    const GridPoint& point(std::uint32_t local) const noexcept {
        const std::uint32_t x = local % side_;
        const std::uint32_t y = local / side_;
        return grid_.points[static_cast<std::size_t>(y0_ + y) * grid_.side + (x0_ + x)];
    }

    std::uint32_t local_index(std::uint32_t x, std::uint32_t y) const noexcept {
        return y * side_ + x;
    }

    std::int64_t orient(std::uint32_t a, std::uint32_t b, std::uint32_t c) const noexcept {
        const auto& pa = point(a);
        const auto& pb = point(b);
        const auto& pc = point(c);
        const std::int64_t ax = static_cast<std::int64_t>(pb.x - pa.x);
        const std::int64_t ay = static_cast<std::int64_t>(pb.y - pa.y);
        const std::int64_t bx = static_cast<std::int64_t>(pc.x - pa.x);
        const std::int64_t by = static_cast<std::int64_t>(pc.y - pa.y);
        return ax * by - ay * bx;
    }

    std::uint32_t add_triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        if (a == b || b == c || c == a) return kInvalid;
        const auto area = orient(a, b, c);
        if (area == 0) return kInvalid;
        if (area < 0) std::swap(b, c);

        const auto id = static_cast<std::uint32_t>(triangles_.size());
        triangles_.push_back(Triangle{a, b, c, true});
        add_edge(edge_key(a, b), id);
        add_edge(edge_key(b, c), id);
        add_edge(edge_key(c, a), id);
        return id;
    }

    void remove_triangle(std::uint32_t id) {
        if (id >= triangles_.size() || !triangles_[id].active) return;
        const auto triangle = triangles_[id];
        remove_edge(edge_key(triangle.a, triangle.b), id);
        remove_edge(edge_key(triangle.b, triangle.c), id);
        remove_edge(edge_key(triangle.c, triangle.a), id);
        triangles_[id].active = false;
    }

    void add_edge(const EdgeKey& edge, std::uint32_t triangle) {
        auto& adj = edges_[edge];
        if (adj.first < 0) adj.first = static_cast<std::int32_t>(triangle);
        else if (adj.second < 0) adj.second = static_cast<std::int32_t>(triangle);
    }

    void remove_edge(const EdgeKey& edge, std::uint32_t triangle) {
        const auto found = edges_.find(edge);
        if (found == edges_.end()) return;
        auto& adj = found->second;
        if (adj.first == static_cast<std::int32_t>(triangle)) adj.first = -1;
        if (adj.second == static_cast<std::int32_t>(triangle)) adj.second = -1;
        if (adj.first < 0 && adj.second >= 0) {
            adj.first = adj.second;
            adj.second = -1;
        }
        if (adj.first < 0 && adj.second < 0) edges_.erase(found);
    }

    bool initialize_boundary(std::string& error) {
        const auto bl = local_index(0U, 0U);
        const auto br = local_index(side_ - 1U, 0U);
        const auto tr = local_index(side_ - 1U, side_ - 1U);
        const auto tl = local_index(0U, side_ - 1U);
        select(bl); select(br); select(tr); select(tl);

        if (add_triangle(bl, br, tr) == kInvalid || add_triangle(bl, tr, tl) == kInvalid) {
            error = "Could not initialize greedy terrain corner triangles.";
            return false;
        }
        constraints_.insert(edge_key(bl, br));
        constraints_.insert(edge_key(br, tr));
        constraints_.insert(edge_key(tr, tl));
        constraints_.insert(edge_key(tl, bl));

        if (boundary_step_ == 0U || boundary_step_ >= side_ - 1U) return true;

        std::uint32_t current = bl;
        for (std::uint32_t x = boundary_step_; x < side_ - 1U; x += boundary_step_) {
            const auto p = local_index(x, 0U);
            if (!insert_boundary_point(current, br, p, error)) return false;
            current = p;
        }
        current = br;
        for (std::uint32_t y = boundary_step_; y < side_ - 1U; y += boundary_step_) {
            const auto p = local_index(side_ - 1U, y);
            if (!insert_boundary_point(current, tr, p, error)) return false;
            current = p;
        }
        current = tr;
        for (std::int64_t x = static_cast<std::int64_t>(side_ - 1U) - boundary_step_;
             x > 0; x -= boundary_step_) {
            const auto p = local_index(static_cast<std::uint32_t>(x), side_ - 1U);
            if (!insert_boundary_point(current, tl, p, error)) return false;
            current = p;
        }
        current = tl;
        for (std::int64_t y = static_cast<std::int64_t>(side_ - 1U) - boundary_step_;
             y > 0; y -= boundary_step_) {
            const auto p = local_index(0U, static_cast<std::uint32_t>(y));
            if (!insert_boundary_point(current, bl, p, error)) return false;
            current = p;
        }
        return true;
    }

    bool apply_forced_points(std::string& error) {
        for (std::uint32_t y = 0; y < side_; ++y) {
            for (std::uint32_t x = 0; x < side_; ++x) {
                const auto p = local_index(x, y);
                if (forced_[p] == 0U) continue;
                ignored_[p] = 0U;
                if (selected_[p] != 0U) continue;

                std::optional<EdgeKey> boundary_edge;
                for (const auto& edge : constraints_) {
                    if (p != edge.a && p != edge.b &&
                        point_on_segment(point(edge.a), point(edge.b), point(p))) {
                        boundary_edge = edge;
                        break;
                    }
                }
                if (boundary_edge.has_value()) {
                    if (!insert_boundary_point(boundary_edge->a, boundary_edge->b, p, error)) return false;
                    continue;
                }

                std::uint32_t containing = kInvalid;
                for (std::uint32_t id = 0; id < triangles_.size(); ++id) {
                    if (triangle_contains(id, p)) {
                        containing = id;
                        break;
                    }
                }
                if (containing == kInvalid) {
                    error = "Scripted greedy point could not be located in the current triangulation.";
                    return false;
                }
                if (!insert_candidate(containing, p, error)) return false;
            }
        }
        return true;
    }

    void select(std::uint32_t p) {
        if (selected_[p] == 0U) {
            selected_[p] = 1U;
            ++selected_count_;
        }
    }

    bool insert_boundary_point(
        std::uint32_t a,
        std::uint32_t b,
        std::uint32_t p,
        std::string& error) {
        ignored_[p] = 0U;
        select(p);
        const EdgeKey old_edge = edge_key(a, b);
        if (!constraints_.contains(old_edge)) {
            error = "Greedy terrain boundary constraint was not present while splitting an edge.";
            return false;
        }
        std::vector<std::uint32_t> changed;
        if (!split_edge(old_edge, p, changed, error)) return false;
        constraints_.erase(old_edge);
        constraints_.insert(edge_key(a, p));
        constraints_.insert(edge_key(p, b));
        legalize_inserted(p, changed);
        return true;
    }

    std::uint32_t third_vertex(const Triangle& triangle, const EdgeKey& edge) const noexcept {
        if (triangle.a != edge.a && triangle.a != edge.b) return triangle.a;
        if (triangle.b != edge.a && triangle.b != edge.b) return triangle.b;
        return triangle.c;
    }

    bool split_edge(
        const EdgeKey& edge,
        std::uint32_t p,
        std::vector<std::uint32_t>& changed,
        std::string& error) {
        const auto found = edges_.find(edge);
        if (found == edges_.end() || found->second.first < 0) {
            error = "Greedy terrain could not find an edge selected for splitting.";
            return false;
        }
        const auto first = static_cast<std::uint32_t>(found->second.first);
        const auto second = found->second.second >= 0
            ? static_cast<std::uint32_t>(found->second.second)
            : kInvalid;
        if (first >= triangles_.size() || !triangles_[first].active) {
            error = "Greedy terrain edge references an inactive triangle.";
            return false;
        }

        const auto c = third_vertex(triangles_[first], edge);
        std::uint32_t d = kInvalid;
        if (second != kInvalid) {
            if (second >= triangles_.size() || !triangles_[second].active) {
                error = "Greedy terrain edge references an invalid neighbor triangle.";
                return false;
            }
            d = third_vertex(triangles_[second], edge);
        }

        remove_triangle(first);
        if (second != kInvalid) remove_triangle(second);

        const std::array<std::array<std::uint32_t, 3>, 4> candidates{{
            {edge.a, p, c},
            {p, edge.b, c},
            {edge.b, p, d},
            {p, edge.a, d},
        }};
        const std::size_t count = second == kInvalid ? 2U : 4U;
        for (std::size_t i = 0; i < count; ++i) {
            const auto id = add_triangle(candidates[i][0], candidates[i][1], candidates[i][2]);
            if (id == kInvalid) {
                error = "Greedy terrain edge split produced a degenerate triangle.";
                return false;
            }
            changed.push_back(id);
        }
        return true;
    }

    bool triangle_contains(std::uint32_t triangle_id, std::uint32_t p) const noexcept {
        if (triangle_id >= triangles_.size() || !triangles_[triangle_id].active) return false;
        const auto& triangle = triangles_[triangle_id];
        return orient(triangle.a, triangle.b, p) >= 0 &&
               orient(triangle.b, triangle.c, p) >= 0 &&
               orient(triangle.c, triangle.a, p) >= 0;
    }

    bool insert_candidate(std::uint32_t triangle_id, std::uint32_t p, std::string& error) {
        if (p >= selected_.size() || selected_[p] != 0U || ignored_[p] != 0U) return true;
        if (!triangle_contains(triangle_id, p)) {
            push_candidate(triangle_id);
            return true;
        }

        select(p);
        const auto triangle = triangles_[triangle_id];
        std::vector<std::uint32_t> changed;
        const std::array<EdgeKey, 3> tri_edges{
            edge_key(triangle.a, triangle.b),
            edge_key(triangle.b, triangle.c),
            edge_key(triangle.c, triangle.a),
        };

        bool on_edge = false;
        for (const auto& edge : tri_edges) {
            if (!point_on_segment(point(edge.a), point(edge.b), point(p))) continue;
            const bool constrained = constraints_.contains(edge);
            if (!split_edge(edge, p, changed, error)) return false;
            if (constrained) {
                constraints_.erase(edge);
                constraints_.insert(edge_key(edge.a, p));
                constraints_.insert(edge_key(p, edge.b));
            }
            on_edge = true;
            break;
        }

        if (!on_edge) {
            remove_triangle(triangle_id);
            const std::array<std::array<std::uint32_t, 3>, 3> pieces{{
                {triangle.a, triangle.b, p},
                {triangle.b, triangle.c, p},
                {triangle.c, triangle.a, p},
            }};
            for (const auto& piece : pieces) {
                const auto id = add_triangle(piece[0], piece[1], piece[2]);
                if (id == kInvalid) {
                    error = "Greedy terrain point insertion produced a degenerate triangle.";
                    return false;
                }
                changed.push_back(id);
            }
        }

        legalize_inserted(p, changed);
        std::unordered_set<std::uint32_t> unique;
        for (const auto id : changed) {
            if (id < triangles_.size() && triangles_[id].active && unique.insert(id).second) {
                push_candidate(id);
            }
        }
        return true;
    }

    bool incircle(const Triangle& triangle, std::uint32_t d) const noexcept {
        const auto& a = point(triangle.a);
        const auto& b = point(triangle.b);
        const auto& c = point(triangle.c);
        const auto& p = point(d);

        const float ax = static_cast<float>(a.x - p.x);
        const float ay = static_cast<float>(a.y - p.y);
        const float bx = static_cast<float>(b.x - p.x);
        const float by = static_cast<float>(b.y - p.y);
        const float cx = static_cast<float>(c.x - p.x);
        const float cy = static_cast<float>(c.y - p.y);
        const float det =
            (ax * ax + ay * ay) * (bx * cy - by * cx) -
            (bx * bx + by * by) * (ax * cy - ay * cx) +
            (cx * cx + cy * cy) * (ax * by - ay * bx);
        return det > 1.0e-6F;
    }

    std::vector<std::uint32_t> flip_edge(const EdgeKey& edge) {
        std::vector<std::uint32_t> changed;
        const auto found = edges_.find(edge);
        if (found == edges_.end() || found->second.first < 0 || found->second.second < 0) return changed;
        const auto t1 = static_cast<std::uint32_t>(found->second.first);
        const auto t2 = static_cast<std::uint32_t>(found->second.second);
        if (t1 >= triangles_.size() || t2 >= triangles_.size() ||
            !triangles_[t1].active || !triangles_[t2].active) return changed;

        const auto c = third_vertex(triangles_[t1], edge);
        const auto d = third_vertex(triangles_[t2], edge);
        if (c == d || constraints_.contains(edge)) return changed;
        if (orient(c, d, edge.a) == 0 || orient(c, d, edge.b) == 0 ||
            (orient(c, d, edge.a) > 0) == (orient(c, d, edge.b) > 0)) return changed;

        remove_triangle(t1);
        remove_triangle(t2);
        const auto n1 = add_triangle(c, d, edge.a);
        const auto n2 = add_triangle(d, c, edge.b);
        if (n1 != kInvalid) changed.push_back(n1);
        if (n2 != kInvalid) changed.push_back(n2);
        return changed;
    }

    void legalize_all() {
        std::queue<EdgeKey> queue;
        for (const auto& [edge, adj] : edges_) {
            if (adj.first >= 0 && adj.second >= 0 && !constraints_.contains(edge)) queue.push(edge);
        }

        std::size_t flips = 0U;
        const std::size_t flip_limit = std::max<std::size_t>(1000U, triangles_.size() * 32U);
        while (!queue.empty() && flips < flip_limit) {
            const auto edge = queue.front();
            queue.pop();
            const auto found = edges_.find(edge);
            if (found == edges_.end() || found->second.first < 0 || found->second.second < 0 ||
                constraints_.contains(edge)) continue;
            const auto t1 = static_cast<std::uint32_t>(found->second.first);
            const auto t2 = static_cast<std::uint32_t>(found->second.second);
            if (!triangles_[t1].active || !triangles_[t2].active) continue;
            const auto d = third_vertex(triangles_[t2], edge);
            if (!incircle(triangles_[t1], d)) continue;

            const auto changed = flip_edge(edge);
            if (changed.empty()) continue;
            ++flips;
            for (const auto id : changed) {
                const auto& triangle = triangles_[id];
                queue.push(edge_key(triangle.a, triangle.b));
                queue.push(edge_key(triangle.b, triangle.c));
                queue.push(edge_key(triangle.c, triangle.a));
            }
        }
    }

    void legalize_inserted(
        std::uint32_t p,
        std::vector<std::uint32_t>& changed) {
        std::queue<EdgeKey> queue;
        for (const auto id : changed) {
            if (id >= triangles_.size() || !triangles_[id].active) continue;
            const auto& t = triangles_[id];
            if (t.a == p) queue.push(edge_key(t.b, t.c));
            else if (t.b == p) queue.push(edge_key(t.c, t.a));
            else if (t.c == p) queue.push(edge_key(t.a, t.b));
        }

        std::size_t guard = 0U;
        while (!queue.empty() && guard++ < 100000U) {
            const auto edge = queue.front();
            queue.pop();
            if (constraints_.contains(edge)) continue;
            const auto found = edges_.find(edge);
            if (found == edges_.end() || found->second.first < 0 || found->second.second < 0) continue;

            const auto t1 = static_cast<std::uint32_t>(found->second.first);
            const auto t2 = static_cast<std::uint32_t>(found->second.second);
            if (!triangles_[t1].active || !triangles_[t2].active) continue;

            std::uint32_t with_p = kInvalid;
            std::uint32_t other = kInvalid;
            const auto contains_p = [&](const Triangle& t) {
                return t.a == p || t.b == p || t.c == p;
            };
            if (contains_p(triangles_[t1]) && !contains_p(triangles_[t2])) {
                with_p = t1; other = t2;
            } else if (contains_p(triangles_[t2]) && !contains_p(triangles_[t1])) {
                with_p = t2; other = t1;
            } else {
                continue;
            }

            const auto d = third_vertex(triangles_[other], edge);
            if (!incircle(triangles_[with_p], d)) continue;
            const auto new_triangles = flip_edge(edge);
            if (new_triangles.empty()) continue;
            changed.insert(changed.end(), new_triangles.begin(), new_triangles.end());
            for (const auto id : new_triangles) {
                if (id >= triangles_.size() || !triangles_[id].active) continue;
                const auto& t = triangles_[id];
                if (t.a == p) queue.push(edge_key(t.b, t.c));
                else if (t.b == p) queue.push(edge_key(t.c, t.a));
                else if (t.c == p) queue.push(edge_key(t.a, t.b));
            }
        }
    }

    bool compute_candidate(std::uint32_t triangle_id, HeapEntry& entry) const {
        if (triangle_id >= triangles_.size() || !triangles_[triangle_id].active) return false;
        const auto& t = triangles_[triangle_id];
        const auto& a = point(t.a);
        const auto& b = point(t.b);
        const auto& c = point(t.c);
        const auto area = orient(t.a, t.b, t.c);
        if (area <= 0) return false;

        const auto min_x = std::max<std::int32_t>(0, std::min({a.x, b.x, c.x}) - static_cast<std::int32_t>(x0_));
        const auto max_x = std::min<std::int32_t>(static_cast<std::int32_t>(side_ - 1U),
            std::max({a.x, b.x, c.x}) - static_cast<std::int32_t>(x0_));
        const auto min_y = std::max<std::int32_t>(0, std::min({a.y, b.y, c.y}) - static_cast<std::int32_t>(y0_));
        const auto max_y = std::min<std::int32_t>(static_cast<std::int32_t>(side_ - 1U),
            std::max({a.y, b.y, c.y}) - static_cast<std::int32_t>(y0_));

        bool found = false;
        float best_error = -1.0F;
        std::uint32_t best_point = 0U;
        const long double area_ld = static_cast<long double>(area);

        // Scanline order is y-major then x-major. Candidate.Consider in the
        // reference keeps only the first strictly-greater error, so equal-error
        // points never replace an earlier point.
        for (std::int32_t y = min_y; y <= max_y; ++y) {
            for (std::int32_t x = min_x; x <= max_x; ++x) {
                const auto p = local_index(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                if (selected_[p] != 0U || ignored_[p] != 0U) continue;
                if (!triangle_contains(triangle_id, p)) continue;

                const long double wa = static_cast<long double>(orient(t.b, t.c, p)) / area_ld;
                const long double wb = static_cast<long double>(orient(t.c, t.a, p)) / area_ld;
                const long double wc = 1.0L - wa - wb;
                const long double plane =
                    wa * static_cast<long double>(a.z_local) +
                    wb * static_cast<long double>(b.z_local) +
                    wc * static_cast<long double>(c.z_local);
                const float error = static_cast<float>(std::fabs(
                    static_cast<long double>(point(p).z_local) - plane));
                if (!found || error > best_error) {
                    found = true;
                    best_error = error;
                    best_point = p;
                }
            }
        }

        // TrackedTriangle.Scan kills candidates below 0.0001 instead of
        // inserting zero-error samples forever when Quality is zero.
        if (!found || best_error < 0.0001F) return false;
        entry = HeapEntry{best_error, triangle_id, best_point};
        return true;
    }

    void push_candidate(std::uint32_t triangle_id) {
        HeapEntry entry;
        if (compute_candidate(triangle_id, entry)) heap_.push(entry);
    }

    bool pop_candidate(HeapEntry& entry) {
        while (!heap_.empty()) {
            entry = heap_.top();
            heap_.pop();
            if (entry.triangle >= triangles_.size() || !triangles_[entry.triangle].active) continue;
            if (entry.point >= selected_.size()) continue;
            if (selected_[entry.point] != 0U || ignored_[entry.point] != 0U ||
                !triangle_contains(entry.triangle, entry.point)) {
                push_candidate(entry.triangle);
                continue;
            }
            HeapEntry current;
            if (!compute_candidate(entry.triangle, current)) continue;
            if (current.point != entry.point || current.error != entry.error) {
                heap_.push(current);
                continue;
            }
            entry = current;
            return true;
        }
        return false;
    }

    bool emit(
        std::vector<std::array<std::uint32_t, 3>>& output,
        std::string& error) const {
        for (const auto& t : triangles_) {
            if (!t.active) continue;
            const auto a = point(t.a).source_index;
            const auto b = point(t.b).source_index;
            const auto c = point(t.c).source_index;
            if (a == b || b == c || c == a) {
                error = "Greedy terrain output contains a degenerate source-index triangle.";
                return false;
            }
            output.push_back({a, b, c});
        }
        if (output.empty()) {
            error = "Greedy terrain output contains no triangles.";
            return false;
        }
        return true;
    }

    static constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();

    const HeightGrid& grid_;
    std::uint32_t x0_{};
    std::uint32_t y0_{};
    std::uint32_t side_{};
    float threshold_{};
    std::size_t point_limit_{};
    std::uint32_t boundary_step_{};
    std::vector<unsigned char> selected_;
    std::vector<unsigned char> forced_;
    std::vector<unsigned char> ignored_;
    std::size_t selected_count_{};
    std::vector<Triangle> triangles_;
    std::unordered_map<EdgeKey, EdgeAdjacency, EdgeKeyHash> edges_;
    std::unordered_set<EdgeKey, EdgeKeyHash> constraints_;
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapLess> heap_;
};

TerrainSimplifiedMesh build_output_mesh(
    const TerrainBlockMesh& source,
    const std::vector<std::array<std::uint32_t, 3>>& raw_triangles) {
    TerrainSimplifiedMesh mesh;
    mesh.source_vertices = source.vertices.size();
    mesh.source_triangles = source.indices.size() / 3U;
    mesh.target_triangles = raw_triangles.size();

    std::vector<unsigned char> used(source.vertices.size(), 0U);
    for (const auto& triangle : raw_triangles) {
        for (const auto source_index : triangle) {
            if (source_index >= source.vertices.size()) {
                mesh.error = "Greedy terrain output references a source vertex outside the terrain block.";
                return mesh;
            }
            used[source_index] = 1U;
        }
    }

    // CreateGeometry in the reference re-sorts terrain vertices by y*4096+x.
    // TerrainBlockMesh source samples are already stored in that y-major order,
    // so iterating source indices gives the same deterministic normalization.
    std::vector<std::uint32_t> remap(source.vertices.size(), std::numeric_limits<std::uint32_t>::max());
    for (std::size_t source_index = 0; source_index < source.vertices.size(); ++source_index) {
        if (used[source_index] == 0U) continue;
        remap[source_index] = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(source.vertices[source_index]);
    }

    mesh.indices.reserve(raw_triangles.size() * 3U);
    for (const auto& triangle : raw_triangles) {
        for (const auto source_index : triangle) {
            const auto mapped = remap[source_index];
            if (mapped == std::numeric_limits<std::uint32_t>::max()) {
                mesh.error = "Greedy terrain output remap is incomplete.";
                return mesh;
            }
            mesh.indices.push_back(mapped);
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        mesh.error = "Greedy terrain output is empty.";
        return mesh;
    }
    mesh.valid = true;
    return mesh;
}

} // namespace

TerrainGreedyResult reduce_terrain_greedy(
    const TerrainBlockMesh& source,
    const TerrainGreedyOptions& options) {
    TerrainGreedyResult result;
    if (!source.valid) {
        result.error = "Greedy terrain source is invalid: " + source.error;
        return result;
    }
    if (options.quality < 0) {
        result.error = "Greedy terrain reducer accepts Quality 0 or greater after caller normalization.";
        return result;
    }
    if (options.point_limit < 4U || options.point_limit > 32767U) {
        result.error = "Greedy terrain MaxVerts/point limit must be between 4 and 32767 until BTR chunk splitting is enabled.";
        return result;
    }
    if (source.lod_level != 4U && source.lod_level != 8U &&
        source.lod_level != 16U && source.lod_level != 32U) {
        result.error = "Greedy terrain reducer supports LOD4, LOD8, LOD16, and LOD32.";
        return result;
    }
    if ((!options.forced_points.empty() && options.forced_points.size() != source.vertices.size()) ||
        (!options.ignored_points.empty() && options.ignored_points.size() != source.vertices.size())) {
        result.error = "Greedy terrain point-state masks must match the source vertex count.";
        return result;
    }

    const auto grid = make_height_grid(source);
    if (!grid.valid) {
        result.error = grid.error;
        return result;
    }

    std::vector<std::array<std::uint32_t, 3>> raw_triangles;
    float max_error_local = 0.0F;
    bool limit_reached = false;
    std::size_t insertions = 0U;
    std::size_t selected_points = 0U;

    if (source.lod_level == 4U && options.protect_cell_borders) {
        const float threshold = static_cast<float>(options.quality) / 4.0F;
        result.threshold_quad_units = threshold;
        for (std::uint32_t cell_y = 0U; cell_y < 4U; ++cell_y) {
            for (std::uint32_t cell_x = 0U; cell_x < 4U; ++cell_x) {
                // Previous-level pinning is not yet represented in the native
                // job graph, so retain dense per-cell boundaries as the safe
                // conservative path rather than inventing those old-geometry
                // states. Interior WaterDelta states are still exact.
                RegionReducer reducer(
                    grid,
                    cell_x * 32U,
                    cell_y * 32U,
                    33U,
                    threshold,
                    options.point_limit,
                    1U,
                    options.forced_points,
                    options.ignored_points);
                auto region = reducer.run();
                if (!region.valid) {
                    result.error = "LOD4 protected-cell greedy reduction failed: " + region.error;
                    return result;
                }
                max_error_local = std::max(max_error_local, region.max_error_local);
                limit_reached = limit_reached || region.point_limit_reached;
                insertions += region.insertions;
                raw_triangles.insert(
                    raw_triangles.end(),
                    region.triangles_source_indices.begin(),
                    region.triangles_source_indices.end());
            }
        }
    } else {
        const float threshold = static_cast<float>(options.quality);
        result.threshold_quad_units = threshold;
        // TerrainLOD scripts outline points every lodLevel*2 samples; the old
        // native reducer incorrectly forced every point on all four sides.
        const std::uint32_t boundary_step = source.lod_level * 2U;
        RegionReducer reducer(
            grid, 0U, 0U, grid.side, threshold, options.point_limit,
            boundary_step, options.forced_points, options.ignored_points);
        auto region = reducer.run();
        if (!region.valid) {
            result.error = region.error;
            return result;
        }
        max_error_local = region.max_error_local;
        limit_reached = region.point_limit_reached;
        insertions = region.insertions;
        selected_points = region.selected_points;
        raw_triangles = std::move(region.triangles_source_indices);
    }

    result.mesh = build_output_mesh(source, raw_triangles);
    if (!result.mesh.valid) {
        result.error = result.mesh.error;
        return result;
    }
    if (source.lod_level == 4U && options.protect_cell_borders) {
        selected_points = result.mesh.vertices.size();
    }

    result.max_error_quad_units = max_error_local;
    result.max_error_world_units = max_error_local * static_cast<float>(source.lod_level);
    result.point_limit_reached = limit_reached;
    result.selected_points = selected_points;
    result.greedy_insertions = insertions;
    result.mesh.result_error = result.max_error_world_units;
    result.valid = true;
    return result;
}

} // namespace f4lod
