#pragma once

// Constraint: parse and write share these size rules; every offset, count and size in a written tome is derived from the model, never copied from the input.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace fo4::previs {

using UvdBytes = std::vector<std::byte>;

inline constexpr std::size_t k_uvd_tome_header_bytes = 0x150;
inline constexpr std::size_t k_uvd_tile_header_bytes = 0x50;

class UvdTileLayoutError : public std::runtime_error {
public:
  explicit UvdTileLayoutError(const std::string& reason) : std::runtime_error(reason) {}
  const char* reason() const noexcept { return what(); }
};

struct UvdKdBytes {
  std::uint32_t node_count{0};
  std::uint32_t map_width{0};
  UvdBytes nodes;
  UvdBytes splits;
  UvdBytes leaf_map;
};

struct UvdTopKdBytes {
  UvdBytes nodes;
  UvdBytes splits;
};

struct UvdTileBytes {
  std::array<float, 6> box{};
  bool leaf{false};
  std::uint32_t flags{0};
  float expand{0.0f};
  std::uint32_t cluster_count{0};
  UvdBytes cells;
  UvdBytes portals;
  UvdBytes planes;
  UvdBytes bsp;
  std::optional<UvdKdBytes> kd;
};

struct UvdRunListBytes {
  std::uint32_t count{0};
  UvdBytes bytes;
};

struct UvdGateBytes {
  std::uint32_t count{0};
  UvdBytes records;
  UvdBytes lists;
  UvdBytes trailer;
};

struct UvdLeafRowBytes {
  std::array<std::uint32_t, 3> words{};
  UvdBytes block;
  std::uint32_t trees_before{0};
};

struct UvdChildBytes {
  std::vector<std::optional<UvdKdBytes>> trees;
  std::vector<UvdLeafRowBytes> rows;
};

struct UvdTileModel {
  std::string banner;
  float lod_base{0.0f};
  std::uint32_t reserved10{0};
  std::array<float, 3> world_min{};
  std::array<float, 3> world_max{};
  std::uint32_t path_bits{0};
  UvdTopKdBytes top_kd;
  UvdBytes slot_paths;
  UvdBytes lod;
  UvdBytes cell_starts;
  std::array<std::uint32_t, 4> widths{};
  UvdRunListBytes run_list_a;
  UvdRunListBytes run_list_b;
  UvdBytes cluster_nodes;
  UvdBytes cluster_portals;
  UvdGateBytes gates;
  UvdBytes object_boxes;
  std::optional<UvdBytes> object_distances;
  UvdBytes object_ids;
  std::vector<std::optional<UvdTileBytes>> slots;
  UvdChildBytes child;
};

// Constraint: the kd node region is 2 bits per node, then a u16 rank per 256 nodes, then one u8 rank per 16 nodes except every sixteenth (measured on 189,036 trees).
std::uint64_t uvd_node_region_bytes(std::uint64_t node_count);
std::uint64_t uvd_leaf_map_bytes(std::uint64_t node_count, std::uint64_t map_width);
std::uint64_t uvd_slot_path_bytes(std::uint64_t slots, std::uint64_t path_bits);
std::uint64_t uvd_run_list_bytes(std::uint64_t count, std::uint64_t bits);

// Constraint: throws UvdTileLayoutError on any gap, overlap, nonzero padding or size/CRC mismatch instead of skipping it.
UvdTileModel parse_uvd_tiles(std::span<const std::byte> bytes);

// Constraint: every offset, count, size, padding byte and the CRC-32C over [8, size) come from the model alone.
UvdBytes write_uvd_tiles(const UvdTileModel& model);

}  // namespace fo4::previs
