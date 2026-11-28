#include "fo4/c_api_nif_mesh.h"

#include "c_api_internal.hpp"
#include "fo4/nif/uv.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace radicle_c_detail;
namespace uv = fo4::nif::uv;

constexpr const char* k_args = "the UV arrays, counts and every output are required";
constexpr const char* k_alloc = "failed to allocate the UV result";
constexpr const char* k_mode = "the box mode must be 0, 1 or 2 and the align axis 0 or 1 and target 0, 1 or 2";

template <typename T>
bool missing(const T* values, std::size_t count) {
  return count != 0 && values == nullptr;
}

bool too_many(std::size_t count) { return count > std::numeric_limits<std::uint32_t>::max(); }

template <typename T>
std::span<const T> view(const T* values, std::size_t count) {
  return count == 0 ? std::span<const T>() : std::span<const T>(values, count);
}

radicle_status copy_floats(const std::vector<float>& values, float** out, std::size_t* out_count) {
  return copy_out(values, out, out_count, {k_args, k_alloc});
}

radicle_status copy_ids(const std::vector<std::uint32_t>& values, std::uint32_t** out, std::size_t* out_count) {
  return copy_out(values, out, out_count, {k_args, k_alloc});
}

radicle_status copy_pairs(const std::vector<uv::IndexPair>& pairs, std::uint32_t** out, std::size_t* out_pair_count) {
  std::vector<std::uint32_t> flat;
  flat.reserve(2 * pairs.size());
  for (const uv::IndexPair& pair : pairs) {
    flat.push_back(pair.first);
    flat.push_back(pair.second);
  }
  const auto status = copy_out(flat, out, out_pair_count, {k_args, k_alloc});
  if (status == RADICLE_STATUS_OK) *out_pair_count = pairs.size();
  return status;
}

void zero_ids(std::uint32_t** out, std::size_t* out_count) {
  if (out) *out = nullptr;
  if (out_count) *out_count = 0;
}

void zero_floats(float** out, std::size_t* out_count) {
  if (out) *out = nullptr;
  if (out_count) *out_count = 0;
}

}  // namespace

extern "C" {

radicle_status radicle_uv_boundary_verts(size_t vertex_count, const uint32_t* tris, size_t tri_index_count, uint32_t** out_verts, size_t* out_count) {
  clear_error();
  zero_ids(out_verts, out_count);
  if (!out_verts || !out_count || too_many(vertex_count) || missing(tris, tri_index_count)) return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  return guard_exceptions("unknown exception while finding UV boundary vertices", [&] {
    return copy_ids(uv::boundary_verts(vertex_count, view(tris, tri_index_count)), out_verts, out_count);
  });
}

radicle_status radicle_uv_relax(const float* uvs,
                                size_t vertex_count,
                                const uint32_t* tris,
                                size_t tri_index_count,
                                const uint32_t* selection,
                                size_t selection_count,
                                double iterations,
                                float** out_uvs,
                                size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(tris, tri_index_count) ||
      missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while relaxing UVs", [&] {
    return copy_floats(uv::relax_uvs(view(uvs, 2 * vertex_count), view(tris, tri_index_count), view(selection, selection_count), iterations),
                       out_uvs, out_count);
  });
}

radicle_status radicle_uv_find_stitch_pairs(const float* positions,
                                            size_t vertex_count,
                                            const uint32_t* ids,
                                            const uint32_t* selection,
                                            size_t selection_count,
                                            double eps,
                                            uint32_t** out_pairs,
                                            size_t* out_pair_count) {
  clear_error();
  zero_ids(out_pairs, out_pair_count);
  if (!out_pairs || !out_pair_count || too_many(vertex_count) || missing(positions, 3 * vertex_count) || missing(ids, vertex_count) ||
      missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while finding UV stitch pairs", [&] {
    return copy_pairs(uv::find_stitch_pairs(view(positions, 3 * vertex_count), view(ids, vertex_count), view(selection, selection_count), eps),
                      out_pairs, out_pair_count);
  });
}

radicle_status radicle_uv_stitch(const float* uvs,
                                 const float* positions,
                                 size_t vertex_count,
                                 const uint32_t* ids,
                                 const uint32_t* selection,
                                 size_t selection_count,
                                 double eps,
                                 float** out_uvs,
                                 size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(positions, 3 * vertex_count) ||
      missing(ids, vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while stitching UVs", [&] {
    return copy_floats(uv::stitch_selection(view(uvs, 2 * vertex_count), view(positions, 3 * vertex_count), view(ids, vertex_count),
                                            view(selection, selection_count), eps),
                       out_uvs, out_count);
  });
}

radicle_status radicle_uv_overlapping_faces(const float* uvs,
                                            size_t vertex_count,
                                            const uint32_t* tris,
                                            size_t tri_index_count,
                                            const uint32_t* hidden,
                                            size_t hidden_count,
                                            uint32_t** out_faces,
                                            size_t* out_count) {
  clear_error();
  zero_ids(out_faces, out_count);
  if (!out_faces || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(tris, tri_index_count) ||
      missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while finding overlapping UV faces", [&] {
    return copy_ids(uv::overlapping_faces(view(uvs, 2 * vertex_count), view(tris, tri_index_count), view(hidden, hidden_count)), out_faces,
                    out_count);
  });
}

radicle_status radicle_uv_visible_verts(size_t vertex_count,
                                        const uint32_t* tris,
                                        size_t tri_index_count,
                                        const uint32_t* hidden,
                                        size_t hidden_count,
                                        uint32_t** out_verts,
                                        size_t* out_count) {
  clear_error();
  zero_ids(out_verts, out_count);
  if (!out_verts || !out_count || too_many(vertex_count) || missing(tris, tri_index_count) || missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while listing visible UV vertices", [&] {
    return copy_ids(uv::visible_verts(vertex_count, view(tris, tri_index_count), view(hidden, hidden_count)), out_verts, out_count);
  });
}

radicle_status radicle_uv_pick_edge(const float* uvs,
                                    size_t vertex_count,
                                    const uint32_t* tris,
                                    size_t tri_index_count,
                                    const uint32_t* hidden,
                                    size_t hidden_count,
                                    double u,
                                    double v,
                                    double max_dist,
                                    uint32_t* out_a,
                                    uint32_t* out_b,
                                    uint8_t* out_found) {
  clear_error();
  if (out_a) *out_a = 0;
  if (out_b) *out_b = 0;
  if (out_found) *out_found = 0;
  if (!out_a || !out_b || !out_found || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(tris, tri_index_count) ||
      missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while picking a UV edge", [&] {
    const auto edge = uv::pick_edge(view(uvs, 2 * vertex_count), view(tris, tri_index_count), view(hidden, hidden_count), u, v, max_dist);
    if (edge) {
      *out_a = edge->first;
      *out_b = edge->second;
      *out_found = 1;
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_uv_pick_face(const float* uvs,
                                    size_t vertex_count,
                                    const uint32_t* tris,
                                    size_t tri_index_count,
                                    const uint32_t* hidden,
                                    size_t hidden_count,
                                    double u,
                                    double v,
                                    uint32_t* out_corners,
                                    uint8_t* out_found) {
  clear_error();
  if (out_corners) out_corners[0] = out_corners[1] = out_corners[2] = 0;
  if (out_found) *out_found = 0;
  if (!out_corners || !out_found || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(tris, tri_index_count) ||
      missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while picking a UV face", [&] {
    const auto face = uv::pick_face(view(uvs, 2 * vertex_count), view(tris, tri_index_count), view(hidden, hidden_count), u, v);
    if (face) {
      for (std::size_t i = 0; i < 3; ++i) out_corners[i] = (*face)[i];
      *out_found = 1;
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_uv_box_verts(const float* uvs,
                                    size_t vertex_count,
                                    const uint32_t* tris,
                                    size_t tri_index_count,
                                    const uint32_t* hidden,
                                    size_t hidden_count,
                                    double min_u,
                                    double min_v,
                                    double max_u,
                                    double max_v,
                                    uint32_t mode,
                                    uint32_t** out_verts,
                                    size_t* out_count) {
  clear_error();
  zero_ids(out_verts, out_count);
  if (!out_verts || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(tris, tri_index_count) ||
      missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  if (mode > 2) return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_mode);
  return guard_exceptions("unknown exception while box selecting UV vertices", [&] {
    const uv::Box box{min_u, min_v, max_u, max_v};
    return copy_ids(uv::box_verts(view(uvs, 2 * vertex_count), view(tris, tri_index_count), view(hidden, hidden_count), box,
                                  static_cast<uv::BoxMode>(mode)),
                    out_verts, out_count);
  });
}

radicle_status radicle_uv_invert(const uint32_t* visible,
                                 size_t visible_count,
                                 const uint32_t* selection,
                                 size_t selection_count,
                                 uint32_t** out_verts,
                                 size_t* out_count) {
  clear_error();
  zero_ids(out_verts, out_count);
  if (!out_verts || !out_count || missing(visible, visible_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while inverting the UV selection", [&] {
    return copy_ids(uv::invert(view(visible, visible_count), view(selection, selection_count)), out_verts, out_count);
  });
}

radicle_status radicle_uv_linked(const uint32_t* ids,
                                 size_t vertex_count,
                                 const uint32_t* selection,
                                 size_t selection_count,
                                 uint32_t** out_verts,
                                 size_t* out_count) {
  clear_error();
  zero_ids(out_verts, out_count);
  if (!out_verts || !out_count || too_many(vertex_count) || missing(ids, vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while selecting linked UV vertices", [&] {
    return copy_ids(uv::linked(view(ids, vertex_count), view(selection, selection_count)), out_verts, out_count);
  });
}

radicle_status radicle_uv_selected_tris(const uint32_t* tris,
                                        size_t tri_index_count,
                                        size_t vertex_count,
                                        const uint32_t* selection,
                                        size_t selection_count,
                                        const uint32_t* hidden,
                                        size_t hidden_count,
                                        uint32_t** out_faces,
                                        size_t* out_count) {
  clear_error();
  zero_ids(out_faces, out_count);
  if (!out_faces || !out_count || too_many(vertex_count) || missing(tris, tri_index_count) || missing(selection, selection_count) ||
      missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while listing selected UV triangles", [&] {
    return copy_ids(uv::selected_tris(view(tris, tri_index_count), vertex_count, view(selection, selection_count), view(hidden, hidden_count)),
                    out_faces, out_count);
  });
}

radicle_status radicle_uv_merge_at_centre(const float* uvs,
                                          size_t vertex_count,
                                          const uint32_t* selection,
                                          size_t selection_count,
                                          float** out_uvs,
                                          size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while merging UVs at the centre", [&] {
    return copy_floats(uv::merge_at_centre(view(uvs, 2 * vertex_count), view(selection, selection_count)), out_uvs, out_count);
  });
}

radicle_status radicle_uv_merge_by_distance(const float* uvs,
                                            size_t vertex_count,
                                            const uint32_t* selection,
                                            size_t selection_count,
                                            double distance,
                                            float** out_uvs,
                                            size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while merging UVs by distance", [&] {
    return copy_floats(uv::merge_by_distance(view(uvs, 2 * vertex_count), view(selection, selection_count), distance), out_uvs, out_count);
  });
}

radicle_status radicle_uv_align(const float* uvs,
                                size_t vertex_count,
                                const uint32_t* selection,
                                size_t selection_count,
                                uint32_t axis,
                                uint32_t to,
                                float** out_uvs,
                                size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  if (axis > 1 || to > 2) return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_mode);
  return guard_exceptions("unknown exception while aligning UVs", [&] {
    return copy_floats(uv::align_sel(view(uvs, 2 * vertex_count), view(selection, selection_count), static_cast<uv::Axis>(axis),
                                     static_cast<uv::Align>(to)),
                       out_uvs, out_count);
  });
}

radicle_status radicle_uv_snap_grid(const float* uvs,
                                    size_t vertex_count,
                                    const uint32_t* selection,
                                    size_t selection_count,
                                    double step,
                                    float** out_uvs,
                                    size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while snapping UVs to a grid", [&] {
    return copy_floats(uv::snap_grid(view(uvs, 2 * vertex_count), view(selection, selection_count), step), out_uvs, out_count);
  });
}

radicle_status radicle_uv_snap_pixels(const float* uvs,
                                      size_t vertex_count,
                                      const uint32_t* selection,
                                      size_t selection_count,
                                      double width,
                                      double height,
                                      uint8_t centre,
                                      float** out_uvs,
                                      size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while snapping UVs to pixels", [&] {
    return copy_floats(uv::snap_pixels(view(uvs, 2 * vertex_count), view(selection, selection_count), width, height, centre != 0), out_uvs,
                       out_count);
  });
}

radicle_status radicle_uv_clamp_tile(const float* uvs,
                                     size_t vertex_count,
                                     const uint32_t* selection,
                                     size_t selection_count,
                                     float** out_uvs,
                                     size_t* out_count) {
  clear_error();
  zero_floats(out_uvs, out_count);
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while clamping UVs to the tile", [&] {
    return copy_floats(uv::clamp_tile(view(uvs, 2 * vertex_count), view(selection, selection_count)), out_uvs, out_count);
  });
}

radicle_status radicle_uv_seams_in_selection(const uint32_t* tris,
                                             size_t tri_index_count,
                                             const uint32_t* selection,
                                             size_t selection_count,
                                             const uint32_t* hidden,
                                             size_t hidden_count,
                                             uint32_t** out_seams,
                                             size_t* out_pair_count) {
  clear_error();
  zero_ids(out_seams, out_pair_count);
  if (!out_seams || !out_pair_count || missing(tris, tri_index_count) || missing(selection, selection_count) || missing(hidden, hidden_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while finding UV seams in the selection", [&] {
    return copy_pairs(uv::seams_in_selection(view(tris, tri_index_count), view(selection, selection_count), view(hidden, hidden_count)),
                      out_seams, out_pair_count);
  });
}

radicle_status radicle_uv_split_plan(size_t vertex_count,
                                     const uint32_t* tris,
                                     size_t tri_index_count,
                                     const uint32_t* seam_edges,
                                     size_t seam_pair_count,
                                     uint32_t** out_sources,
                                     size_t* out_source_count,
                                     uint32_t** out_tris,
                                     size_t* out_tri_count) {
  clear_error();
  zero_ids(out_sources, out_source_count);
  zero_ids(out_tris, out_tri_count);
  if (!out_sources || !out_source_count || !out_tris || !out_tri_count || too_many(vertex_count) || too_many(seam_pair_count) ||
      missing(tris, tri_index_count) || missing(seam_edges, 2 * seam_pair_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while planning a UV split", [&] {
    std::vector<uv::IndexPair> seams;
    seams.reserve(seam_pair_count);
    for (std::size_t i = 0; i < seam_pair_count; ++i) seams.push_back({seam_edges[2 * i], seam_edges[2 * i + 1]});
    const auto plan = uv::split_plan(vertex_count, view(tris, tri_index_count), seams);
    auto status = copy_ids(plan.sources, out_sources, out_source_count);
    if (status != RADICLE_STATUS_OK) return status;
    status = copy_ids(plan.tris, out_tris, out_tri_count);
    if (status != RADICLE_STATUS_OK) {
      radicle_free_node_refs(*out_sources);
      zero_ids(out_sources, out_source_count);
    }
    return status;
  });
}

}  // extern "C"
