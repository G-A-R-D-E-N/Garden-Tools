#include "fo4/c_api_nif_mesh.h"

#include "c_api_internal.hpp"
#include "fo4/nif/uv.hpp"

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

template <typename T>
bool missing(const T* values, std::size_t count) {
  return count != 0 && values == nullptr;
}

bool too_many(std::size_t vertex_count) { return vertex_count > std::numeric_limits<std::uint32_t>::max(); }

template <typename T>
std::span<const T> view(const T* values, std::size_t count) {
  return count == 0 ? std::span<const T>() : std::span<const T>(values, count);
}

radicle_status copy_floats(const std::vector<float>& values, float** out, std::size_t* out_count) {
  return copy_out(values, out, out_count, {k_args, k_alloc});
}

}  // namespace

extern "C" {

radicle_status radicle_uv_islands(size_t vertex_count, const uint32_t* tris, size_t tri_index_count, uint32_t** out_ids, size_t* out_count) {
  clear_error();
  if (out_ids) *out_ids = nullptr;
  if (out_count) *out_count = 0;
  if (!out_ids || !out_count || too_many(vertex_count) || missing(tris, tri_index_count)) return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  return guard_exceptions("unknown exception while numbering UV islands", [&] {
    const auto ids = uv::islands(vertex_count, view(tris, tri_index_count));
    return copy_out(ids, out_ids, out_count, {k_args, k_alloc});
  });
}

radicle_status radicle_uv_resolve_pivot(const float* uvs,
                                        size_t vertex_count,
                                        const uint32_t* selection,
                                        size_t selection_count,
                                        uint32_t mode,
                                        float* out_u,
                                        float* out_v) {
  clear_error();
  if (out_u) *out_u = 0.0f;
  if (out_v) *out_v = 0.0f;
  if (!out_u || !out_v || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  if (mode > 2) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the pivot mode must be 0, 1 or 2");
  return guard_exceptions("unknown exception while resolving the UV pivot", [&] {
    const auto pivot = uv::resolve_pivot(view(uvs, 2 * vertex_count), view(selection, selection_count), static_cast<uv::PivotMode>(mode));
    *out_u = static_cast<float>(pivot.u);
    *out_v = static_cast<float>(pivot.v);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_uv_transform(const float* uvs,
                                    size_t vertex_count,
                                    const uint32_t* selection,
                                    size_t selection_count,
                                    double translate_u,
                                    double translate_v,
                                    double scale_u,
                                    double scale_v,
                                    double rotation_radians,
                                    double pivot_u,
                                    double pivot_v,
                                    float** out_uvs,
                                    size_t* out_count) {
  clear_error();
  if (out_uvs) *out_uvs = nullptr;
  if (out_count) *out_count = 0;
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while transforming UVs", [&] {
    const uv::Xform xform{translate_u, translate_v, scale_u, scale_v, rotation_radians, {pivot_u, pivot_v}};
    return copy_floats(uv::transform_sel(view(uvs, 2 * vertex_count), view(selection, selection_count), xform), out_uvs, out_count);
  });
}

radicle_status radicle_uv_pack(const float* uvs,
                               size_t vertex_count,
                               const uint32_t* ids,
                               const uint32_t* selection,
                               size_t selection_count,
                               double margin,
                               float** out_uvs,
                               size_t* out_count,
                               uint8_t* out_fits) {
  clear_error();
  if (out_uvs) *out_uvs = nullptr;
  if (out_count) *out_count = 0;
  if (out_fits) *out_fits = 0;
  if (!out_uvs || !out_count || !out_fits || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(ids, vertex_count) ||
      missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while packing UV islands", [&] {
    const auto input = view(uvs, 2 * vertex_count);
    const auto packed = uv::pack_islands(input, view(ids, vertex_count), view(selection, selection_count), margin);
    const std::vector<float> values = packed ? *packed : std::vector<float>(input.begin(), input.end());
    const auto status = copy_floats(values, out_uvs, out_count);
    if (status == RADICLE_STATUS_OK) *out_fits = packed ? 1 : 0;
    return status;
  });
}

radicle_status radicle_uv_average_scale(const float* uvs,
                                        const float* positions,
                                        size_t vertex_count,
                                        const uint32_t* tris,
                                        size_t tri_index_count,
                                        const uint32_t* ids,
                                        const uint32_t* selection,
                                        size_t selection_count,
                                        float** out_uvs,
                                        size_t* out_count) {
  clear_error();
  if (out_uvs) *out_uvs = nullptr;
  if (out_count) *out_count = 0;
  if (!out_uvs || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(positions, 3 * vertex_count) ||
      missing(tris, tri_index_count) || missing(ids, vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while averaging UV island scale", [&] {
    return copy_floats(uv::average_island_scale(view(uvs, 2 * vertex_count), view(positions, 3 * vertex_count), view(tris, tri_index_count),
                                                view(ids, vertex_count), view(selection, selection_count)),
                       out_uvs, out_count);
  });
}

radicle_status radicle_uv_tri_stretch(const float* uvs,
                                      const float* positions,
                                      size_t vertex_count,
                                      const uint32_t* tris,
                                      size_t tri_index_count,
                                      float** out_values,
                                      size_t* out_count) {
  clear_error();
  if (out_values) *out_values = nullptr;
  if (out_count) *out_count = 0;
  if (!out_values || !out_count || too_many(vertex_count) || missing(uvs, 2 * vertex_count) || missing(positions, 3 * vertex_count) ||
      missing(tris, tri_index_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while measuring UV stretch", [&] {
    return copy_floats(uv::tri_stretch(view(uvs, 2 * vertex_count), view(positions, 3 * vertex_count), view(tris, tri_index_count)),
                       out_values, out_count);
  });
}

radicle_status radicle_uv_smart_project(const float* uvs,
                                        const float* positions,
                                        size_t vertex_count,
                                        const uint32_t* tris,
                                        size_t tri_index_count,
                                        const uint32_t* ids,
                                        const uint32_t* selection,
                                        size_t selection_count,
                                        double margin,
                                        float** out_uvs,
                                        size_t* out_count,
                                        uint32_t* out_projected,
                                        uint32_t* out_skipped,
                                        uint8_t* out_fits) {
  clear_error();
  if (out_uvs) *out_uvs = nullptr;
  if (out_count) *out_count = 0;
  if (out_projected) *out_projected = 0;
  if (out_skipped) *out_skipped = 0;
  if (out_fits) *out_fits = 0;
  if (!out_uvs || !out_count || !out_projected || !out_skipped || !out_fits || too_many(vertex_count) || missing(uvs, 2 * vertex_count) ||
      missing(positions, 3 * vertex_count) || missing(tris, tri_index_count) || missing(ids, vertex_count) || missing(selection, selection_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while projecting UVs", [&] {
    const auto result = uv::smart_project(view(uvs, 2 * vertex_count), view(positions, 3 * vertex_count), view(tris, tri_index_count),
                                          view(ids, vertex_count), view(selection, selection_count), margin);
    const auto status = copy_floats(result.uvs, out_uvs, out_count);
    if (status == RADICLE_STATUS_OK) {
      *out_projected = result.projected;
      *out_skipped = result.skipped;
      *out_fits = result.fits ? 1 : 0;
    }
    return status;
  });
}

radicle_status radicle_uv_lscm_unwrap(const float* uvs,
                                      const float* positions,
                                      size_t vertex_count,
                                      const uint32_t* tris,
                                      size_t tri_index_count,
                                      const uint32_t* ids,
                                      const uint32_t* selection,
                                      size_t selection_count,
                                      const uint32_t* pin_vertices,
                                      const float* pin_uvs,
                                      size_t pin_count,
                                      float** out_uvs,
                                      size_t* out_count,
                                      uint32_t* out_unwrapped,
                                      uint32_t* out_skipped) {
  clear_error();
  if (out_uvs) *out_uvs = nullptr;
  if (out_count) *out_count = 0;
  if (out_unwrapped) *out_unwrapped = 0;
  if (out_skipped) *out_skipped = 0;
  if (!out_uvs || !out_count || !out_unwrapped || !out_skipped || too_many(vertex_count) || pin_count > std::numeric_limits<std::uint32_t>::max() ||
      missing(uvs, 2 * vertex_count) || missing(positions, 3 * vertex_count) || missing(tris, tri_index_count) || missing(ids, vertex_count) ||
      missing(selection, selection_count) || missing(pin_vertices, pin_count) || missing(pin_uvs, 2 * pin_count)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, k_args);
  }
  return guard_exceptions("unknown exception while unwrapping UVs", [&] {
    std::vector<uv::Pin> pins;
    pins.reserve(pin_count);
    for (std::size_t i = 0; i < pin_count; ++i) pins.push_back({pin_vertices[i], pin_uvs[2 * i], pin_uvs[2 * i + 1]});
    const auto result = uv::lscm_unwrap(view(uvs, 2 * vertex_count), view(positions, 3 * vertex_count), view(tris, tri_index_count),
                                        view(ids, vertex_count), view(selection, selection_count), pins);
    const auto status = copy_floats(result.uvs, out_uvs, out_count);
    if (status == RADICLE_STATUS_OK) {
      *out_unwrapped = result.unwrapped;
      *out_skipped = result.skipped;
    }
    return status;
  });
}

}  // extern "C"
