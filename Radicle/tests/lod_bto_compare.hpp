#pragma once

#include "fo4/hash.hpp"
#include "fo4/nif/nif.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::lodcompare {

// Invariant: each tier is asserted on its own, so one run names every part of a tile that departs from vanilla.
inline const std::array<std::string, 9> k_tiers{"layout", "blocks",   "shape-header", "segments", "bounds",
                                                "shapes", "geometry", "tile-set",     "self-check"};

inline constexpr double k_log_only_tier_ceiling = 0.05;

inline constexpr bool k_log_only_tier_ceiling_enforced = false;

inline constexpr std::string_view k_ceiling_mode = k_log_only_tier_ceiling_enforced ? " | enforced" : " | log-only";

struct TileComparison {
  std::map<std::string, std::vector<std::string>> diffs;
  // Invariant: triangles that did not match exactly, and how many of them paired through the tolerance.
  std::size_t exact_leftovers{0};
  std::size_t tolerated{0};
  void note(std::string_view tier, std::string text) { diffs[std::string{tier}].push_back(std::move(text)); }
};

inline std::string hex64(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << value;
  return out.str();
}

inline std::string hex8(std::byte value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setfill('0') << std::setw(2) << std::to_integer<unsigned>(value);
  return out.str();
}

inline std::span<const std::byte> block_bytes(const nif::NifFile& file, std::size_t block) {
  const auto& info = file.blocks().at(block);
  if (info.offset > file.bytes().size() || info.size > file.bytes().size() - info.offset) return {};
  return std::span<const std::byte>{file.bytes()}.subspan(info.offset, info.size);
}

inline std::optional<std::uint32_t> u32_in(const nif::NifFile& file, std::size_t block, std::size_t offset) {
  const auto bytes = block_bytes(file, block);
  if (offset > bytes.size() || bytes.size() - offset < 4) return std::nullopt;
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

// Invariant: a file offset is named by the block that holds it.
inline std::string place_of(const nif::NifFile& file, std::size_t offset) {
  const auto& blocks = file.blocks();
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    if (offset >= blocks[index].offset && offset < blocks[index].offset + blocks[index].size) {
      return "block " + std::to_string(index) + " " + blocks[index].type + " +" +
             std::to_string(offset - blocks[index].offset);
    }
  }
  return "header or footer, offset " + std::to_string(offset);
}

// Invariant: the first difference is a differing byte, or the shorter length when one side is a prefix.
inline std::optional<std::size_t> first_difference(std::span<const std::byte> left, std::span<const std::byte> right) {
  const auto common = std::min(left.size(), right.size());
  for (std::size_t at = 0; at < common; ++at) {
    if (left[at] != right[at]) return at;
  }
  if (left.size() != right.size()) return common;
  return std::nullopt;
}

inline std::string difference_text(std::span<const std::byte> ours, std::span<const std::byte> vanilla) {
  const auto at = first_difference(ours, vanilla);
  if (!at) return "identical";
  std::string text = "sizes ours " + std::to_string(ours.size()) + " vanilla " + std::to_string(vanilla.size()) +
                     ", first difference +" + std::to_string(*at);
  if (*at < ours.size() && *at < vanilla.size()) {
    text += ": ours " + hex8(ours[*at]) + " vanilla " + hex8(vanilla[*at]);
  }
  return text;
}

inline std::string floats_text(const std::vector<float>& values) {
  std::ostringstream out;
  out << std::setprecision(9) << '(';
  for (std::size_t index = 0; index < values.size(); ++index) out << (index == 0 ? "" : ", ") << values[index];
  out << ')';
  return out.str();
}

inline std::vector<float> block_floats_in(const nif::NifFile& file, std::size_t block, std::size_t count) {
  const auto bytes = block_bytes(file, block);
  std::vector<float> out;
  for (std::size_t index = 0; index < count && (index + 1) * 4 <= bytes.size(); ++index) {
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + index * 4, sizeof(value));
    out.push_back(value);
  }
  return out;
}

inline std::vector<std::size_t> blocks_of(const nif::NifFile& file, std::string_view type) {
  std::vector<std::size_t> out;
  for (std::size_t index = 0; index < file.blocks().size(); ++index) {
    if (file.blocks()[index].type == type) out.push_back(index);
  }
  return out;
}

// Invariant: vertex data starts 118 bytes into the block plus four per extra-data ref; the data size sits at 114.
inline constexpr std::size_t k_shape_fixed_header = 118;
inline constexpr std::size_t k_shape_data_size_offset = 114;

struct ShapeSpans {
  std::span<const std::byte> header;
  std::span<const std::byte> tail;
};

inline ShapeSpans spans_of_shape(const nif::NifFile& file, std::size_t block) {
  ShapeSpans spans;
  const auto bytes = block_bytes(file, block);
  const auto extras = u32_in(file, block, 4);
  if (!extras) return spans;
  const std::size_t shift = 4 * static_cast<std::size_t>(*extras);
  const auto data_size = u32_in(file, block, k_shape_data_size_offset + shift);
  if (!data_size) return spans;
  const std::size_t header_end = k_shape_fixed_header + shift;
  const std::size_t tail_start = header_end + static_cast<std::size_t>(*data_size);
  if (tail_start > bytes.size()) return spans;
  spans.header = bytes.subspan(0, header_end);
  spans.tail = bytes.subspan(tail_start);
  return spans;
}

inline std::uint32_t toggle_of(const nif::NifFile& file, std::size_t shape_block) {
  const auto extras = u32_in(file, shape_block, 4);
  if (!extras || *extras == 0) return 0;
  const auto extra = u32_in(file, shape_block, 8);
  if (!extra || *extra >= file.blocks().size() || file.blocks()[*extra].type != "NiIntegerExtraData") return 0;
  return u32_in(file, *extra, 4).value_or(0);
}

inline std::uint64_t position_key(const nif::Shape& shape, std::size_t vertex) {
  if ((vertex + 1) * 3 > shape.verts.size()) return 0;
  return fo4::fnv1a64_bytes(std::as_bytes(std::span<const float>{shape.verts.data() + vertex * 3, 3}),
                            fo4::k_fnv64_offset);
}

inline std::uint8_t unit_byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround((value + 1.0f) * 127.5f), 0L, 255L));
}

// Invariant: a vertex frame is the six normal and tangent bytes, zero where the shape has no such channel.
using VertexFrame = std::array<std::uint8_t, 6>;

inline VertexFrame frame_of(const nif::Shape& shape, std::size_t vertex) {
  VertexFrame frame{};
  std::size_t slot = 0;
  for (const auto* channel : {&shape.normals, &shape.tangents}) {
    for (std::size_t axis = 0; axis < 3; ++axis, ++slot) {
      if ((vertex + 1) * 3 <= channel->size()) frame[slot] = unit_byte((*channel)[vertex * 3 + axis]);
    }
  }
  return frame;
}

struct CanonicalTriangle {
  std::array<std::uint64_t, 3> key{};
  std::array<VertexFrame, 3> frame{};
  std::array<std::array<float, 3>, 3> pos{};
  std::size_t shape{0};
  std::size_t index{0};
  std::array<float, 3> at{};
};

// Invariant: the key is the three positions rotated to the smallest first, UVs and frames stay out, see the code note.
inline std::map<std::string, std::vector<CanonicalTriangle>> canonical_pools(const nif::NifFile& file,
                                                                            const std::vector<nif::Shape>& shapes) {
  std::map<std::string, std::vector<CanonicalTriangle>> pools;
  for (std::size_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
    const auto& shape = shapes[shape_index];
    auto& pool = pools[shape.name + "/toggle " + std::to_string(toggle_of(file, shape.block))];
    for (std::size_t corner = 0; corner + 2 < shape.tris.size(); corner += 3) {
      CanonicalTriangle triangle;
      triangle.shape = shape_index;
      triangle.index = corner / 3;
      std::array<std::uint64_t, 3> keys{};
      std::array<VertexFrame, 3> frames{};
      std::array<std::array<float, 3>, 3> poses{};
      for (std::size_t k = 0; k < 3; ++k) {
        keys[k] = position_key(shape, shape.tris[corner + k]);
        frames[k] = frame_of(shape, shape.tris[corner + k]);
        const auto at = static_cast<std::size_t>(shape.tris[corner + k]);
        for (std::size_t axis = 0; axis < 3 && (at + 1) * 3 <= shape.verts.size(); ++axis) {
          poses[k][axis] = shape.verts[at * 3 + axis];
        }
      }
      const auto first = static_cast<std::size_t>(shape.tris[corner]);
      for (std::size_t k = 0; k < 3 && (first + 1) * 3 <= shape.verts.size(); ++k) {
        triangle.at[k] = shape.verts[first * 3 + k];
      }
      std::size_t best = 0;
      for (std::size_t by = 1; by < 3; ++by) {
        const std::array<std::uint64_t, 3> now{keys[by], keys[(by + 1) % 3], keys[(by + 2) % 3]};
        const std::array<std::uint64_t, 3> before{keys[best], keys[(best + 1) % 3], keys[(best + 2) % 3]};
        if (now < before) best = by;
      }
      for (std::size_t k = 0; k < 3; ++k) {
        triangle.key[k] = keys[(best + k) % 3];
        triangle.frame[k] = frames[(best + k) % 3];
        triangle.pos[k] = poses[(best + k) % 3];
      }
      pool.push_back(triangle);
    }
  }
  for (auto& entry : pools) {
    std::stable_sort(entry.second.begin(), entry.second.end(),
                     [](const CanonicalTriangle& left, const CanonicalTriangle& right) { return left.key < right.key; });
  }
  return pools;
}

// Invariant: a bound matches within 1e-3 or one half-precision step of the larger magnitude.
inline bool near_bound(float left, float right) {
  const float step = std::ldexp(std::max(std::fabs(left), std::fabs(right)), -10);
  return std::fabs(left - right) <= std::max(1e-3f, step);
}

// Invariant: triangles that pair only through the byte slack or the leftover pass are counted and budgeted per pool.
struct PairingSlack {
  std::size_t frame{0};
  std::size_t leftover{0};
  float max_position_delta{0.0f};
};

// Invariant: a position matches within 1e-3 or one half-precision step at the larger magnitude, the stored precision.
inline float position_slack(float left, float right) {
  const float larger = std::max(std::fabs(left), std::fabs(right));
  return larger < 1e-3f ? 1e-3f : std::max(1e-3f, std::ldexp(1.0f, std::ilogb(larger) - 10));
}

inline int frame_delta(const CanonicalTriangle& left, const CanonicalTriangle& right, std::size_t by) {
  int worst = 0;
  for (std::size_t corner = 0; corner < 3; ++corner) {
    for (std::size_t slot = 0; slot < 6; ++slot) {
      const int d = static_cast<int>(left.frame[corner][slot]) - static_cast<int>(right.frame[(corner + by) % 3][slot]);
      worst = std::max(worst, std::abs(d));
    }
  }
  return worst;
}

// Invariant: at most one triangle in a hundred of a pool may pair through the slack, so a systematic bias fails.
inline bool slack_over_budget(const PairingSlack& slack, std::size_t triangles) {
  return (slack.frame + slack.leftover) * 100 > triangles;
}

// Invariant: a leftover triangle pairs when every position is within position_slack and the frame bytes within one.
inline bool triangles_near(const CanonicalTriangle& left, const CanonicalTriangle& right, float& delta) {
  for (std::size_t by = 0; by < 3; ++by) {
    float worst = 0.0f;
    bool fits = true;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const float a = left.pos[corner][axis];
        const float b = right.pos[(corner + by) % 3][axis];
        if (std::fabs(a - b) > position_slack(a, b)) fits = false;
        worst = std::max(worst, std::fabs(a - b));
      }
    }
    if (fits && frame_delta(left, right, by) <= 1) {
      delta = worst;
      return true;
    }
  }
  return false;
}

// Invariant: triangles pair by position key, exact frames first, then within one byte, so counts stay exact.
inline void diff_by_key(const std::vector<CanonicalTriangle>& left, const std::vector<CanonicalTriangle>& right,
                        std::vector<CanonicalTriangle>& only_left, std::vector<CanonicalTriangle>& only_right,
                        PairingSlack& slack) {
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < left.size() || j < right.size()) {
    if (j == right.size() || (i < left.size() && left[i].key < right[j].key)) {
      only_left.push_back(left[i++]);
    } else if (i == left.size() || right[j].key < left[i].key) {
      only_right.push_back(right[j++]);
    } else {
      const auto key = left[i].key;
      std::size_t i_end = i;
      std::size_t j_end = j;
      while (i_end < left.size() && left[i_end].key == key) ++i_end;
      while (j_end < right.size() && right[j_end].key == key) ++j_end;
      std::vector<bool> taken(j_end - j, false);
      std::vector<bool> paired(i_end - i, false);
      for (const int allowed : {0, 1}) {
        for (std::size_t at = i; at < i_end; ++at) {
          if (paired[at - i]) continue;
          for (std::size_t k = j; k < j_end; ++k) {
            if (taken[k - j] || frame_delta(left[at], right[k], 0) > allowed) continue;
            taken[k - j] = true;
            paired[at - i] = true;
            slack.frame += static_cast<std::size_t>(allowed);
            break;
          }
        }
      }
      for (std::size_t at = i; at < i_end; ++at) {
        if (!paired[at - i]) only_left.push_back(left[at]);
      }
      for (std::size_t k = j; k < j_end; ++k) {
        if (!taken[k - j]) only_right.push_back(right[k]);
      }
      i = i_end;
      j = j_end;
    }
  }
}

inline void diff_triangles(const std::vector<CanonicalTriangle>& left, const std::vector<CanonicalTriangle>& right,
                           std::vector<CanonicalTriangle>& only_left, std::vector<CanonicalTriangle>& only_right,
                           PairingSlack& slack) {
  std::vector<CanonicalTriangle> rest_left;
  std::vector<CanonicalTriangle> rest_right;
  diff_by_key(left, right, rest_left, rest_right, slack);
  std::vector<bool> taken(rest_right.size(), false);
  for (const auto& triangle : rest_left) {
    bool found = false;
    for (std::size_t k = 0; k < rest_right.size() && !found; ++k) {
      float delta = 0.0f;
      if (!taken[k] && triangles_near(triangle, rest_right[k], delta)) {
        taken[k] = true;
        found = true;
        ++slack.leftover;
        slack.max_position_delta = std::max(slack.max_position_delta, delta);
      }
    }
    if (!found) only_left.push_back(triangle);
  }
  for (std::size_t k = 0; k < rest_right.size(); ++k) {
    if (!taken[k]) only_right.push_back(rest_right[k]);
  }
}

inline std::string triangle_text(const CanonicalTriangle& triangle) {
  return "shape " + std::to_string(triangle.shape) + " triangle " + std::to_string(triangle.index) +
         ", a vertex of the triangle is at (" + std::to_string(triangle.at[0]) + ", " +
         std::to_string(triangle.at[1]) + ", " + std::to_string(triangle.at[2]) + ")";
}

inline bool bounds_near(std::span<const std::byte> left, std::span<const std::byte> right, std::size_t floats) {
  if (left.size() < floats * 4 || right.size() < floats * 4) return false;
  for (std::size_t index = 0; index < floats; ++index) {
    float a = 0.0f;
    float b = 0.0f;
    std::memcpy(&a, left.data() + index * 4, sizeof(a));
    std::memcpy(&b, right.data() + index * 4, sizeof(b));
    if (!near_bound(a, b)) return false;
  }
  return true;
}

inline std::string strings_text(const std::vector<std::string>& items) {
  std::string text;
  for (const auto& item : items) text += " '" + item + "'";
  return text;
}

inline void compare_layout(const nif::NifFile& ours, const nif::NifFile& vanilla, TileComparison& out) {
  if (ours.version() != vanilla.version() || ours.user_version() != vanilla.user_version() ||
      ours.bs_version() != vanilla.bs_version()) {
    out.note("layout", "versions ours " + std::to_string(ours.version()) + "/" + std::to_string(ours.user_version()) +
                           "/" + std::to_string(ours.bs_version()) + " vanilla " + std::to_string(vanilla.version()) +
                           "/" + std::to_string(vanilla.user_version()) + "/" + std::to_string(vanilla.bs_version()));
  }
  if (ours.strings() != vanilla.strings()) {
    out.note("layout", "string tables differ: ours" + strings_text(ours.strings()) + " vanilla" +
                           strings_text(vanilla.strings()));
  }
  if (ours.roots() != vanilla.roots()) out.note("layout", "root block lists differ");
  const auto& left = ours.blocks();
  const auto& right = vanilla.blocks();
  if (left.size() != right.size()) {
    out.note("layout", "block count ours " + std::to_string(left.size()) + " vanilla " + std::to_string(right.size()));
  }
  for (std::size_t index = 0; index < std::min(left.size(), right.size()); ++index) {
    if (left[index].type == right[index].type) continue;
    out.note("layout", "block " + std::to_string(index) + " type ours " + left[index].type + " vanilla " +
                           right[index].type);
    break;
  }
}

inline void compare_blocks(const nif::NifFile& ours, const nif::NifFile& vanilla, TileComparison& out,
                           bool tolerant_bounds) {
  std::vector<std::string> types;
  for (const auto& info : ours.blocks()) types.push_back(info.type);
  for (const auto& info : vanilla.blocks()) types.push_back(info.type);
  std::sort(types.begin(), types.end());
  types.erase(std::unique(types.begin(), types.end()), types.end());
  for (const auto& type : types) {
    const auto left = blocks_of(ours, type);
    const auto right = blocks_of(vanilla, type);
    const bool bounds = type == "BSMultiBoundAABB";
    const bool shape = type == "BSSubIndexTriShape";
    const char* tier = bounds ? "bounds" : (shape ? "shapes" : "blocks");
    if (left.size() != right.size()) {
      out.note(tier, type + " count ours " + std::to_string(left.size()) + " vanilla " + std::to_string(right.size()));
    }
    for (std::size_t nth = 0; nth < std::min(left.size(), right.size()); ++nth) {
      const std::string where = type + " #" + std::to_string(nth) + " (ours block " + std::to_string(left[nth]) +
                                ", vanilla block " + std::to_string(right[nth]) + ")";
      if (shape) {
        const auto mine = spans_of_shape(ours, left[nth]);
        const auto theirs = spans_of_shape(vanilla, right[nth]);
        if (mine.header.empty() || theirs.header.empty()) {
          out.note("shape-header", where + ": header could not be located");
          continue;
        }
        auto mine_header = std::vector<std::byte>(mine.header.begin(), mine.header.end());
        auto theirs_header = std::vector<std::byte>(theirs.header.begin(), theirs.header.end());
        bool sphere_ok = true;
        if (tolerant_bounds) {
          const std::size_t sphere = 72 + 4 * static_cast<std::size_t>(u32_in(ours, left[nth], 4).value_or(0));
          const std::size_t theirs_sphere =
              72 + 4 * static_cast<std::size_t>(u32_in(vanilla, right[nth], 4).value_or(0));
          if (sphere + 16 <= mine_header.size() && theirs_sphere + 16 <= theirs_header.size()) {
            sphere_ok = bounds_near(std::span<const std::byte>{mine_header}.subspan(sphere),
                                    std::span<const std::byte>{theirs_header}.subspan(theirs_sphere), 4);
            std::fill_n(mine_header.begin() + static_cast<std::ptrdiff_t>(sphere), 16, std::byte{0});
            std::fill_n(theirs_header.begin() + static_cast<std::ptrdiff_t>(theirs_sphere), 16, std::byte{0});
          }
        }
        if (!sphere_ok) out.note("shape-header", where + ": bound sphere outside tolerance");
        if (!std::equal(mine_header.begin(), mine_header.end(), theirs_header.begin(), theirs_header.end())) {
          out.note("shape-header", where + ": " + difference_text(mine_header, theirs_header));
        }
        if (!std::equal(mine.tail.begin(), mine.tail.end(), theirs.tail.begin(), theirs.tail.end())) {
          out.note("segments", where + " segment tail: " + difference_text(mine.tail, theirs.tail));
        }
        continue;
      }
      const auto mine = block_bytes(ours, left[nth]);
      const auto theirs = block_bytes(vanilla, right[nth]);
      if (std::equal(mine.begin(), mine.end(), theirs.begin(), theirs.end())) continue;
      if (bounds && tolerant_bounds && mine.size() == theirs.size() && bounds_near(mine, theirs, 6)) continue;
      std::string text = where + ": " + difference_text(mine, theirs);
      if (bounds) {
        text += "; ours " + floats_text(block_floats_in(ours, left[nth], 6)) + " vanilla " +
                floats_text(block_floats_in(vanilla, right[nth], 6));
      }
      out.note(tier, text);
    }
  }
}

inline void compare_shapes(const nif::NifFile& ours, const nif::NifFile& vanilla, TileComparison& out) {
  const auto mine = nif::read_shapes(ours);
  const auto theirs = nif::read_shapes(vanilla);
  if (mine.size() != theirs.size()) {
    out.note("shapes", "shape count ours " + std::to_string(mine.size()) + " vanilla " + std::to_string(theirs.size()));
  }
  for (std::size_t index = 0; index < std::min(mine.size(), theirs.size()); ++index) {
    const auto& left = mine[index];
    const auto& right = theirs[index];
    const std::string where = "shape " + std::to_string(index);
    if (left.name != right.name) out.note("shapes", where + " name ours '" + left.name + "' vanilla '" + right.name + "'");
    if (left.material != right.material) {
      out.note("shapes", where + " material ours '" + left.material + "' vanilla '" + right.material + "'");
    }
    if (left.textures != right.textures) {
      out.note("shapes", where + " textures differ: ours" + strings_text(left.textures) + " vanilla" +
                             strings_text(right.textures));
    }
    if (left.verts.size() != right.verts.size() || left.tris.size() != right.tris.size()) {
      out.note("shapes", where + " vertices ours " + std::to_string(left.verts.size() / 3) + " vanilla " +
                             std::to_string(right.verts.size() / 3) + ", triangles ours " +
                             std::to_string(left.tris.size() / 3) + " vanilla " + std::to_string(right.tris.size() / 3));
    }
  }
  const auto left_pools = canonical_pools(ours, mine);
  const auto right_pools = canonical_pools(vanilla, theirs);
  std::vector<std::string> names;
  for (const auto& entry : left_pools) names.push_back(entry.first);
  for (const auto& entry : right_pools) names.push_back(entry.first);
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  const std::vector<CanonicalTriangle> none;
  for (const auto& name : names) {
    const auto left = left_pools.find(name);
    const auto right = right_pools.find(name);
    const auto& left_list = left == left_pools.end() ? none : left->second;
    const auto& right_list = right == right_pools.end() ? none : right->second;
    std::vector<CanonicalTriangle> only_ours;
    std::vector<CanonicalTriangle> only_vanilla;
    PairingSlack slack;
    diff_triangles(left_list, right_list, only_ours, only_vanilla, slack);
    const auto slackers = slack.frame + slack.leftover;
    out.tolerated += slackers;
    out.exact_leftovers += slackers + only_ours.size();
    if (slackers != 0) {
      std::cout << "geometry-slack pool '" << name << "' triangles " << left_list.size() << " byte-slack " << slack.frame
                << " leftover " << slack.leftover << " max-position-delta " << slack.max_position_delta << "\n";
    }
    if (slack_over_budget(slack, left_list.size())) {
      out.note("geometry", "pool '" + name + "' pairs " + std::to_string(slackers) + " of " +
                               std::to_string(left_list.size()) + " triangles only through the tolerance");
    }
    if (only_ours.empty() && only_vanilla.empty()) continue;
    std::string text = "pool '" + name + "' triangles ours " + std::to_string(left_list.size()) + " vanilla " +
                       std::to_string(right_list.size()) + ", only in ours " + std::to_string(only_ours.size()) +
                       ", only in vanilla " + std::to_string(only_vanilla.size());
    if (!only_ours.empty()) text += "; first only in ours: " + triangle_text(only_ours.front());
    if (!only_vanilla.empty()) text += "; first only in vanilla: " + triangle_text(only_vanilla.front());
    out.note("geometry", text);
  }
}

inline std::uint64_t channel_key(const nif::Shape& shape, std::size_t vertex, std::size_t channels) {
  std::uint64_t hash = fo4::k_fnv64_offset;
  const std::array<const std::vector<float>*, 3> lists{&shape.verts, &shape.normals, &shape.tangents};
  for (std::size_t index = 0; index < channels; ++index) {
    const auto& channel = *lists[index];
    if (channel.size() != shape.verts.size() || (vertex + 1) * 3 > channel.size()) continue;
    hash = fo4::fnv1a64_bytes(std::as_bytes(std::span<const float>{channel.data() + vertex * 3, 3}), hash);
  }
  return hash;
}

inline std::string vertex_text(const nif::Shape& shape, std::size_t vertex) {
  std::ostringstream out;
  out << std::setprecision(9);
  const auto put = [&](const char* name, const std::vector<float>& values, std::size_t width) {
    out << ' ' << name << '(';
    for (std::size_t k = 0; k < width; ++k) {
      const auto at = vertex * width + k;
      out << (k == 0 ? "" : ", ");
      if (at < values.size()) {
        out << values[at];
      } else {
        out << '?';
      }
    }
    out << ')';
  };
  put("pos", shape.verts, 3);
  put("normal", shape.normals, 3);
  put("tangent", shape.tangents, 3);
  put("uv", shape.uvs, 2);
  return out.str();
}

inline std::map<std::string, std::vector<const nif::Shape*>> shape_pools(const nif::NifFile& file,
                                                                         const std::vector<nif::Shape>& shapes) {
  std::map<std::string, std::vector<const nif::Shape*>> pools;
  for (const auto& shape : shapes) {
    pools[shape.name + "/toggle " + std::to_string(toggle_of(file, shape.block))].push_back(&shape);
  }
  return pools;
}

// Invariant: log-only; it names which vertex channel keeps a generated vertex from matching a vanilla one.
inline void log_vertex_agreement(const std::string& label, const nif::NifFile& ours, const nif::NifFile& vanilla) {
  const auto mine = nif::read_shapes(ours);
  const auto theirs = nif::read_shapes(vanilla);
  const auto print_table = [&](const char* side, const nif::NifFile& file, const std::vector<nif::Shape>& shapes) {
    std::ostringstream table;
    for (const auto& shape : shapes) {
      table << ' ' << shape.name << '/' << toggle_of(file, shape.block) << ':' << shape.verts.size() / 3 << "v/"
            << shape.tris.size() / 3 << 't';
    }
    std::cout << "shape-table " << label << ' ' << side << table.str() << std::endl;
  };
  print_table("ours", ours, mine);
  print_table("vanilla", vanilla, theirs);
  const auto left = shape_pools(ours, mine);
  const auto right = shape_pools(vanilla, theirs);
  for (const auto& [name, ours_shapes] : left) {
    const auto found = right.find(name);
    if (found == right.end()) {
      std::cout << "vertex-agreement " << label << " pool '" << name << "' has no vanilla counterpart" << std::endl;
      continue;
    }
    std::array<std::set<std::uint64_t>, 3> known;
    std::size_t vanilla_total = 0;
    for (const auto* shape : found->second) {
      for (std::size_t vertex = 0; vertex < shape->verts.size() / 3; ++vertex) {
        ++vanilla_total;
        for (std::size_t channels = 0; channels < 3; ++channels) {
          known[channels].insert(channel_key(*shape, vertex, channels + 1));
        }
      }
    }
    std::array<std::size_t, 3> hits{};
    std::size_t total = 0;
    std::vector<std::pair<const nif::Shape*, std::size_t>> misses;
    for (const auto* shape : ours_shapes) {
      for (std::size_t vertex = 0; vertex < shape->verts.size() / 3; ++vertex) {
        ++total;
        for (std::size_t channels = 0; channels < 3; ++channels) {
          if (known[channels].count(channel_key(*shape, vertex, channels + 1)) != 0) ++hits[channels];
        }
        if (known[2].count(channel_key(*shape, vertex, 3)) == 0 && misses.size() < 3) misses.emplace_back(shape, vertex);
      }
    }
    std::cout << "vertex-agreement " << label << " pool '" << name << "' ours " << total << " vanilla "
              << vanilla_total << " position " << hits[0] << " position+normal " << hits[1] << " full " << hits[2]
              << std::endl;
    for (const auto& [shape, vertex] : misses) {
      const nif::Shape* nearest_shape = nullptr;
      std::size_t nearest_vertex = 0;
      double nearest = 1e300;
      for (const auto* candidate : found->second) {
        for (std::size_t other = 0; other < candidate->verts.size() / 3; ++other) {
          double squared = 0.0;
          for (std::size_t axis = 0; axis < 3; ++axis) {
            const double delta = static_cast<double>(shape->verts[vertex * 3 + axis]) -
                                 static_cast<double>(candidate->verts[other * 3 + axis]);
            squared += delta * delta;
          }
          if (squared < nearest) {
            nearest = squared;
            nearest_shape = candidate;
            nearest_vertex = other;
          }
        }
      }
      std::cout << "  sample ours" << vertex_text(*shape, vertex) << std::endl;
      if (nearest_shape != nullptr) {
        std::cout << "  nearest vanilla (distance " << std::sqrt(nearest) << ")"
                  << vertex_text(*nearest_shape, nearest_vertex) << std::endl;
      }
    }
  }
}

inline TileComparison compare_object_tile(const nif::NifFile& ours, const nif::NifFile& vanilla,
                                          bool tolerant_bounds = false) {
  TileComparison out;
  compare_layout(ours, vanilla, out);
  compare_blocks(ours, vanilla, out, tolerant_bounds);
  compare_shapes(ours, vanilla, out);
  return out;
}

inline void check_generated_tile(const nif::NifFile& file, bool baked, TileComparison& out) {
  const auto shapes = nif::read_shapes(file);
  if (shapes.empty()) out.note("self-check", "no shape");
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    const auto& shape = shapes[index];
    const std::string where = "shape " + std::to_string(index);
    if (shape.name != "obj" && shape.name != "obj-at") out.note("self-check", where + " name '" + shape.name + "'");
    if (shape.verts.empty() || shape.tris.empty()) out.note("self-check", where + " has no geometry");
    if (shape.textures.empty() || shape.textures.front().empty()) {
      out.note("self-check", where + " names no diffuse texture");
    }
    for (const float value : shape.verts) {
      if (std::isfinite(value)) continue;
      out.note("self-check", where + " has a non-finite position");
      break;
    }
    if (!baked) continue;
    for (const float value : shape.uvs) {
      if (value >= -1.0f / 512 && value <= 1.0f + 1.0f / 512) continue;
      out.note("self-check", where + " has a UV outside the atlas: " + std::to_string(value));
      break;
    }
  }
}

struct Sweep {
  std::size_t tiles{0};
  std::map<std::string, std::size_t> tiles_with_diff;
  std::map<std::string, std::vector<std::string>> first;
  std::size_t cap{40};
  std::size_t probes{0};

  std::size_t compared() const { return tiles - probes; }

  void add_probe(const std::string& label, const TileComparison& tile) {
    add(label, tile);
    ++probes;
  }

  void add(const std::string& label, const TileComparison& tile) {
    ++tiles;
    for (const auto& [tier, messages] : tile.diffs) {
      if (messages.empty()) continue;
      ++tiles_with_diff[tier];
      for (const auto& message : messages) {
        if (first[tier].size() < cap) first[tier].push_back(label + ": " + message);
      }
    }
  }

  void expect_clean(std::string_view what, const std::vector<std::string>& strict = {}) const {
    for (const auto& tier : k_tiers) {
      const bool hard = strict.empty() || std::find(strict.begin(), strict.end(), tier) != strict.end();
      const auto found = tiles_with_diff.find(tier);
      const std::size_t count = found == tiles_with_diff.end() ? 0 : found->second;
      std::string detail;
      const auto messages = first.find(tier);
      if (messages != first.end()) {
        for (const auto& message : messages->second) detail += "\n  " + message;
      }
      if (hard) {
        EXPECT_EQ(count, 0u) << what << ": " << count << " of " << tiles << " tiles differ in tier '" << tier << "'"
                             << detail;
        continue;
      }
      std::cout << "log-only tier '" << tier << "' (" << what << "): " << count << " of " << compared()
                << " tiles differ" << detail << std::endl;
      const double share = compared() == 0 ? 0.0 : static_cast<double>(count) / static_cast<double>(compared());
      const bool over = share > k_log_only_tier_ceiling;
      std::cout << "tier-measure | " << what << " | " << tier << " | differ " << count << " | compared " << compared()
                << " | share " << share << " | ceiling " << k_log_only_tier_ceiling << " | "
                << (over ? "OVER CEILING" : "within ceiling")
                << (compared() != 0 && count >= compared() ? " | EVERY TILE DIFFERS" : "")
                << k_ceiling_mode << std::endl;
      if constexpr (k_log_only_tier_ceiling_enforced) {
        EXPECT_LE(share, k_log_only_tier_ceiling)
            << what << ": " << count << " of " << compared() << " tiles differ in tier '" << tier << "'" << detail;
      }
    }
  }
};

}  // namespace fo4::test::lodcompare
