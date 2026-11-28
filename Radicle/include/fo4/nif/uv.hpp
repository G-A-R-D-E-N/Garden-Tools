#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace fo4::nif::uv {

using Floats = std::span<const float>;
using Indices = std::span<const std::uint32_t>;

struct Point {
  double u{0.0};
  double v{0.0};
};

struct Box {
  double min_u{0.0};
  double min_v{0.0};
  double max_u{0.0};
  double max_v{0.0};
};

// Invariant: scale is applied before rotation, both about pivot, then the translation is added.
struct Xform {
  double tx{0.0};
  double ty{0.0};
  double sx{1.0};
  double sy{1.0};
  double rot_rad{0.0};
  Point pivot{};
};

struct View {
  double scale{1.0};
  double px{0.0};
  double py{0.0};
};

enum class PivotMode { centre, median, tile };

struct Pin {
  std::uint32_t vertex{0};
  double u{0.0};
  double v{0.0};
};

namespace detail {

inline std::size_t tri_count(Indices tris) { return tris.size() / 3; }

inline bool tri_ok(Indices tris, std::size_t t, std::size_t n) {
  return tris[3 * t] < n && tris[3 * t + 1] < n && tris[3 * t + 2] < n;
}

inline void set_comp(std::vector<float>& out, std::size_t k, double x) {
  if (static_cast<double>(out[k]) != x) out[k] = static_cast<float>(x);
}

}  // namespace detail

// Constraint: a vertex that no whole triangle references is its own island; ids are numbered by first vertex appearance.
std::vector<std::uint32_t> islands(std::size_t vertex_count, Indices tris);

// Constraint: selection entries at or past the vertex count are ignored; an empty result is nullopt.
std::optional<Box> bounds(Floats uvs, Indices selection);
std::optional<Point> median(Floats uvs, Indices selection);
Point resolve_pivot(Floats uvs, Indices selection, PivotMode mode);
std::vector<float> transform_sel(Floats uvs, Indices selection, const Xform& xform);
bool point_in_box(double u, double v, const Box& box);
// Invariant: -1 when no vertex lies within max_dist; ties go to the later vertex.
std::int64_t pick_nearest(Floats uvs, double u, double v, double max_dist);
View frame_view(Floats uvs, double width, double height);

// Constraint: islands with a selected vertex move whole; nullopt when they cannot fit the 0-1 tile with the margin (clamped to 0..0.25, NaN is 0).
std::optional<std::vector<float>> pack_islands(Floats uvs, Indices ids, Indices selection, double margin);
std::vector<float> average_island_scale(Floats uvs, Floats verts, Indices tris, Indices ids, Indices selection);
// Invariant: one value per whole triangle in 0..1, 0 for a degenerate or out-of-range triangle.
std::vector<float> tri_stretch(Floats uvs, Floats verts, Indices tris);

struct Plane {
  std::array<double, 3> u{};
  std::array<double, 3> v{};
  double cu{0.0};
  double cv{0.0};
};

std::map<std::uint32_t, Plane> island_planes(Floats verts, Indices tris, Indices ids, Indices selection);

struct ProjectResult {
  std::vector<float> uvs;
  std::uint32_t projected{0};
  std::uint32_t skipped{0};
  bool fits{true};
};

// Invariant: when the projected islands cannot be packed the input UVs come back unchanged with fits false and projected 0.
ProjectResult smart_project(Floats uvs, Floats verts, Indices tris, Indices ids, Indices selection, double margin = 0.01);

struct UnwrapResult {
  std::vector<float> uvs;
  std::uint32_t unwrapped{0};
  std::uint32_t skipped{0};
};

// Constraint: an island needs two pins with distinct UVs and a connected patch with two pins; anything else is skipped and left as it was.
UnwrapResult lscm_unwrap(Floats uvs, Floats verts, Indices tris, Indices ids, Indices selection, std::span<const Pin> pins);

struct IndexPair {
  std::uint32_t first{0};
  std::uint32_t second{0};
  bool operator==(const IndexPair&) const = default;
};

enum class BoxMode { vertex, edge, face };
enum class Axis { u, v };
enum class Align { min, centre, max };

struct SplitPlan {
  std::vector<std::uint32_t> sources;
  std::vector<std::uint32_t> tris;
};

namespace detail {

// Invariant: first appearance order is kept, duplicates and entries at or past vertex_count are dropped.
inline std::vector<std::size_t> unique_selection(std::size_t vertex_count, Indices selection) {
  std::vector<std::size_t> out;
  std::vector<bool> seen(vertex_count, false);
  for (const std::uint32_t i : selection) {
    if (i >= vertex_count || seen[i]) continue;
    seen[i] = true;
    out.push_back(i);
  }
  return out;
}

}  // namespace detail

// Invariant: answers ascend; a triangle with a corner at or past vertex_count adds no boundary edge, and an edge that repeats a vertex is skipped.
std::vector<std::uint32_t> boundary_verts(std::size_t vertex_count, Indices tris);
// Constraint: boundary and unreachable vertices stay put; iterations is floored and clamped to 0..200, NaN is 0.
std::vector<float> relax_uvs(Floats uvs, Indices tris, Indices selection, double iterations);
// Invariant: pairs are (a, b) with a < b, ordered by a then b, and join vertices of different islands within eps; an eps below 1e-9 or NaN answers none.
std::vector<IndexPair> find_stitch_pairs(Floats verts, Indices ids, Indices selection, double eps);
std::vector<float> stitch_selection(Floats uvs, Floats verts, Indices ids, Indices selection, double eps);
// Invariant: answers ascend triangle ids; hidden, degenerate, out-of-range and non-finite triangles are ignored, edge or corner contact is not overlap.
std::vector<std::uint32_t> overlapping_faces(Floats uvs, Indices tris, Indices hidden);

// Invariant: answers ascend; a vertex that no whole triangle references stays visible.
std::vector<std::uint32_t> visible_verts(std::size_t vertex_count, Indices tris, Indices hidden);
// Invariant: the nearest edge within max_dist in the triangle's own corner order, the earlier edge on a tie.
std::optional<IndexPair> pick_edge(Floats uvs, Indices tris, Indices hidden, double u, double v, double max_dist);
// Invariant: the last visible non-degenerate triangle containing the point, a point on an edge counts.
std::optional<std::array<std::uint32_t, 3>> pick_face(Floats uvs, Indices tris, Indices hidden, double u, double v);
// Invariant: answers ascend; vertex mode ignores hidden-only vertices, face mode needs all three corners in the box, edge mode takes the edges with both ends in it.
std::vector<std::uint32_t> box_verts(Floats uvs, Indices tris, Indices hidden, const Box& box, BoxMode mode);
// Invariant: answers the entries of visible that are not selected, in visible order, each once.
std::vector<std::uint32_t> invert(Indices visible, Indices selection);
// Invariant: answers ascend; every vertex of every island that holds a selected vertex.
std::vector<std::uint32_t> linked(Indices ids, Indices selection);
// Invariant: answers ascend triangle ids whose three corners are selected and which are not hidden.
std::vector<std::uint32_t> selected_tris(Indices tris, std::size_t vertex_count, Indices selection, Indices hidden);
std::vector<float> merge_at_centre(Floats uvs, Indices selection);
// Constraint: selected vertices within dist of each other, transitively, move to their mean; a dist below 1e-9 or NaN changes nothing.
std::vector<float> merge_by_distance(Floats uvs, Indices selection, double dist);
std::vector<float> align_sel(Floats uvs, Indices selection, Axis axis, Align to);
std::vector<float> snap_grid(Floats uvs, Indices selection, double step);
// Constraint: centre mode keeps the integer tile of a UV and snaps within it; an integer boundary snaps to the tile nearer zero.
std::vector<float> snap_pixels(Floats uvs, Indices selection, double width, double height, bool centre);
std::vector<float> clamp_tile(Floats uvs, Indices selection);

// Invariant: every edge of a visible triangle with both ends selected, as (lo, hi) with lo < hi, once, in order of first appearance.
std::vector<IndexPair> seams_in_selection(Indices tris, Indices selection, Indices hidden);
// Invariant: sources[k] is the vertex copied to the new vertex vertex_count + k; the first fan group of a seam vertex keeps its index and tris is the input with the new corners.
SplitPlan split_plan(std::size_t vertex_count, Indices tris, std::span<const IndexPair> seams);

}  // namespace fo4::nif::uv
