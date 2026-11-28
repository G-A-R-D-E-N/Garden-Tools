#pragma once

// Constraint: every decoder here has an encoder that rebuilds the identical bytes, and a decode is claimed only for the shapes the retail sweep proves.

#include "fo4/previs/uvd_tile_model.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fo4::previs {

inline constexpr std::uint32_t k_uvd_outside_target = 0x3ffffffu;

struct UvdCell {
  std::uint32_t portal_start{0};
  std::uint32_t portal_count{0};
  std::uint32_t list_start{0};
  std::uint32_t list_count{0};
  std::uint32_t cluster{0};
  std::uint32_t tag{0};
  std::array<std::uint16_t, 2> x{};
  std::array<std::uint16_t, 2> y{};
  std::array<std::uint16_t, 2> z{};
};

struct UvdPortal {
  std::uint32_t target{0};
  std::uint32_t hier{0};
  std::uint32_t door{0};
  std::uint32_t outside{0};
  std::uint32_t face{0};
  std::uint16_t plane{0};
  std::uint16_t target_cell{0};
  std::array<std::uint16_t, 2> rect0{};
  std::array<std::uint16_t, 2> rect1{};
};

struct UvdBspNode {
  std::uint32_t low_is_cell{0};
  std::uint32_t high_is_cell{0};
  std::uint32_t plane{0};
  std::uint16_t low{0};
  std::uint16_t high{0};
};

using UvdPlane = std::array<float, 4>;

struct UvdKd {
  std::uint32_t node_count{0};
  std::uint32_t map_width{0};
  std::vector<std::uint8_t> symbols;
  std::vector<float> splits;
  std::vector<std::uint32_t> entries;
};

struct UvdTileBody {
  std::array<float, 6> box{};
  bool leaf{false};
  std::uint32_t flags{0};
  float expand{0.0f};
  std::uint32_t cluster_count{0};
  std::vector<UvdCell> cells;
  std::vector<UvdPortal> portals;
  std::vector<UvdPlane> planes;
  std::vector<UvdBspNode> bsp;
  std::optional<UvdKd> kd;
};

struct UvdBodies {
  std::vector<std::optional<UvdTileBody>> slots;
};

// Constraint: the node region is 2 bits per node (3 = leaf), a u16 internal-node rank per 256 nodes, then a u8 rank per 16 nodes except every sixteenth.
UvdBytes encode_uvd_nodes(std::span<const std::uint8_t> symbols);
std::vector<std::uint8_t> decode_uvd_symbols(std::span<const std::byte> nodes, std::size_t count);

UvdKd decode_uvd_kd(const UvdKdBytes& kd);
UvdKdBytes encode_uvd_kd(const UvdKd& kd);

struct UvdKdRegion {
  std::array<double, 3> lo{};
  std::array<double, 3> hi{};
};

struct UvdKdCut {
  std::uint32_t axis{0};
  double cut{0.0};
  bool stored{false};
};

struct UvdKdWalk {
  std::vector<UvdKdRegion> regions;
  std::vector<std::optional<UvdKdCut>> cuts;
  bool ok{false};
};

// Constraint: children of internal node k are the next two unused indices in level order; a stored cut applies for index < splits.size(), later internals cut at the midpoint.
UvdKdWalk walk_uvd_kd(std::span<const std::uint8_t> symbols, std::span<const float> splits, const std::array<double, 3>& root_lo,
                      const std::array<double, 3>& root_hi, bool implicit_midpoint = true);

UvdTileBody decode_uvd_tile(const UvdTileBytes& tile);
UvdTileBytes encode_uvd_tile(const UvdTileBody& tile);

UvdBodies decode_uvd_bodies(const UvdTileModel& model);
UvdTileModel encode_uvd_bodies(const UvdBodies& bodies, const UvdTileModel& model);

}  // namespace fo4::previs
