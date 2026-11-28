#include "fo4/previs/uvd_tile_body.hpp"

#include "uvd_tile_internal.hpp"

#include <limits>
#include <string>

namespace fo4::previs {

using namespace detail;

namespace {

constexpr std::size_t k_cell_bytes = 36;
constexpr std::size_t k_portal_bytes = 16;
constexpr std::size_t k_plane_bytes = 16;
constexpr std::size_t k_bsp_bytes = 8;

void require_whole(const UvdBytes& bytes, std::size_t unit, const char* label) {
  if (bytes.size() % unit != 0) {
    fail(std::string(label) + " is not a whole number of " + std::to_string(unit) + "-byte records");
  }
}

std::vector<UvdCell> decode_cells(const UvdBytes& buf) {
  require_whole(buf, k_cell_bytes, "cells");
  std::vector<UvdCell> cells(buf.size() / k_cell_bytes);
  for (std::size_t i = 0; i < cells.size(); ++i) {
    const std::size_t o = i * k_cell_bytes;
    const auto q = [&](std::size_t k) { return rd16(buf, o + 24 + 2 * k); };
    UvdCell& c = cells[i];
    c.portal_start = rd32(buf, o);
    c.portal_count = rd32(buf, o + 4);
    c.list_start = rd32(buf, o + 8);
    c.list_count = rd32(buf, o + 12);
    c.cluster = rd32(buf, o + 16);
    c.tag = rd32(buf, o + 20);
    c.x = {q(0), q(5)};
    c.y = {q(1), q(2)};
    c.z = {q(3), q(4)};
  }
  return cells;
}

UvdBytes encode_cells(const std::vector<UvdCell>& cells) {
  UvdBytes buf(k_cell_bytes * cells.size(), std::byte{0});
  for (std::size_t i = 0; i < cells.size(); ++i) {
    const std::size_t o = i * k_cell_bytes;
    const UvdCell& c = cells[i];
    const std::array<std::uint32_t, 6> words{c.portal_start, c.portal_count, c.list_start, c.list_count, c.cluster, c.tag};
    for (std::size_t k = 0; k < 6; ++k) wr32(buf, o + 4 * k, words[k]);
    const std::array<std::uint16_t, 6> box{c.x[0], c.y[0], c.y[1], c.z[0], c.z[1], c.x[1]};
    for (std::size_t k = 0; k < 6; ++k) wr16(buf, o + 24 + 2 * k, box[k]);
  }
  return buf;
}

std::vector<UvdPortal> decode_portals(const UvdBytes& buf) {
  require_whole(buf, k_portal_bytes, "portals");
  std::vector<UvdPortal> portals(buf.size() / k_portal_bytes);
  for (std::size_t i = 0; i < portals.size(); ++i) {
    const std::size_t o = i * k_portal_bytes;
    const std::uint32_t link = rd32(buf, o);
    const std::uint32_t r0 = rd32(buf, o + 8);
    const std::uint32_t r1 = rd32(buf, o + 12);
    UvdPortal& p = portals[i];
    p.target = link & k_uvd_outside_target;
    p.hier = (link >> 26u) & 1u;
    p.door = (link >> 27u) & 1u;
    p.outside = (link >> 28u) & 1u;
    p.face = link >> 29u;
    p.plane = rd16(buf, o + 4);
    p.target_cell = rd16(buf, o + 6);
    p.rect0 = {static_cast<std::uint16_t>(r0 >> 16u), static_cast<std::uint16_t>(r0 & 0xffffu)};
    p.rect1 = {static_cast<std::uint16_t>(r1 >> 16u), static_cast<std::uint16_t>(r1 & 0xffffu)};
  }
  return portals;
}

UvdBytes encode_portals(const std::vector<UvdPortal>& portals) {
  UvdBytes buf(k_portal_bytes * portals.size(), std::byte{0});
  for (std::size_t i = 0; i < portals.size(); ++i) {
    const std::size_t o = i * k_portal_bytes;
    const UvdPortal& p = portals[i];
    if (p.target > k_uvd_outside_target || p.hier > 1 || p.door > 1 || p.outside > 1 || p.face > 7) {
      fail("model: portal field wider than its bits");
    }
    wr32(buf, o, p.target | (p.hier << 26u) | (p.door << 27u) | (p.outside << 28u) | (p.face << 29u));
    wr16(buf, o + 4, p.plane);
    wr16(buf, o + 6, p.target_cell);
    wr32(buf, o + 8, (static_cast<std::uint32_t>(p.rect0[0]) << 16u) | p.rect0[1]);
    wr32(buf, o + 12, (static_cast<std::uint32_t>(p.rect1[0]) << 16u) | p.rect1[1]);
  }
  return buf;
}

std::vector<UvdPlane> decode_planes(const UvdBytes& buf) {
  require_whole(buf, k_plane_bytes, "planes");
  std::vector<UvdPlane> planes(buf.size() / k_plane_bytes);
  for (std::size_t i = 0; i < planes.size(); ++i) {
    for (std::size_t k = 0; k < 4; ++k) planes[i][k] = rdf(buf, i * k_plane_bytes + 4 * k);
  }
  return planes;
}

UvdBytes encode_planes(const std::vector<UvdPlane>& planes) {
  UvdBytes buf(k_plane_bytes * planes.size(), std::byte{0});
  for (std::size_t i = 0; i < planes.size(); ++i) {
    for (std::size_t k = 0; k < 4; ++k) wrf(buf, i * k_plane_bytes + 4 * k, planes[i][k]);
  }
  return buf;
}

std::vector<UvdBspNode> decode_bsp(const UvdBytes& buf) {
  require_whole(buf, k_bsp_bytes, "bsp");
  std::vector<UvdBspNode> nodes(buf.size() / k_bsp_bytes);
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const std::size_t o = i * k_bsp_bytes;
    const std::uint32_t word = rd32(buf, o);
    nodes[i].low_is_cell = (word >> 30u) & 1u;
    nodes[i].high_is_cell = word >> 31u;
    nodes[i].plane = word & 0x3fffffffu;
    nodes[i].low = rd16(buf, o + 4);
    nodes[i].high = rd16(buf, o + 6);
  }
  return nodes;
}

UvdBytes encode_bsp(const std::vector<UvdBspNode>& nodes) {
  UvdBytes buf(k_bsp_bytes * nodes.size(), std::byte{0});
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const UvdBspNode& n = nodes[i];
    if (n.plane > 0x3fffffffu || n.low_is_cell > 1 || n.high_is_cell > 1) {
      fail("model: bsp field wider than its bits");
    }
    wr32(buf, i * k_bsp_bytes, n.plane | (n.low_is_cell << 30u) | (n.high_is_cell << 31u));
    wr16(buf, i * k_bsp_bytes + 4, n.low);
    wr16(buf, i * k_bsp_bytes + 6, n.high);
  }
  return buf;
}

}  // namespace

UvdBytes encode_uvd_nodes(std::span<const std::uint8_t> symbols) {
  const std::size_t n = symbols.size();
  UvdBytes out(uvd_node_region_bytes(n), std::byte{0});
  std::vector<std::uint32_t> before(n + 1, 0);
  for (std::size_t i = 0; i < n; ++i) {
    if (symbols[i] > 3) {
      fail("model: kd symbol above 3");
    }
    out[i >> 2u] |= static_cast<std::byte>(static_cast<unsigned>(symbols[i]) << ((i & 3u) * 2u));
    before[i + 1] = before[i] + (symbols[i] != 3 ? 1u : 0u);
  }
  std::uint64_t pos = align4((2 * n + 7) / 8);
  const std::size_t supers = n / 256;
  for (std::size_t k = 1; k <= supers; ++k) {
    if (before[256 * k] > 0xffffu) {
      fail("model: kd rank exceeds 16 bits");
    }
    wr16(out, pos + 2 * (k - 1), static_cast<std::uint16_t>(before[256 * k]));
  }
  pos += align4(2 * supers);
  for (std::size_t k = 1; k <= n / 16; ++k) {
    if (k % 16 != 0) {
      out[pos++] = static_cast<std::byte>(before[16 * k] - before[256 * (k / 16)]);
    }
  }
  return out;
}

std::vector<std::uint8_t> decode_uvd_symbols(std::span<const std::byte> nodes, std::size_t count) {
  if (nodes.size() < (count + 3) / 4) {
    fail("kd node region is shorter than its symbols");
  }
  std::vector<std::uint8_t> symbols(count);
  for (std::size_t i = 0; i < count; ++i) {
    symbols[i] = static_cast<std::uint8_t>((std::to_integer<unsigned>(nodes[i >> 2u]) >> ((i & 3u) * 2u)) & 3u);
  }
  return symbols;
}

UvdKd decode_uvd_kd(const UvdKdBytes& kd) {
  UvdKd d;
  d.node_count = kd.node_count;
  d.map_width = kd.map_width;
  d.symbols = decode_uvd_symbols(kd.nodes, kd.node_count);
  require_whole(kd.splits, 4, "kd splits");
  d.splits.resize(kd.splits.size() / 4);
  for (std::size_t i = 0; i < d.splits.size(); ++i) d.splits[i] = rdf(kd.splits, 4 * i);
  const std::uint64_t entries = kd.map_width != 0 ? (static_cast<std::uint64_t>(kd.node_count) + 1) / 2 : 0;
  if (kd.map_width != 0 && kd.leaf_map.size() < uvd_leaf_map_bytes(kd.node_count, kd.map_width)) {
    fail("kd leaf map is shorter than its entries");
  }
  d.entries.assign(entries, 0);
  for (std::uint64_t i = 0; i < entries; ++i) {
    std::uint32_t v = 0;
    for (std::uint32_t b = 0; b < kd.map_width; ++b) {
      const std::uint64_t bit = i * kd.map_width + b;
      v |= ((std::to_integer<std::uint32_t>(kd.leaf_map[bit >> 3u]) >> (bit & 7u)) & 1u) << b;
    }
    d.entries[i] = v;
  }
  return d;
}

UvdKdBytes encode_uvd_kd(const UvdKd& d) {
  UvdKdBytes kd;
  kd.node_count = d.node_count;
  kd.map_width = d.map_width;
  if (d.symbols.size() != d.node_count) {
    fail("model: kd symbol count differs from its node count");
  }
  if (d.map_width > 31) {
    fail("model: kd map width exceeds 31 bits");
  }
  const std::uint64_t want = d.map_width != 0 ? (static_cast<std::uint64_t>(d.node_count) + 1) / 2 : 0;
  if (d.entries.size() != want) {
    fail("model: kd leaf map entry count differs from its leaf count");
  }
  kd.splits.assign(4 * d.splits.size(), std::byte{0});
  for (std::size_t i = 0; i < d.splits.size(); ++i) wrf(kd.splits, 4 * i, d.splits[i]);
  kd.leaf_map.assign(d.map_width != 0 ? uvd_leaf_map_bytes(d.node_count, d.map_width) : 0, std::byte{0});
  for (std::size_t i = 0; i < d.entries.size(); ++i) {
    if (d.map_width < 32 && (d.entries[i] >> d.map_width) != 0) {
      fail("model: kd leaf map entry wider than the map width");
    }
    for (std::uint32_t b = 0; b < d.map_width; ++b) {
      if (((d.entries[i] >> b) & 1u) == 0) {
        continue;
      }
      const std::uint64_t bit = i * d.map_width + b;
      kd.leaf_map[bit >> 3u] |= static_cast<std::byte>(1u << (bit & 7u));
    }
  }
  kd.nodes = encode_uvd_nodes(d.symbols);
  return kd;
}

UvdKdWalk walk_uvd_kd(std::span<const std::uint8_t> symbols, std::span<const float> splits, const std::array<double, 3>& root_lo,
                      const std::array<double, 3>& root_hi, bool implicit_midpoint) {
  const std::size_t n = symbols.size();
  UvdKdWalk walk;
  walk.regions.push_back({root_lo, root_hi});
  walk.cuts.assign(n, std::nullopt);
  std::size_t next = 1;
  for (std::size_t i = 0; i < n; ++i) {
    if (symbols[i] == 3) {
      continue;
    }
    if (i >= walk.regions.size()) {
      return walk;
    }
    const UvdKdRegion r = walk.regions[i];
    const std::uint32_t a = symbols[i];
    const bool stored = i < splits.size();
    const double cut = stored ? static_cast<double>(splits[i]) : implicit_midpoint ? (r.lo[a] + r.hi[a]) / 2 : std::numeric_limits<double>::quiet_NaN();
    walk.cuts[i] = UvdKdCut{a, cut, stored};
    UvdKdRegion left = r;
    UvdKdRegion right = r;
    left.hi[a] = cut;
    right.lo[a] = cut;
    walk.regions.push_back(left);
    walk.regions.push_back(right);
    next += 2;
  }
  walk.ok = next == n;
  return walk;
}

UvdTileBody decode_uvd_tile(const UvdTileBytes& tile) {
  UvdTileBody d;
  d.box = tile.box;
  d.leaf = tile.leaf;
  d.flags = tile.flags;
  d.expand = tile.expand;
  d.cluster_count = tile.cluster_count;
  d.cells = decode_cells(tile.cells);
  d.portals = decode_portals(tile.portals);
  d.planes = decode_planes(tile.planes);
  d.bsp = decode_bsp(tile.bsp);
  if (tile.kd) {
    d.kd = decode_uvd_kd(*tile.kd);
  }
  return d;
}

UvdTileBytes encode_uvd_tile(const UvdTileBody& d) {
  UvdTileBytes tile;
  tile.box = d.box;
  tile.leaf = d.leaf;
  tile.flags = d.flags;
  tile.expand = d.expand;
  tile.cluster_count = d.cluster_count;
  tile.cells = encode_cells(d.cells);
  tile.portals = encode_portals(d.portals);
  tile.planes = encode_planes(d.planes);
  tile.bsp = encode_bsp(d.bsp);
  if (d.kd) {
    tile.kd = encode_uvd_kd(*d.kd);
  }
  return tile;
}

UvdBodies decode_uvd_bodies(const UvdTileModel& model) {
  UvdBodies bodies;
  bodies.slots.resize(model.slots.size());
  for (std::size_t i = 0; i < model.slots.size(); ++i) {
    if (model.slots[i]) {
      bodies.slots[i] = decode_uvd_tile(*model.slots[i]);
    }
  }
  return bodies;
}

UvdTileModel encode_uvd_bodies(const UvdBodies& bodies, const UvdTileModel& model) {
  UvdTileModel out = model;
  out.slots.assign(bodies.slots.size(), std::nullopt);
  for (std::size_t i = 0; i < bodies.slots.size(); ++i) {
    if (bodies.slots[i]) {
      out.slots[i] = encode_uvd_tile(*bodies.slots[i]);
    }
  }
  return out;
}

}  // namespace fo4::previs
