#pragma once

// Constraint: slot paths, cell starts, tile leaf flags and the empty-slot LOD marker are derived on encode from the tree and the tiles, never stored in the decoded form.

#include "fo4/previs/uvd_tile_body.hpp"
#include "fo4/previs/uvd_tile_model.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::previs {

struct UvdCluster {
  std::uint32_t portal_start{0};
  std::uint32_t portal_count{0};
  std::array<std::uint16_t, 2> x{};
  std::array<std::uint16_t, 2> y{};
  std::array<std::uint16_t, 2> z{};
};

struct UvdObjects {
  std::vector<std::array<float, 6>> boxes;
  std::optional<std::vector<std::array<float, 8>>> distances;
  std::vector<std::uint32_t> ids;
};

struct UvdGates {
  std::uint32_t count{0};
  std::vector<std::array<std::uint32_t, 3>> records;
  UvdBytes lists;
  UvdBytes trailer;
};

struct UvdHeader {
  std::string banner;
  float lod_base{0.0f};
  std::uint32_t reserved10{0};
  std::array<float, 3> world_min{};
  std::array<float, 3> world_max{};
  std::array<std::uint32_t, 4> widths{};
  std::vector<std::uint8_t> top_symbols;
  std::vector<float> top_splits;
  std::vector<float> lod;
  UvdObjects objects;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> run_a;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> run_b;
  std::vector<UvdCluster> clusters;
  UvdBytes cluster_portals;
  UvdGates gates;
  std::vector<std::optional<UvdKd>> child_trees;
  std::vector<UvdLeafRowBytes> child_rows;
};

struct UvdSlotPaths {
  std::uint32_t path_bits{0};
  UvdBytes bytes;
};

// Constraint: a slot path is one sentinel bit at pathBits-1-depth with the inverted turns (root first) in the bits above it, measured on 321,002 slots; a tree deeper than 62 levels is refused.
UvdSlotPaths uvd_slot_paths(std::span<const std::uint8_t> symbols);

UvdHeader decode_uvd_header(const UvdTileModel& model);
UvdTileModel encode_uvd_header(const UvdHeader& header, const UvdTileModel& model);

}  // namespace fo4::previs
