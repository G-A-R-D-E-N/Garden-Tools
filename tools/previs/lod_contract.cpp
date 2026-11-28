// Constraint: this hand-built harness runs the real writer over shipped objects; PREVIS.md holds its build command.

#include "fo4/previs/csg.hpp"
#include "fo4/previs/lod_levels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr double k_ratio_tolerance = 0.10;
constexpr std::size_t k_blob_header = 52;
int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("  %-58s %s\n", what.c_str(), ok ? "ok" : "FAILED");
  if (!ok) ++failures;
}

// Constraint: the shipped vertex format stores position as three f16 words, so the driver decodes them.
float half_to_float(std::uint16_t half) {
  const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16U;
  std::uint32_t exponent = (half >> 10U) & 0x1Fu;
  std::uint32_t mantissa = half & 0x3FFu;
  std::uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa != 0) {
      std::uint32_t shift = 127U - 15U + 1U;
      while ((mantissa & 0x400u) == 0) {
        mantissa <<= 1U;
        --shift;
      }
      mantissa &= 0x3FFu;
      bits = sign | (shift << 23U) | (mantissa << 13U);
    } else {
      bits = sign;
    }
  } else if (exponent == 31) {
    bits = sign | 0x7F800000u | (mantissa << 13U);
  } else {
    bits = sign | ((exponent + 127U - 15U) << 23U) | (mantissa << 13U);
  }
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> raw;
  raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) out[i] = static_cast<std::byte>(raw[i]);
  return out;
}

std::uint32_t u32(const std::vector<std::byte>& bytes, std::size_t at) {
  return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}

std::uint16_t u16(const std::byte* at) {
  return static_cast<std::uint16_t>(static_cast<std::uint8_t>(at[0]) |
                                    (static_cast<std::uint16_t>(static_cast<std::uint8_t>(at[1])) << 8U));
}

struct ShippedObject {
  std::string name;
  std::size_t stride{0};
  std::size_t vertex_count{0};
  std::uint32_t vanilla_triangles[3]{0, 0, 0};
  std::uint32_t vanilla_vertices[3]{0, 0, 0};
  std::vector<std::byte> vertex_bytes;
  std::vector<std::uint16_t> level0;
  std::vector<float> positions;
};

bool load(const std::filesystem::path& path, ShippedObject& object) {
  const auto blob = read_file(path);
  if (blob.size() < k_blob_header || std::memcmp(blob.data(), "LODC", 4) != 0) return false;
  object.name = path.filename().string();
  object.stride = u32(blob, 4);
  object.vertex_count = u32(blob, 8);
  for (std::size_t level = 0; level < 3; ++level) {
    object.vanilla_triangles[level] = u32(blob, 16 + 4 * level);
    object.vanilla_vertices[level] = u32(blob, 40 + 4 * level);
  }
  const auto vertex_begin = blob.begin() + static_cast<std::ptrdiff_t>(k_blob_header);
  object.vertex_bytes.assign(vertex_begin, vertex_begin + static_cast<std::ptrdiff_t>(object.vertex_count * object.stride));
  const auto index_begin = vertex_begin + static_cast<std::ptrdiff_t>(object.vertex_count * object.stride);
  object.level0.resize(static_cast<std::size_t>(object.vanilla_triangles[0]) * 3u);
  for (std::size_t word = 0; word < object.level0.size(); ++word) object.level0[word] = u16(&index_begin[2 * word]);
  object.positions.resize(object.vertex_count * 3u);
  for (std::size_t vertex = 0; vertex < object.vertex_count; ++vertex) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      object.positions[3 * vertex + axis] = half_to_float(u16(&object.vertex_bytes[vertex * object.stride + 2 * axis]));
    }
  }
  return true;
}

struct Ratios {
  double level1{0.0};
  double level2{0.0};
};

Ratios shipped_ratios(const ShippedObject& object) {
  const double base = static_cast<double>(object.vanilla_triangles[0]);
  return {static_cast<double>(object.vanilla_triangles[1]) / base,
          static_cast<double>(object.vanilla_triangles[2]) / base};
}

Ratios welded_ratios(const fo4::previs::LodLevels& levels) {
  const double base = static_cast<double>(levels[0].triangles.size() / 3u);
  return {static_cast<double>(levels[1].triangles.size() / 3u) / base,
          static_cast<double>(levels[2].triangles.size() / 3u) / base};
}

// Constraint: a welded level indexes its own kept list and level 0 indexes the shipped list, so both stay inside their block.
bool blocks_hold(std::uint32_t counts[3], const fo4::previs::LodLevels& levels) {
  bool inside = true;
  for (std::size_t level = 0; level < 3; ++level) {
    counts[level] = static_cast<std::uint32_t>(levels[level].kept_vertices.size());
    for (const auto index : levels[level].triangles) {
      if (index >= counts[level]) inside = false;
    }
  }
  return inside;
}

// Constraint: the packed lists are what a CSG row addresses, so each must stay inside its own shifted vertex block.
bool packed_blocks_hold(const fo4::previs::PackedLodLevels& packed) {
  std::uint32_t start = 0;
  for (std::size_t level = 0; level < fo4::previs::k_previs_lod_levels; ++level) {
    const std::vector<std::uint16_t>& list = level == 0 ? packed.indices : packed.higher_lod_indices[level - 1];
    for (const auto index : list) {
      if (index < start || index >= start + packed.level_vertex_count[level]) return false;
    }
    start += packed.level_vertex_count[level];
  }
  return true;
}

// Constraint: a weld can only remove triangles, so objects whose shipped level is larger cannot be compared to one.
bool shipped_reduces(const ShippedObject& object) {
  const auto ratios = shipped_ratios(object);
  return ratios.level1 <= 1.0 && ratios.level2 <= 1.0;
}

void print_row(const ShippedObject& object, const Ratios& ours, const Ratios& theirs) {
  const double d1 = std::fabs(ours.level1 - theirs.level1);
  const double d2 = std::fabs(ours.level2 - theirs.level2);
  std::printf("%-18s %5zu v %5u t  welded %.3f/%.3f  vanilla %.3f/%.3f  |d| %.3f/%.3f  %s\n", object.name.c_str(),
              object.vertex_count, object.vanilla_triangles[0], ours.level1, ours.level2, theirs.level1, theirs.level2,
              d1, d2, (d1 <= k_ratio_tolerance && d2 <= k_ratio_tolerance) ? "within" : "OUTSIDE");
}

std::vector<ShippedObject> comparable_only(const std::vector<ShippedObject>& all) {
  std::vector<ShippedObject> objects;
  for (const auto& object : all) {
    if (shipped_reduces(object)) objects.push_back(object);
  }
  return objects;
}

constexpr double k_pinned_tolerance = 0.10;

// Constraint: a pinned pass reports the cell each level solved to, so a carried ratio is traced back to a grid.
struct PinnedRow {
  std::string name;
  std::size_t vertex_count{0};
  Ratios vanilla;
  Ratios achieved;
  double level1_factor{0.0};
  double level2_factor{0.0};
};

// Constraint: a fraction outside (0,1) names no reduction, so only those objects carry a pinned pair.
bool pinned_comparable(const ShippedObject& object) {
  const auto ratios = shipped_ratios(object);
  return ratios.level1 > 0.0 && ratios.level1 < 1.0 && ratios.level2 > 0.0 && ratios.level2 < 1.0;
}

PinnedRow pinned_row(const ShippedObject& object) {
  PinnedRow row;
  row.name = object.name;
  row.vertex_count = object.vertex_count;
  row.vanilla = shipped_ratios(object);
  const fo4::previs::LodLevelTarget target{static_cast<float>(row.vanilla.level1),
                                           static_cast<float>(row.vanilla.level2)};
  const auto levels = fo4::previs::reduce_mesh_levels_to_target(object.positions, object.level0, target);
  row.achieved = welded_ratios(levels);
  const auto edge = fo4::previs::mean_edge_length(object.positions, object.level0);
  const auto cells = fo4::previs::solve_lod_weld_cells(object.positions, object.level0, target);
  if (edge > 0.0f) {
    row.level1_factor = static_cast<double>(cells.level1) / static_cast<double>(edge);
    row.level2_factor = static_cast<double>(cells.level2) / static_cast<double>(edge);
  }
  return row;
}

std::vector<PinnedRow> pinned_pass(const std::vector<ShippedObject>& all) {
  std::vector<PinnedRow> rows;
  std::printf("\nlevels pinned to each object's own shipped ratios, with the grid they solved to:\n");
  for (const auto& object : all) {
    if (!pinned_comparable(object)) continue;
    const auto row = pinned_row(object);
    const double d1 = std::fabs(row.achieved.level1 - row.vanilla.level1);
    const double d2 = std::fabs(row.achieved.level2 - row.vanilla.level2);
    std::printf("%-18s %5zu v  vanilla %.3f/%.3f  pinned %.3f/%.3f  |d| %.3f/%.3f  grid %.2f/%.2f x edge  %s\n",
                row.name.c_str(), row.vertex_count, row.vanilla.level1, row.vanilla.level2, row.achieved.level1,
                row.achieved.level2, d1, d2, row.level1_factor, row.level2_factor,
                (d1 <= k_pinned_tolerance && d2 <= k_pinned_tolerance) ? "within" : "OUTSIDE");
    rows.push_back(row);
  }
  int within_five = 0;
  int outside = 0;
  double worst = 0.0;
  for (const auto& row : rows) {
    const double d1 = std::fabs(row.achieved.level1 - row.vanilla.level1);
    const double d2 = std::fabs(row.achieved.level2 - row.vanilla.level2);
    worst = std::max({worst, d1, d2});
    if (d1 <= 0.05 && d2 <= 0.05) ++within_five;
    if (d1 > k_pinned_tolerance || d2 > k_pinned_tolerance) ++outside;
  }
  std::printf("pinned objects %zu, within 0.05 %d, outside %.2f %d, worst |d| %.3f\n", rows.size(), within_five,
              k_pinned_tolerance, outside, worst);
  // Constraint: the sample carries five vertex bands, so a pinned pass over fewer than four objects proves nothing.
  if (rows.size() < 4) {
    std::printf("fewer than four objects carry a pinned pair\n");
    ++failures;
  }
  if (outside > 0) {
    std::printf("%d objects miss the pinned tolerance\n", outside);
    ++failures;
  }
  return rows;
}

void write_report(const std::filesystem::path& path, const std::vector<ShippedObject>& all,
                  const std::vector<PinnedRow>& pinned) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.good()) {
    std::printf("could not write %s\n", path.string().c_str());
    ++failures;
    return;
  }
  char line[512];
  std::snprintf(line, sizeof(line), "{\n  \"rule\": {\"level1Factor\": %.2f, \"level2Factor\": %.2f, \"tolerance\": %.2f},\n",
                static_cast<double>(fo4::previs::k_previs_lod1_edge_factor),
                static_cast<double>(fo4::previs::k_previs_lod2_edge_factor), k_ratio_tolerance);
  out << line;
  out << "  \"objects\": [";
  bool first = true;
  for (const auto& object : comparable_only(all)) {
    const auto theirs = shipped_ratios(object);
    const auto levels = fo4::previs::reduce_mesh_levels(object.positions, object.level0);
    const auto ours = welded_ratios(levels);
    const PinnedRow* matched = nullptr;
    for (const auto& candidate : pinned) {
      if (candidate.name == object.name) matched = &candidate;
    }
    std::snprintf(line, sizeof(line),
                  "%s\n    {\"name\": \"%s\", \"vertices\": %zu, \"vanilla\": [%.4f, %.4f], \"rule\": [%.4f, %.4f]",
                  first ? "" : ",", object.name.c_str(), object.vertex_count, theirs.level1, theirs.level2, ours.level1,
                  ours.level2);
    out << line;
    first = false;
    if (matched != nullptr) {
      std::snprintf(line, sizeof(line), ", \"pinned\": [%.4f, %.4f], \"pinnedGrid\": [%.3f, %.3f]",
                    matched->achieved.level1, matched->achieved.level2, matched->level1_factor,
                    matched->level2_factor);
      out << line;
    }
    out << "}";
  }
  out << "\n  ]\n}\n";
  out.close();
  std::printf("report written to %s\n", path.string().c_str());
}

void sweep(const std::vector<ShippedObject>& all) {
  const auto objects = comparable_only(all);
  struct Candidate {
    float factor1{0.0f};
    float factor2{0.0f};
    double mean{0.0};
    double worst{0.0};
    int within{0};
  };
  std::vector<Candidate> candidates;
  for (float factor1 = 0.20f; factor1 <= 3.001f; factor1 += 0.05f) {
    for (float factor2 = 0.10f; factor2 <= 2.001f; factor2 += 0.05f) {
      if (!(factor1 > factor2)) continue;
      Candidate candidate{factor1, factor2, 0.0, 0.0, 0};
      bool usable = true;
      for (const auto& object : objects) {
        const auto edge = fo4::previs::mean_edge_length(object.positions, object.level0);
        const fo4::previs::LodWeldCells cells{factor1 * edge, factor2 * edge};
        const auto levels = fo4::previs::reduce_mesh_levels_with_cells(object.positions, object.level0, cells);
        if (levels[1].triangles.empty() || levels[2].triangles.empty() ||
            levels[1].kept_vertices.size() >= levels[2].kept_vertices.size()) {
          usable = false;
          break;
        }
        const auto ours = welded_ratios(levels);
        const auto theirs = shipped_ratios(object);
        const double d1 = std::fabs(ours.level1 - theirs.level1);
        const double d2 = std::fabs(ours.level2 - theirs.level2);
        candidate.mean += (d1 + d2) / 2.0;
        candidate.worst = std::max({candidate.worst, d1, d2});
        if (d1 <= k_ratio_tolerance && d2 <= k_ratio_tolerance) ++candidate.within;
      }
      if (!usable) continue;
      candidate.mean /= static_cast<double>(objects.size());
      candidates.push_back(candidate);
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    if (a.within != b.within) return a.within > b.within;
    return a.mean < b.mean;
  });
  std::printf("\nsweep over %zu comparable objects, best mean-edge factors (mean and worst |ratio - vanilla|):\n",
              objects.size());
  for (std::size_t i = 0; i < std::min<std::size_t>(5, candidates.size()); ++i) {
    std::printf("  factor1 %.2f  factor2 %.2f   mean %.4f  worst %.4f\n", candidates[i].factor1, candidates[i].factor2,
                candidates[i].mean, candidates[i].worst);
  }

  // Constraint: the ceiling is what any single factor pair can reach per object, so it is measured apart from the fit.
  int within_five = 0;
  int within_ten = 0;
  std::printf("\nper-object ceiling over the whole factor grid (best |ratio - vanilla| any pair can reach):\n");
  for (const auto& object : objects) {
    const auto theirs = shipped_ratios(object);
    const auto edge = fo4::previs::mean_edge_length(object.positions, object.level0);
    double best = 1e9;
    Ratios best_ratios;
    float best1 = 0.0f;
    float best2 = 0.0f;
    for (float factor1 = 0.20f; factor1 <= 3.001f; factor1 += 0.05f) {
      for (float factor2 = 0.10f; factor2 <= 2.001f; factor2 += 0.05f) {
        const fo4::previs::LodWeldCells cells{factor1 * edge, factor2 * edge};
        const auto levels = fo4::previs::reduce_mesh_levels_with_cells(object.positions, object.level0, cells);
        if (levels[1].triangles.empty() || levels[2].triangles.empty()) continue;
        const auto ours = welded_ratios(levels);
        const double worst = std::max(std::fabs(ours.level1 - theirs.level1), std::fabs(ours.level2 - theirs.level2));
        if (worst < best) {
          best = worst;
          best1 = factor1;
          best2 = factor2;
          best_ratios = ours;
        }
      }
    }
    std::printf("  %-18s vanilla %.3f/%.3f  best %.3f/%.3f at %.2f/%.2f  |d| %.3f\n", object.name.c_str(), theirs.level1,
                theirs.level2, best_ratios.level1, best_ratios.level2, best1, best2, best);
    if (best <= 0.05) ++within_five;
    if (best <= 0.10) ++within_ten;
  }
  std::printf("ceiling: %d objects reachable within 0.05, %d within 0.10, of %zu\n", within_five, within_ten,
              objects.size());
}

void sweep_percentiles(const std::vector<ShippedObject>& all) {
  const auto objects = comparable_only(all);
  struct Candidate {
    float percentile1{0.0f};
    float percentile2{0.0f};
    double mean{0.0};
    double worst{0.0};
  };
  std::vector<Candidate> candidates;
  for (float p1 = 0.50f; p1 <= 0.991f; p1 += 0.01f) {
    for (float p2 = 0.50f; p2 <= 0.991f; p2 += 0.01f) {
      Candidate candidate{p1, p2, 0.0, 0.0};
      bool usable = true;
      for (const auto& object : objects) {
        const auto cell1 = fo4::previs::edge_percentile(object.positions, object.level0, p1);
        const auto cell2 = fo4::previs::edge_percentile(object.positions, object.level0, p2);
        const fo4::previs::LodWeldCells cells{cell1, cell2};
        const auto levels = fo4::previs::reduce_mesh_levels_with_cells(object.positions, object.level0, cells);
        if (levels[1].triangles.empty() || levels[2].triangles.empty()) {
          usable = false;
          break;
        }
        const auto ours = welded_ratios(levels);
        const auto theirs = shipped_ratios(object);
        const double d1 = std::fabs(ours.level1 - theirs.level1);
        const double d2 = std::fabs(ours.level2 - theirs.level2);
        candidate.mean += (d1 + d2) / 2.0;
        candidate.worst = std::max({candidate.worst, d1, d2});
      }
      if (!usable) continue;
      candidate.mean /= static_cast<double>(objects.size());
      candidates.push_back(candidate);
    }
  }
  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.mean < b.mean; });
  std::printf("\npercentile sweep over %zu comparable objects (mean and worst |ratio - vanilla|):\n", objects.size());
  for (std::size_t i = 0; i < std::min<std::size_t>(6, candidates.size()); ++i) {
    std::printf("  percentile1 %.2f  percentile2 %.2f   mean %.4f  worst %.4f\n", candidates[i].percentile1,
                candidates[i].percentile2, candidates[i].mean, candidates[i].worst);
  }
  const auto best = candidates.front();
  std::printf("\nper-object at the best percentile pair:\n");
  for (const auto& object : objects) {
    const auto cell1 = fo4::previs::edge_percentile(object.positions, object.level0, best.percentile1);
    const auto cell2 = fo4::previs::edge_percentile(object.positions, object.level0, best.percentile2);
    const auto levels = fo4::previs::reduce_mesh_levels_with_cells(object.positions, object.level0, {cell1, cell2});
    if (levels[1].triangles.empty() || levels[2].triangles.empty()) {
      std::printf("  %-18s level skipped\n", object.name.c_str());
      continue;
    }
    print_row(object, welded_ratios(levels), shipped_ratios(object));
  }
}

bool single_object(const std::filesystem::path& path) {
  ShippedObject object;
  if (!load(path, object)) {
    std::printf("not a dumped object: %s\n", path.string().c_str());
    return false;
  }
  std::printf("shipped object: %zu vertices, stride %zu, vanilla triangles %u/%u/%u, blocks %u/%u/%u\n",
              object.vertex_count, object.stride, object.vanilla_triangles[0], object.vanilla_triangles[1],
              object.vanilla_triangles[2], object.vanilla_vertices[0], object.vanilla_vertices[1],
              object.vanilla_vertices[2]);
  const auto levels = fo4::previs::reduce_mesh_levels(object.positions, object.level0);
  const auto packed = fo4::previs::pack_lod_levels(levels, object.vertex_bytes, object.stride);
  check(packed.has_value() && packed->level_vertex_count[0] > 0 && packed->level_vertex_count[1] > 0 &&
            packed->level_vertex_count[2] > 0 &&
            packed->vertex_count == packed->level_vertex_count[0] + packed->level_vertex_count[1] +
                                        packed->level_vertex_count[2],
        "pack_lod_levels builds the three blocks");
  if (!packed.has_value()) return false;

  std::uint32_t counts[3] = {0, 0, 0};
  check(blocks_hold(counts, levels) && packed_blocks_hold(*packed), "every level index stays inside its own block");
  check(counts[0] == object.vertex_count && packed->level_vertex_count[0] == object.vertex_count,
        "level 0 keeps every shipped vertex");
  check(counts[1] > 0 && counts[2] > 0, "both welded levels survive with geometry");
  check(counts[1] < counts[2] && counts[2] < counts[0], "level 1 is coarser than level 2, both below level 0");
  check(std::memcmp(packed->vertices.data(), object.vertex_bytes.data(), object.vertex_count * object.stride) == 0,
        "level 0 bytes are the shipped bytes unchanged");
  const auto ours = welded_ratios(levels);
  const auto theirs = shipped_ratios(object);
  print_row(object, ours, theirs);
  check(std::fabs(ours.level1 - theirs.level1) <= k_ratio_tolerance &&
            std::fabs(ours.level2 - theirs.level2) <= k_ratio_tolerance,
        "welded ratios sit within tolerance of vanilla's");

  fo4::previs::CsgMeshObject mesh;
  const auto shipped = fo4::previs::csg_shipped_descriptor(object.stride);
  check(shipped.has_value(), "the shipped object's stride is one of the four shipped layouts");
  if (!shipped.has_value()) return false;
  mesh.vertex_desc = *shipped;
  mesh.vertex_count = packed->vertex_count;
  mesh.vertices = packed->vertices;
  mesh.indices = packed->indices;
  mesh.higher_lod_indices = packed->higher_lod_indices;
  mesh.level_vertex_count = packed->level_vertex_count;
  const std::span<const fo4::previs::CsgMeshObject> meshes(&mesh, 1);
  const auto built = fo4::previs::build_csg(meshes);
  const auto& row = built.rows.at(0);
  const std::size_t words0 = levels[0].triangles.size();
  const std::size_t words1 = levels[1].triangles.size();
  check(row.lod_triangle_count[0] == words0 / 3u && row.lod_triangle_count[1] == words1 / 3u &&
            row.lod_triangle_count[2] == levels[2].triangles.size() / 3u && row.lod_index_offset[0] == 0u &&
            row.lod_index_offset[1] == words0 && row.lod_index_offset[2] == words0 + words1,
        "row offsets count the words below each level");
  const auto stream = fo4::previs::inflate_csg_stream(fo4::previs::parse_csg(built.bytes));
  std::vector<std::byte> expected_stream(packed->vertices);
  for (const auto* list : {&packed->indices, &packed->higher_lod_indices[0], &packed->higher_lod_indices[1]}) {
    for (const auto index : *list) {
      expected_stream.push_back(static_cast<std::byte>(index & 0xFFu));
      expected_stream.push_back(static_cast<std::byte>((index >> 8U) & 0xFFu));
    }
  }
  check(stream == expected_stream, "the written CSG stream holds the vertices and all three lists");
  return failures == 0;
}

bool corpus(const std::filesystem::path& directory, bool run_sweep, const std::filesystem::path& report_path) {
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() == ".bin") paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());
  std::vector<ShippedObject> objects;
  for (const auto& path : paths) {
    ShippedObject object;
    if (load(path, object)) objects.push_back(std::move(object));
  }
  if (objects.empty()) {
    std::printf("no objects under %s\n", directory.string().c_str());
    return false;
  }
  if (run_sweep) {
    sweep(objects);
    sweep_percentiles(objects);
  }

  const auto comparable = comparable_only(objects);
  int identical = 0;
  for (const auto& object : comparable) {
    const auto levels = fo4::previs::reduce_mesh_levels(object.positions, object.level0);
    const auto packed = fo4::previs::pack_lod_levels(levels, object.vertex_bytes, object.stride);
    if (packed.has_value() &&
        std::memcmp(packed->vertices.data(), object.vertex_bytes.data(), object.vertex_count * object.stride) == 0) {
      ++identical;
    }
  }
  std::printf("\nlevel 0 bytes identical to the shipped block on %d of %zu objects\n", identical, comparable.size());
  if (static_cast<std::size_t>(identical) != comparable.size()) {
    std::printf("level 0 bytes differ from the shipped block on %zu objects\n",
                comparable.size() - static_cast<std::size_t>(identical));
    ++failures;
  }
  std::printf("\nshipped objects %zu, of which %zu reduce both levels (a weld can only remove triangles)\n",
              objects.size(), comparable.size());
  std::printf("welded ratios against vanilla, tolerance %.2f:\n", k_ratio_tolerance);
  int within = 0;
  int outside = 0;
  for (const auto& object : comparable) {
    const auto levels = fo4::previs::reduce_mesh_levels(object.positions, object.level0);
    std::uint32_t counts[3] = {0, 0, 0};
    const bool inside = blocks_hold(counts, levels);
    const auto ours = welded_ratios(levels);
    const auto theirs = shipped_ratios(object);
    print_row(object, ours, theirs);
    if (std::fabs(ours.level1 - theirs.level1) > k_ratio_tolerance ||
        std::fabs(ours.level2 - theirs.level2) > k_ratio_tolerance) {
      ++outside;
    } else {
      ++within;
    }
    if (!inside || counts[0] != object.vertex_count) {
      std::printf("  blocks broken on %s\n", object.name.c_str());
      ++failures;
    }
  }
  std::printf("objects %zu, within tolerance %d, outside %d\n", comparable.size(), within, outside);
  // Constraint: the acceptance floor is four shipped objects across the bands, so fewer fails the run.
  if (within < 4) {
    std::printf("fewer than four objects land inside the tolerance\n");
    ++failures;
  }
  const auto pinned = pinned_pass(objects);
  if (!report_path.empty()) write_report(report_path, objects, pinned);
  return failures == 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2) {
    std::printf("usage: lod_contract <object.bin|corpus-dir> [--sweep] [--report <file.json>]\n");
    return 2;
  }
  try {
    std::filesystem::path target;
    bool run_sweep = false;
    std::filesystem::path report_path;
    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--sweep") {
        run_sweep = true;
      } else if (argument == "--report" && index + 1 < argc) {
        report_path = argv[++index];
      } else if (target.empty()) {
        target = argument;
      } else {
        std::printf("unknown argument: %s\n", argument.c_str());
        return 2;
      }
    }
    if (target.empty()) {
      std::printf("usage: lod_contract <object.bin|corpus-dir> [--sweep] [--report <file.json>]\n");
      return 2;
    }
    const bool ok = std::filesystem::is_directory(target) ? corpus(target, run_sweep, report_path) : single_object(target);
    std::printf("%s\n", ok ? "contract: all checks passed" : "contract: FAILURES");
    return ok ? 0 : 1;
  } catch (const std::exception& error) {
    std::printf("threw: %s\n", error.what());
    return 1;
  }
}
