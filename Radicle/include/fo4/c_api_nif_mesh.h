#pragma once

#include "fo4/c_api_nif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Constraint: any flag bit other than RADICLE_MESH_FLIP_WINDING reports RADICLE_STATUS_INVALID_ARGUMENT. */
typedef enum radicle_mesh_flag {
  RADICLE_MESH_FLIP_WINDING = 1
} radicle_mesh_flag;

typedef struct radicle_mesh_options {
  float units_per_metre;
  uint32_t flags;
} radicle_mesh_options;

typedef struct radicle_mesh_transform {
  float basis[9];
  float origin[3];
} radicle_mesh_transform;

typedef struct radicle_mesh_skin {
  size_t bone_count;
  const char* const* bone_names;
  const radicle_mesh_transform* inverse_binds;
  const uint16_t* bone_indices;
  const float* bone_weights;
} radicle_mesh_skin;

/* Invariant: an attribute the NIF lacks is NULL; skin is NULL for a rigid part; material -1 means none. */
typedef struct radicle_mesh_part {
  const char* name;
  uint32_t block;
  size_t vertex_count;
  size_t index_count;
  const float* positions;
  const float* normals;
  const float* tangents;
  const float* uv0;
  const float* uv1;
  const float* colors;
  const uint32_t* indices;
  float bounds_min[3];
  float bounds_max[3];
  float bounds_center[3];
  float bounds_radius;
  int32_t material;
  const radicle_mesh_skin* skin;
  const char* warning;
} radicle_mesh_part;

/* Constraint: kind is 1 lighting, 2 effect, 3 other; alpha_mode is 0 opaque, 1 mask, 2 blend.
 * Invariant: shader values are the raw block fields (zero when the kind does not store them); effect_* are zero unless kind is 2 and lighting values zero unless kind is 1.
 * Invariant: an effect shader reports emissive_color and emissive_multiple as its base colour and scale, and has_env_map says whether the block stores an environment map scale.
 * Constraint: alpha_source_blend and alpha_destination_blend are NiAlphaProperty blend enums (0 one, 1 zero, 6 source alpha, 7 inverse source alpha, up to 10) and are meaningful only when has_alpha_property is 1. */
typedef struct radicle_mesh_material {
  const char* key;
  const char* shader_kind;
  uint8_t kind;
  uint8_t alpha_mode;
  float alpha_cutoff;
  const char* diffuse;
  const char* normal;
  const char* glow;
  const char* greyscale;
  const char* cubemap;
  const char* env_mask;
  const char* smooth_spec;
  float uv_offset[2];
  float uv_scale[2];
  uint8_t has_skin_tint;
  float skin_tint[3];
  uint8_t two_sided;
  uint8_t vertex_colors;
  uint8_t vertex_alpha;
  uint8_t z_write;
  uint8_t cast_shadows;
  uint8_t model_space_normals;
  uint8_t decal;
  uint8_t specular;
  uint8_t environment_mapping;
  uint8_t own_emit;
  uint8_t glow_map;
  uint8_t palette_color;
  uint8_t hair;
  const char* root_material;
  uint32_t shader_type;
  uint32_t shader_flags1;
  uint32_t shader_flags2;
  uint32_t texture_clamp;
  float emissive_color[3];
  float emissive_multiple;
  float env_map_scale;
  float glossiness;
  float specular_color[3];
  float specular_strength;
  float alpha;
  float fresnel_power;
  float subsurface_rolloff;
  float rimlight_power;
  float backlight_power;
  float grayscale_to_palette_scale;
  float effect_base_color[4];
  float effect_base_color_scale;
  float effect_falloff_start_angle;
  float effect_falloff_stop_angle;
  float effect_falloff_start_opacity;
  float effect_falloff_stop_opacity;
  float effect_soft_falloff_depth;
  uint8_t has_alpha_property;
  uint8_t alpha_source_blend;
  uint8_t alpha_destination_blend;
  uint8_t alpha_threshold;
  uint8_t has_env_map;
  uint8_t has_backlight;
  uint8_t has_grayscale_to_palette_scale;
} radicle_mesh_material;

/* Views require radicle_nif_mesh_free and do not borrow the radicle_nif. */
typedef struct radicle_mesh {
  const radicle_mesh_part* parts;
  size_t part_count;
  const radicle_mesh_material* materials;
  size_t material_count;
  void* internal;
} radicle_mesh;

RADICLE_API radicle_status radicle_nif_mesh_read(const radicle_nif* nif,
                                                 const radicle_mesh_options* options,
                                                 radicle_mesh** out_mesh);
RADICLE_API void radicle_nif_mesh_free(radicle_mesh* mesh);

typedef struct radicle_tri radicle_tri;

/* Constraint: only the FRTRI003 shape every shipped head file has is read (no quads, no modifier morphs); anything else is PARSE_ERROR. Requires radicle_tri_free. */
RADICLE_API radicle_status radicle_tri_read(const uint8_t* bytes, size_t size, radicle_tri** out_tri);
RADICLE_API void radicle_tri_free(radicle_tri* tri);
RADICLE_API radicle_status radicle_tri_counts(const radicle_tri* tri,
                                              uint32_t* out_vertex_count,
                                              uint32_t* out_triangle_count,
                                              uint32_t* out_morph_count);
/* Invariant: the name is NUL-terminated and stays valid until radicle_tri_free. */
RADICLE_API radicle_status radicle_tri_morph_name(const radicle_tri* tri, uint32_t index, const char** out_name);
/* Invariant: 3 * vertex_count floats in the radicle_nif_mesh_read frame (NIF x, z, -y, in metres; units_per_metre 0 selects the default), released with radicle_free_floats. */
RADICLE_API radicle_status radicle_tri_positions(const radicle_tri* tri,
                                                 float units_per_metre,
                                                 float** out_values,
                                                 size_t* out_count);
/* Invariant: 3 * vertex_count floats, per-vertex offsets from the tri positions in the same frame as radicle_tri_positions, released with radicle_free_floats. */
RADICLE_API radicle_status radicle_tri_morph_deltas(const radicle_tri* tri,
                                                    uint32_t index,
                                                    float units_per_metre,
                                                    float** out_values,
                                                    size_t* out_count);

/* Invariant: every radicle_uv_* export is a pure function of its arguments. The arrays hold 2 * vertex_count UV floats, 3 * vertex_count position floats and vertex_count island ids; a selection or triangle index at or past vertex_count is ignored. Count outputs are zeroed on failure. */
/* Invariant: out_ids holds vertex_count island ids numbered by first vertex appearance, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_islands(size_t vertex_count,
                                              const uint32_t* tris,
                                              size_t tri_index_count,
                                              uint32_t** out_ids,
                                              size_t* out_count);
/* Constraint: mode is 0 centre of the bounds, 1 per-axis median, 2 the tile centre (0.5, 0.5); an empty selection answers (0.5, 0.5). */
RADICLE_API radicle_status radicle_uv_resolve_pivot(const float* uvs,
                                                    size_t vertex_count,
                                                    const uint32_t* selection,
                                                    size_t selection_count,
                                                    uint32_t mode,
                                                    float* out_u,
                                                    float* out_v);
/* Invariant: the scale is applied before the rotation, both about the pivot, then the translation; out_uvs holds 2 * vertex_count floats released with radicle_free_floats. */
RADICLE_API radicle_status radicle_uv_transform(const float* uvs,
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
                                                size_t* out_count);
/* Constraint: islands with a selected vertex move whole into the 0-1 tile; out_fits 0 returns the input UVs unchanged. Release out_uvs with radicle_free_floats. */
RADICLE_API radicle_status radicle_uv_pack(const float* uvs,
                                           size_t vertex_count,
                                           const uint32_t* ids,
                                           const uint32_t* selection,
                                           size_t selection_count,
                                           double margin,
                                           float** out_uvs,
                                           size_t* out_count,
                                           uint8_t* out_fits);
RADICLE_API radicle_status radicle_uv_average_scale(const float* uvs,
                                                    const float* positions,
                                                    size_t vertex_count,
                                                    const uint32_t* tris,
                                                    size_t tri_index_count,
                                                    const uint32_t* ids,
                                                    const uint32_t* selection,
                                                    size_t selection_count,
                                                    float** out_uvs,
                                                    size_t* out_count);
/* Invariant: one value in 0..1 per whole triangle, 0 for a degenerate or out-of-range triangle, released with radicle_free_floats. */
RADICLE_API radicle_status radicle_uv_tri_stretch(const float* uvs,
                                                  const float* positions,
                                                  size_t vertex_count,
                                                  const uint32_t* tris,
                                                  size_t tri_index_count,
                                                  float** out_values,
                                                  size_t* out_count);
/* Constraint: out_projected counts projected vertices and out_skipped the selected islands with no usable plane; out_fits 0 returns the input UVs unchanged with out_projected 0. */
RADICLE_API radicle_status radicle_uv_smart_project(const float* uvs,
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
                                                    uint8_t* out_fits);
/* Constraint: pin_uvs holds 2 floats per pin, a repeated pin vertex keeps the last; an island needs two pins with distinct UVs in one connected patch, and unsolved islands are counted in out_skipped and left as they were. */
RADICLE_API radicle_status radicle_uv_lscm_unwrap(const float* uvs,
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
                                                  uint32_t* out_skipped);

/* Invariant: the UV edit exports below follow the radicle_uv_* rules above; a hidden list holds triangle indices, a selection holds vertex indices, and a repeated selection entry counts once. */
/* Invariant: out_verts holds the vertices of edges used by exactly one triangle, ascending, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_boundary_verts(size_t vertex_count, const uint32_t* tris, size_t tri_index_count, uint32_t** out_verts, size_t* out_count);
/* Constraint: iterations is floored and clamped to 0..200, out_uvs holds 2 * vertex_count floats, and only selected non-boundary vertices move. */
RADICLE_API radicle_status radicle_uv_relax(const float* uvs,
                                            size_t vertex_count,
                                            const uint32_t* tris,
                                            size_t tri_index_count,
                                            const uint32_t* selection,
                                            size_t selection_count,
                                            double iterations,
                                            float** out_uvs,
                                            size_t* out_count);
/* Invariant: out_pairs holds out_pair_count (lower, higher) vertex pairs as 2 * out_pair_count uint32 values, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_find_stitch_pairs(const float* positions,
                                                        size_t vertex_count,
                                                        const uint32_t* ids,
                                                        const uint32_t* selection,
                                                        size_t selection_count,
                                                        double eps,
                                                        uint32_t** out_pairs,
                                                        size_t* out_pair_count);
RADICLE_API radicle_status radicle_uv_stitch(const float* uvs,
                                             const float* positions,
                                             size_t vertex_count,
                                             const uint32_t* ids,
                                             const uint32_t* selection,
                                             size_t selection_count,
                                             double eps,
                                             float** out_uvs,
                                             size_t* out_count);
/* Invariant: out_faces holds triangle indices, ascending, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_overlapping_faces(const float* uvs,
                                                        size_t vertex_count,
                                                        const uint32_t* tris,
                                                        size_t tri_index_count,
                                                        const uint32_t* hidden,
                                                        size_t hidden_count,
                                                        uint32_t** out_faces,
                                                        size_t* out_count);
RADICLE_API radicle_status radicle_uv_visible_verts(size_t vertex_count,
                                                    const uint32_t* tris,
                                                    size_t tri_index_count,
                                                    const uint32_t* hidden,
                                                    size_t hidden_count,
                                                    uint32_t** out_verts,
                                                    size_t* out_count);
/* Constraint: out_found 0 means no edge is within max_dist and out_a and out_b stay 0; otherwise they are the edge ends in triangle winding order. */
RADICLE_API radicle_status radicle_uv_pick_edge(const float* uvs,
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
                                                uint8_t* out_found);
/* Requires out_corners to point at 3 uint32 values; out_found 0 means no visible face holds the point. */
RADICLE_API radicle_status radicle_uv_pick_face(const float* uvs,
                                                size_t vertex_count,
                                                const uint32_t* tris,
                                                size_t tri_index_count,
                                                const uint32_t* hidden,
                                                size_t hidden_count,
                                                double u,
                                                double v,
                                                uint32_t* out_corners,
                                                uint8_t* out_found);
/* Constraint: mode is 0 vertex, 1 edge (selects both ends of each edge inside the box) or 2 face (selects the corners of each face inside the box); any other value is INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_uv_box_verts(const float* uvs,
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
                                                size_t* out_count);
RADICLE_API radicle_status radicle_uv_invert(const uint32_t* visible,
                                             size_t visible_count,
                                             const uint32_t* selection,
                                             size_t selection_count,
                                             uint32_t** out_verts,
                                             size_t* out_count);
RADICLE_API radicle_status radicle_uv_linked(const uint32_t* ids,
                                             size_t vertex_count,
                                             const uint32_t* selection,
                                             size_t selection_count,
                                             uint32_t** out_verts,
                                             size_t* out_count);
/* Invariant: out_faces holds the visible triangles whose three corners are all selected, ascending, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_selected_tris(const uint32_t* tris,
                                                    size_t tri_index_count,
                                                    size_t vertex_count,
                                                    const uint32_t* selection,
                                                    size_t selection_count,
                                                    const uint32_t* hidden,
                                                    size_t hidden_count,
                                                    uint32_t** out_faces,
                                                    size_t* out_count);
RADICLE_API radicle_status radicle_uv_merge_at_centre(const float* uvs,
                                                      size_t vertex_count,
                                                      const uint32_t* selection,
                                                      size_t selection_count,
                                                      float** out_uvs,
                                                      size_t* out_count);
RADICLE_API radicle_status radicle_uv_merge_by_distance(const float* uvs,
                                                        size_t vertex_count,
                                                        const uint32_t* selection,
                                                        size_t selection_count,
                                                        double distance,
                                                        float** out_uvs,
                                                        size_t* out_count);
/* Constraint: axis is 0 u or 1 v and to is 0 min, 1 centre or 2 max; any other value is INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_uv_align(const float* uvs,
                                            size_t vertex_count,
                                            const uint32_t* selection,
                                            size_t selection_count,
                                            uint32_t axis,
                                            uint32_t to,
                                            float** out_uvs,
                                            size_t* out_count);
RADICLE_API radicle_status radicle_uv_snap_grid(const float* uvs,
                                                size_t vertex_count,
                                                const uint32_t* selection,
                                                size_t selection_count,
                                                double step,
                                                float** out_uvs,
                                                size_t* out_count);
RADICLE_API radicle_status radicle_uv_snap_pixels(const float* uvs,
                                                  size_t vertex_count,
                                                  const uint32_t* selection,
                                                  size_t selection_count,
                                                  double width,
                                                  double height,
                                                  uint8_t centre,
                                                  float** out_uvs,
                                                  size_t* out_count);
RADICLE_API radicle_status radicle_uv_clamp_tile(const float* uvs,
                                                 size_t vertex_count,
                                                 const uint32_t* selection,
                                                 size_t selection_count,
                                                 float** out_uvs,
                                                 size_t* out_count);
/* Invariant: out_seams holds out_pair_count (lower, higher) vertex pairs as 2 * out_pair_count uint32 values, released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_seams_in_selection(const uint32_t* tris,
                                                         size_t tri_index_count,
                                                         const uint32_t* selection,
                                                         size_t selection_count,
                                                         const uint32_t* hidden,
                                                         size_t hidden_count,
                                                         uint32_t** out_seams,
                                                         size_t* out_pair_count);
/* Invariant: seam_edges holds seam_pair_count vertex pairs in either order; out_sources holds the source vertex of each created vertex in order from vertex_count, and out_tris holds the tri_index_count rewritten corners, both released with radicle_free_node_refs. */
RADICLE_API radicle_status radicle_uv_split_plan(size_t vertex_count,
                                                 const uint32_t* tris,
                                                 size_t tri_index_count,
                                                 const uint32_t* seam_edges,
                                                 size_t seam_pair_count,
                                                 uint32_t** out_sources,
                                                 size_t* out_source_count,
                                                 uint32_t** out_tris,
                                                 size_t* out_tri_count);

#ifdef __cplusplus
}
#endif
