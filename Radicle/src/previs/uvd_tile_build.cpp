#include "fo4/previs/uvd_tile_build.hpp"

#include "fo4/error.hpp"
#include "fo4/previs/uvd.hpp"
#include "fo4/previs/uvd_tile_body.hpp"
#include "fo4/previs/uvd_tile_check.hpp"
#include "fo4/previs/uvd_tile_header.hpp"
#include "uvd_tile_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace fo4::previs {

using namespace detail;

namespace {

constexpr std::array<std::uint16_t, 2> k_full{0, 0xffff};
constexpr float k_flt_max = std::numeric_limits<float>::max();
constexpr std::uint32_t k_expand_min = 16;
constexpr std::uint32_t k_expand_max = 128;
// Constraint: one exterior cell edge; a ray that needs more than this to hit a wall is not looking at an enclosing room.
constexpr double k_max_enclosure = 4096.0;
// Constraint: lets a ray through the diagonal seam of two wall triangles be blocked by one of them.
constexpr double k_seam = 1e-9;

bool finite_box(const std::array<float, 6>& box) {
  return std::all_of(box.begin(), box.end(), [](float v) { return std::isfinite(v); });
}

void require_box(const std::array<float, 6>& box, const std::string& label) {
  if (!finite_box(box)) {
    fail("build: " + label + " has a non-finite coordinate");
  }
  for (std::size_t a = 0; a < 3; ++a) {
    if (!(box[a] <= box[3 + a])) {
      fail("build: " + label + " has min above max");
    }
  }
}

UvdTileBody open_tile(const std::array<float, 6>& box, std::uint32_t object_count, float expand) {
  UvdTileBody tile;
  tile.box = box;
  tile.leaf = true;
  tile.expand = expand;
  tile.cluster_count = 1;
  UvdCell cell;
  cell.portal_count = k_uvd_build_faces;
  cell.list_count = object_count;
  cell.x = k_full;
  cell.y = k_full;
  cell.z = k_full;
  tile.cells.push_back(cell);
  for (std::uint32_t face = 0; face < k_uvd_build_faces; ++face) {
    UvdPortal portal;
    portal.target = k_uvd_outside_target;
    portal.outside = 1;
    portal.face = face;
    portal.plane = (face & 1u) != 0 ? 0xffffu : 0u;
    portal.rect0 = k_full;
    portal.rect1 = k_full;
    tile.portals.push_back(portal);
  }
  UvdKd kd;
  kd.node_count = 1;
  kd.map_width = 1;
  kd.symbols = {3};
  kd.entries = {0};
  tile.kd = kd;
  return tile;
}

UvdHeader open_header(const UvdBuildInput& input, const std::array<float, 6>& world, float lod) {
  UvdHeader h;
  h.banner = input.banner.empty() ? std::string(k_uvd_default_banner) : input.banner;
  h.lod_base = k_uvd_lod_base;
  h.world_min = {world[0], world[1], world[2]};
  h.world_max = {world[3], world[4], world[5]};
  h.top_symbols = {3};
  h.top_splits = {k_flt_max};
  h.lod = {lod};
  const std::uint32_t count = static_cast<std::uint32_t>(input.objects.size());
  for (const UvdBuildObject& object : input.objects) {
    h.objects.boxes.push_back(object.box);
    h.objects.ids.push_back(object.id);
  }
  const std::uint32_t second_width = static_cast<std::uint32_t>(std::bit_width(count));
  h.widths = {count != 0 ? 1u : 0u, second_width, 0, 0};
  if (count != 0) {
    h.run_a = {{0, count}};
  }
  UvdCluster cluster;
  cluster.x = k_full;
  cluster.y = k_full;
  cluster.z = k_full;
  h.clusters = {cluster};
  UvdKd trivial;
  trivial.node_count = 1;
  trivial.map_width = 1;
  trivial.symbols = {3};
  trivial.entries = {0};
  h.child_trees.assign(k_uvd_build_faces, trivial);
  UvdLeafRowBytes row;
  row.words = {k_uvd_build_faces, 0, 0};
  h.child_rows = {row};
  return h;
}

// Constraint: a body's own triangles lie on its box, so geometry that overlaps the box is skipped and a body never culls itself.
bool triangle_touches_box(const std::array<float, 9>& triangle, const std::array<float, 6>& box) {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const float low = std::min({triangle[axis], triangle[3 + axis], triangle[6 + axis]});
    const float high = std::max({triangle[axis], triangle[3 + axis], triangle[6 + axis]});
    if (high < box[axis] || low > box[3 + axis]) return false;
  }
  return true;
}

// Constraint: Moller-Trumbore without a backface test, so a wall blocks whichever way its winding faces.
bool triangle_blocks_ray(const std::array<float, 9>& triangle, const double origin[3], const double direction[3], double max_distance) {
  const double edge1[3] = {triangle[3] - triangle[0], triangle[4] - triangle[1], triangle[5] - triangle[2]};
  const double edge2[3] = {triangle[6] - triangle[0], triangle[7] - triangle[1], triangle[8] - triangle[2]};
  const double p[3] = {direction[1] * edge2[2] - direction[2] * edge2[1], direction[2] * edge2[0] - direction[0] * edge2[2],
                       direction[0] * edge2[1] - direction[1] * edge2[0]};
  const double determinant = edge1[0] * p[0] + edge1[1] * p[1] + edge1[2] * p[2];
  if (!(std::abs(determinant) > 1e-12)) return false;
  const double inverse = 1.0 / determinant;
  const double offset[3] = {origin[0] - triangle[0], origin[1] - triangle[1], origin[2] - triangle[2]};
  const double u = (offset[0] * p[0] + offset[1] * p[1] + offset[2] * p[2]) * inverse;
  if (u < -k_seam || u > 1.0 + k_seam) return false;
  const double q[3] = {offset[1] * edge1[2] - offset[2] * edge1[1], offset[2] * edge1[0] - offset[0] * edge1[2],
                       offset[0] * edge1[1] - offset[1] * edge1[0]};
  const double v = (direction[0] * q[0] + direction[1] * q[1] + direction[2] * q[2]) * inverse;
  if (v < -k_seam || u + v > 1.0 + k_seam) return false;
  const double distance = (edge2[0] * q[0] + edge2[1] * q[1] + edge2[2] * q[2]) * inverse;
  return distance > 0.0 && distance <= max_distance;
}

// Constraint: the box is padded so a ray along a flat wall's own plane is never rejected here, only triangles the exact test would also miss.
bool ray_reaches_box(const double origin[3], const double direction[3], const std::array<float, 6>& box, double max_distance) {
  constexpr double k_pad = 1e-4;
  double near_t = 0.0;
  double far_t = max_distance;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const double low = static_cast<double>(box[axis]) - k_pad;
    const double high = static_cast<double>(box[3 + axis]) + k_pad;
    if (std::abs(direction[axis]) < 1e-12) {
      if (origin[axis] < low || origin[axis] > high) return false;
      continue;
    }
    double t0 = (low - origin[axis]) / direction[axis];
    double t1 = (high - origin[axis]) / direction[axis];
    if (t0 > t1) std::swap(t0, t1);
    near_t = std::max(near_t, t0);
    far_t = std::min(far_t, t1);
    if (near_t > far_t) return false;
  }
  return true;
}

}  // namespace

std::optional<std::array<float, 6>> uvd_positions_box(std::span<const float> xyz) {
  if (xyz.empty() || xyz.size() % 3 != 0) {
    return std::nullopt;
  }
  std::array<float, 6> box{xyz[0], xyz[1], xyz[2], xyz[0], xyz[1], xyz[2]};
  for (std::size_t i = 0; i < xyz.size(); i += 3) {
    for (std::size_t a = 0; a < 3; ++a) {
      const float v = xyz[i + a];
      if (!std::isfinite(v)) {
        return std::nullopt;
      }
      box[a] = std::min(box[a], v);
      box[3 + a] = std::max(box[3 + a], v);
    }
  }
  return box;
}

float uvd_tile_lod(const std::array<float, 6>& box, float lod_base) {
  const double volume = (static_cast<double>(box[3]) - static_cast<double>(box[0])) * (static_cast<double>(box[4]) - static_cast<double>(box[1])) *
                        (static_cast<double>(box[5]) - static_cast<double>(box[2]));
  return static_cast<float>(std::cbrt(volume) / static_cast<double>(lod_base));
}

float uvd_default_expand(const std::array<float, 6>& box) {
  const double longest = std::max({static_cast<double>(box[3]) - static_cast<double>(box[0]), static_cast<double>(box[4]) - static_cast<double>(box[1]),
                                   static_cast<double>(box[5]) - static_cast<double>(box[2])});
  std::uint32_t expand = k_expand_min;
  while (expand < k_expand_max && static_cast<double>(expand) * 2.0 <= longest / 64.0) {
    expand *= 2;
  }
  return static_cast<float>(expand);
}

std::array<float, 6> uvd_build_world_box(const UvdBuildInput& input) {
  std::optional<std::array<float, 6>> world = input.cell_box;
  if (world) {
    require_box(*world, "cell box");
  }
  for (std::size_t i = 0; i < input.objects.size(); ++i) {
    const std::array<float, 6>& box = input.objects[i].box;
    require_box(box, "object " + std::to_string(i) + " box");
    if (!world) {
      world = box;
      continue;
    }
    for (std::size_t a = 0; a < 3; ++a) {
      (*world)[a] = std::min((*world)[a], box[a]);
      (*world)[3 + a] = std::max((*world)[3 + a], box[3 + a]);
    }
  }
  if (!world) {
    fail("build: no cell box and no objects");
  }
  for (std::size_t a = 0; a < 3; ++a) {
    if (!((*world)[3 + a] > (*world)[a])) {
      (*world)[3 + a] = (*world)[a] + k_uvd_lod_base;
    }
  }
  if (!finite_box(*world)) {
    fail("build: world box is not finite");
  }
  return *world;
}

std::vector<UvdBuildObject> uvd_cull_occluded(const UvdBuildInput& input) {
  if (input.occluders.empty()) return input.objects;
  static constexpr double k_directions[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  // Constraint: nothing outside the occluder set's own extent can block, so this rejects an open cell without scanning it.
  std::array<float, 6> bounds{};
  bool bounded = false;
  std::vector<std::array<float, 6>> triangle_boxes;
  triangle_boxes.reserve(input.occluders.size());
  for (const std::array<float, 9>& triangle : input.occluders) {
    std::array<float, 6> box{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const float low = std::min({triangle[axis], triangle[3 + axis], triangle[6 + axis]});
      const float high = std::max({triangle[axis], triangle[3 + axis], triangle[6 + axis]});
      box[axis] = low;
      box[3 + axis] = high;
      bounds[axis] = bounded ? std::min(bounds[axis], low) : low;
      bounds[3 + axis] = bounded ? std::max(bounds[3 + axis], high) : high;
    }
    triangle_boxes.push_back(box);
    bounded = true;
  }
  std::vector<UvdBuildObject> visible;
  visible.reserve(input.objects.size());
  for (const UvdBuildObject& object : input.objects) {
    // Constraint: one open side leaves the object listed, so a missing or broken wall can never hide geometry behind it.
    bool enclosed = !input.occluders.empty();
    // Constraint: origin 0 is the centre and 1..8 are the box corners, so the shell must cover the whole box and not only its centre line.
    double origins[9][3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
      origins[0][axis] = (static_cast<double>(object.box[axis]) + static_cast<double>(object.box[3 + axis])) * 0.5;
    }
    for (std::size_t corner = 0; corner < 8; ++corner) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        origins[1 + corner][axis] = static_cast<double>(object.box[((corner >> axis) & 1u) != 0 ? 3 + axis : axis]);
      }
    }
    for (std::size_t direction = 0; direction < 6 && enclosed; ++direction) {
      const std::size_t axis = direction / 2;
      const bool positive = (direction % 2) == 0;
      // Constraint: a centre outside the occluder set's extent on this axis cannot be blocked along it.
      const bool within_extent = positive ? origins[0][axis] < static_cast<double>(bounds[3 + axis])
                                          : origins[0][axis] > static_cast<double>(bounds[axis]);
      if (!within_extent) {
        enclosed = false;
        break;
      }
      for (std::size_t from = 0; from < 9 && enclosed; ++from) {
        enclosed = false;
        for (std::size_t t = 0; t < input.occluders.size(); ++t) {
          if (!ray_reaches_box(origins[from], k_directions[direction], triangle_boxes[t], k_max_enclosure)) continue;
          if (triangle_touches_box(input.occluders[t], object.box)) continue;
          if (triangle_blocks_ray(input.occluders[t], origins[from], k_directions[direction], k_max_enclosure)) {
            enclosed = true;
            break;
          }
        }
      }
    }
    if (!enclosed) {
      visible.push_back(object);
    }
  }
  return visible;
}

UvdTileModel build_conservative_uvd_model(const UvdBuildInput& input) {
  UvdBuildInput visible = input;
  visible.objects = uvd_cull_occluded(input);
  const std::array<float, 6> world = uvd_build_world_box(visible);
  std::unordered_set<std::uint32_t> ids;
  for (const UvdBuildObject& object : visible.objects) {
    if (!ids.insert(object.id).second) {
      fail("build: object id " + hex(object.id) + " is repeated");
    }
  }
  if (visible.objects.size() >= (std::size_t{1} << 31)) {
    fail("build: too many objects");
  }
  UvdBodies bodies;
  bodies.slots.push_back(open_tile(world, static_cast<std::uint32_t>(visible.objects.size()), visible.expand.value_or(uvd_default_expand(world))));
  const UvdHeader header = open_header(visible, world, uvd_tile_lod(world, k_uvd_lod_base));
  return encode_uvd_header(header, encode_uvd_bodies(bodies, UvdTileModel{}));
}

UvdBytes write_uvd_tome_tiled(const UvdBuildInput& input) {
  UvdBytes bytes = write_uvd_tiles(build_conservative_uvd_model(input));
  const std::vector<std::string> issues = validate_uvd_tome_tiled(bytes);
  if (!issues.empty()) {
    fail("build: the written tome is invalid: " + issues.front());
  }
  return bytes;
}

std::filesystem::path uvd_tome_path(const std::filesystem::path& data_dir, std::string_view plugin_folder,
                                    std::uint32_t cell_low24) {
  char name[16]{};
  std::snprintf(name, sizeof(name), "%08X.uvd", cell_low24 & 0xFFFFFFu);
  return data_dir / "Vis" / std::string(plugin_folder) / name;
}

std::filesystem::path write_visi_tome(const std::filesystem::path& data_dir, std::string_view plugin_folder,
                                      std::uint32_t cell_low24, std::span<const std::byte> tome) {
  const std::vector<std::string> issues = validate_uvd_tome_tiled(tome);
  if (!issues.empty()) {
    throw parse_error(0, "the generated tome is invalid: " + issues.front());
  }
  const std::filesystem::path path = uvd_tome_path(data_dir, plugin_folder, cell_low24);
  std::error_code status;
  std::filesystem::create_directories(path.parent_path(), status);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw parse_error(0, "the generated tome could not be opened for writing");
  }
  out.write(reinterpret_cast<const char*>(tome.data()), static_cast<std::streamsize>(tome.size()));
  out.close();
  if (!out) {
    throw parse_error(0, "the generated tome could not be written");
  }
  return path;
}

std::vector<std::string> validate_uvd_tome_tiled(std::span<const std::byte> bytes) {
  std::vector<std::string> issues = validate_uvd(bytes);
  if (!issues.empty()) {
    return issues;
  }
  try {
    for (const UvdTileViolation& v : validate_uvd_tiles(parse_uvd_tiles(bytes))) {
      issues.push_back(v.claim + ": " + std::to_string(v.failed) + "/" + std::to_string(v.of));
    }
  } catch (const UvdTileLayoutError& e) {
    issues.push_back(e.reason());
  }
  return issues;
}

}  // namespace fo4::previs
