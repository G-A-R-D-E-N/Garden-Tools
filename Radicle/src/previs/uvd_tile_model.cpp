#include "fo4/previs/uvd_tile_model.hpp"

#include "fo4/previs/uvd.hpp"
#include "uvd_tile_internal.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace fo4::previs {

using namespace detail;

std::uint64_t uvd_node_region_bytes(std::uint64_t n) {
  return align16(align4((n + 3) / 4) + align4(2 * (n / 256)) + n / 16 - n / 256);
}

std::uint64_t uvd_leaf_map_bytes(std::uint64_t node_count, std::uint64_t map_width) {
  return ((node_count + 1) / 2 * map_width + 7) / 8;
}

std::uint64_t uvd_slot_path_bytes(std::uint64_t slots, std::uint64_t path_bits) { return (slots * path_bits + 7) / 8; }

std::uint64_t uvd_run_list_bytes(std::uint64_t count, std::uint64_t bits) { return (count * bits + 7) / 8; }

namespace {

class Reader {
public:
  Reader(std::span<const std::byte> bytes, std::uint64_t base, std::uint64_t limit, std::string label)
      : bytes_(bytes), base_(base), limit_(limit), label_(std::move(label)) {}

  std::uint64_t pos{0};

  const std::string& label() const { return label_; }
  void set_limit(std::uint64_t limit) { limit_ = limit; }

  std::uint32_t u32(std::uint64_t o) const {
    need(o, 4);
    return rd32(bytes_, base_ + o);
  }
  std::uint32_t u16(std::uint64_t o) const {
    need(o, 2);
    return rd16(bytes_, base_ + o);
  }
  float f32(std::uint64_t o) const {
    need(o, 4);
    return rdf(bytes_, base_ + o);
  }

  // Constraint: a region starts exactly where the previous one ended, an absent region has offset 0, and every byte between a payload and its 16-byte boundary is zero.
  UvdBytes take(const std::string& name, std::uint64_t off, std::int64_t len, bool whole = false) {
    if (len == 0) {
      if (off != 0) {
        fail(label_ + name + ": offset " + hex(off) + " for an empty region");
      }
      return {};
    }
    if (len < 0) {
      fail(label_ + name + ": ends before it starts");
    }
    if (off != pos) {
      fail(label_ + name + ": at " + hex(off) + ", expected " + hex(pos));
    }
    const std::uint64_t length = static_cast<std::uint64_t>(len);
    const std::uint64_t end = align16(off + length);
    if (base_ + end > limit_) {
      fail(label_ + name + ": " + hex(end) + " runs past the end");
    }
    if (!whole) {
      for (std::uint64_t p = base_ + off + length; p < base_ + end; ++p) {
        if (bytes_[p] != std::byte{0}) {
          fail(label_ + name + ": nonzero padding at " + hex(p - base_));
        }
      }
    }
    pos = whole ? off + length : end;
    const auto first = bytes_.begin() + static_cast<std::ptrdiff_t>(base_ + off);
    return UvdBytes(first, first + static_cast<std::ptrdiff_t>(length));
  }

  void zero(const std::string& name, std::uint64_t off, std::uint64_t len) const {
    for (std::uint64_t p = base_ + off; p < base_ + off + len; ++p) {
      if (bytes_[p] != std::byte{0}) {
        fail(label_ + name + ": nonzero at " + hex(p - base_));
      }
    }
  }

private:
  void need(std::uint64_t o, std::uint64_t size) const {
    if (base_ + o + size > bytes_.size()) {
      fail(label_ + "read at " + hex(o) + " runs past the end");
    }
  }

  std::span<const std::byte> bytes_;
  std::uint64_t base_;
  std::uint64_t limit_;
  std::string label_;
};

UvdKdBytes parse_kd(Reader& r, std::uint32_t word, std::uint32_t node_off, std::uint32_t leaf_map_off,
                    std::uint32_t split_count, std::uint32_t split_off, const std::string& prefix) {
  UvdKdBytes kd;
  kd.node_count = word >> 5u;
  kd.map_width = word & 31u;
  if (kd.node_count % 2 == 0) {
    fail(prefix + ": even node count " + std::to_string(kd.node_count));
  }
  kd.nodes = r.take(prefix + ".nodes", node_off, static_cast<std::int64_t>(uvd_node_region_bytes(kd.node_count)));
  kd.splits = r.take(prefix + ".splits", split_off, 4 * static_cast<std::int64_t>(split_count));
  kd.leaf_map = r.take(prefix + ".leafMap", leaf_map_off,
                       kd.map_width != 0 ? static_cast<std::int64_t>(uvd_leaf_map_bytes(kd.node_count, kd.map_width)) : 0);
  return kd;
}

UvdTileBytes parse_tile(std::span<const std::byte> bytes, std::uint64_t at, std::uint64_t limit, std::size_t slot) {
  Reader r(bytes, at, limit, "tile slot " + std::to_string(slot) + ": ");
  const std::uint32_t word = r.u32(0x2c);
  const std::uint32_t size = word >> 8u;
  const bool leaf = (word & 1u) == 1u;
  if (size < k_uvd_tile_header_bytes || size % 16 != 0) {
    fail(r.label() + "size " + std::to_string(size));
  }
  if (at + size > limit) {
    fail(r.label() + "size " + std::to_string(size) + " runs past the end");
  }
  r.set_limit(at + size);
  const std::uint32_t cell_count = r.u16(0x34);
  if (r.u32(0x38) != k_uvd_tile_header_bytes) {
    fail(r.label() + "cells at " + hex(r.u32(0x38)));
  }
  r.pos = k_uvd_tile_header_bytes;
  UvdTileBytes tile;
  for (std::size_t k = 0; k < 6; ++k) tile.box[k] = r.f32(4 * k);
  tile.leaf = leaf;
  tile.flags = (word & 0xffu) >> 1u;
  tile.expand = r.f32(0x30);
  tile.cluster_count = r.u16(0x36);
  tile.cells = r.take("cells", k_uvd_tile_header_bytes, 36 * static_cast<std::int64_t>(cell_count));
  const std::uint32_t plane_count = r.u32(0x4c);
  const std::uint32_t bsp_count = r.u32(0x44);
  const std::uint64_t next_after_portals = r.u32(0x48) != 0 ? r.u32(0x48) : (leaf ? r.u32(0x1c) : size);
  const std::uint32_t portal_off = r.u32(0x3c);
  if (portal_off != r.pos) {
    fail(r.label() + "portals at " + hex(portal_off) + ", expected " + hex(r.pos));
  }
  tile.portals = r.take("portals", portal_off, static_cast<std::int64_t>(next_after_portals) - static_cast<std::int64_t>(portal_off), true);
  tile.planes = r.take("planes", r.u32(0x48), 16 * static_cast<std::int64_t>(plane_count));
  if (leaf) {
    tile.kd = parse_kd(r, r.u32(0x18), r.u32(0x1c), r.u32(0x20), r.u32(0x24), r.u32(0x28), r.label() + "kd");
  } else {
    r.zero("kd descriptor", 0x18, 0x14);
  }
  tile.bsp = r.take("bsp", r.u32(0x40), 8 * static_cast<std::int64_t>(bsp_count));
  if (r.pos != size) {
    fail(r.label() + "regions end at " + hex(r.pos) + ", size field " + hex(size));
  }
  return tile;
}

struct Start {
  std::uint64_t at;
  char kind;
  std::size_t i;
};

void parse_child(UvdTileModel& model, std::span<const std::byte> bytes, Reader& r, const UvdBytes& row_bytes) {
  const std::uint32_t child_off = r.u32(0xa8);
  const std::uint32_t count = r.u32(0xac);
  if (child_off != r.pos) {
    fail("child block at " + hex(child_off) + ", expected " + hex(r.pos));
  }
  const std::uint64_t body_start = align16(static_cast<std::uint64_t>(child_off) + 20ull * count);
  if (body_start > bytes.size()) {
    fail("child descriptors run past the end");
  }
  std::vector<std::optional<std::array<std::uint32_t, 5>>> descs(count);
  std::vector<Start> starts;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint64_t d = child_off + 20ull * i;
    std::array<std::uint32_t, 5> v{};
    for (std::size_t k = 0; k < 5; ++k) v[k] = rd32(bytes, d + 4 * k);
    if (v[0] == 0) {
      if (v[1] != 0 || v[2] != 0 || v[3] != 0 || v[4] != 0) {
        fail("child descriptor " + std::to_string(i) + ": empty tree with offsets");
      }
      continue;
    }
    starts.push_back({v[1], 'T', i});
    descs[i] = v;
  }
  const std::size_t row_count = row_bytes.size() / 16;
  model.child.rows.assign(row_count, UvdLeafRowBytes{});
  for (std::size_t k = 0; k < row_count; ++k) {
    std::array<std::uint32_t, 4> w{};
    for (std::size_t j = 0; j < 4; ++j) w[j] = rd32(row_bytes, 16 * k + 4 * j);
    model.child.rows[k].words = {w[0], w[2], w[3]};
    if (w[1] != 0) {
      starts.push_back({w[1], 'B', k});
    }
  }
  std::stable_sort(starts.begin(), starts.end(), [](const Start& a, const Start& b) { return a.at < b.at; });
  model.child.trees.assign(count, std::nullopt);
  Reader rd(bytes, 0, bytes.size(), "child: ");
  rd.pos = body_start;
  for (std::size_t idx = 0; idx < starts.size(); ++idx) {
    const Start& s = starts[idx];
    if (s.kind == 'T') {
      const auto& t = *descs[s.i];
      model.child.trees[s.i] = parse_kd(rd, t[0], t[1], t[2], t[3], t[4], "tree " + std::to_string(s.i));
      continue;
    }
    const std::uint64_t next = idx + 1 < starts.size() ? starts[idx + 1].at : bytes.size();
    model.child.rows[s.i].block = rd.take("block " + std::to_string(s.i), s.at, static_cast<std::int64_t>(next) - static_cast<std::int64_t>(s.at), true);
  }
  if (rd.pos != bytes.size()) {
    fail("child body ends at " + hex(rd.pos) + ", file ends at " + hex(bytes.size()));
  }
  std::vector<std::size_t> tree_idx;
  for (std::size_t i = 0; i < model.child.trees.size(); ++i) {
    if (model.child.trees[i]) {
      tree_idx.push_back(i);
    }
  }
  std::size_t order = 0;
  for (const Start& s : starts) {
    if (s.kind == 'T') {
      ++order;
      continue;
    }
    model.child.rows[s.i].trees_before = static_cast<std::uint32_t>(order < tree_idx.size() ? tree_idx[order] : count);
  }
}

}  // namespace

UvdTileModel parse_uvd_tiles(std::span<const std::byte> bytes) {
  if (bytes.size() < k_uvd_tome_header_bytes) {
    fail("shorter than the 0x150-byte header");
  }
  Reader r(bytes, 0, bytes.size(), "");
  if (r.u32(0) != k_uvd_magic) {
    fail("bad magic");
  }
  if (r.u32(8) != bytes.size()) {
    fail("size field " + std::to_string(r.u32(8)) + " != " + std::to_string(bytes.size()));
  }
  if (bytes.size() % 16 != 0) {
    fail("size is not 16-aligned");
  }
  if (crc32c(bytes.subspan(8)) != r.u32(4)) {
    fail("crc mismatch");
  }
  std::uint64_t banner_end = k_uvd_banner_offset;
  while (banner_end < k_uvd_tome_header_bytes && bytes[banner_end] != std::byte{0}) ++banner_end;
  if (banner_end >= k_uvd_tome_header_bytes) {
    fail("banner has no NUL inside the header");
  }
  r.zero("after banner", banner_end, k_uvd_tome_header_bytes - banner_end);
  r.zero("reserved 0xb0", 0xb0, k_uvd_banner_offset - 0xb0);
  r.zero("reserved 0x34", 0x34, 4);
  r.zero("reserved 0x4c", 0x4c, 4);

  const std::uint32_t n = r.u32(0x90);
  if (n == 0) {
    fail("no tile slots");
  }
  if (r.u32(0x2c) >> 5u != n) {
    fail("top kd node count differs from the slot count");
  }
  UvdTileModel model;
  model.banner.assign(reinterpret_cast<const char*>(bytes.data() + k_uvd_banner_offset), banner_end - k_uvd_banner_offset);
  model.lod_base = r.f32(0x0c);
  model.reserved10 = r.u32(0x10);
  for (std::size_t a = 0; a < 3; ++a) {
    model.world_min[a] = r.f32(0x14 + 4 * a);
    model.world_max[a] = r.f32(0x20 + 4 * a);
  }
  model.path_bits = r.u32(0x94);
  r.pos = k_uvd_tome_header_bytes;
  if ((r.u32(0x2c) & 31u) != 0) {
    fail("top kd tree with a leaf map is not modelled");
  }
  model.top_kd.nodes = r.take("topKd.nodes", r.u32(0x30), static_cast<std::int64_t>(uvd_node_region_bytes(n)));
  model.top_kd.splits = r.take("topKd.splits", r.u32(0x3c), 4 * static_cast<std::int64_t>(r.u32(0x38)));
  model.slot_paths = r.take("slotPaths", r.u32(0x98), static_cast<std::int64_t>(uvd_slot_path_bytes(n, model.path_bits)));
  model.lod = r.take("lod", r.u32(0x9c), 4 * static_cast<std::int64_t>(n));
  model.cell_starts = r.take("cellStarts", r.u32(0x88), 4 * (static_cast<std::int64_t>(n) + 1));

  const std::uint32_t width_word = r.u32(0x54);
  if ((width_word >> 20u) != 0) {
    fail("list widths use more than 20 bits");
  }
  for (std::size_t k = 0; k < 4; ++k) model.widths[k] = (width_word >> (5 * k)) & 31u;
  const std::uint32_t objects = r.u32(0x40);
  model.run_list_a.count = r.u32(0x5c);
  model.run_list_a.bytes = r.take("runListA", r.u32(0x58),
                                  static_cast<std::int64_t>(uvd_run_list_bytes(model.run_list_a.count, model.widths[0] + model.widths[1])));
  model.run_list_b.count = r.u32(0x64);
  model.run_list_b.bytes = r.take("runListB", r.u32(0x60),
                                  static_cast<std::int64_t>(uvd_run_list_bytes(model.run_list_b.count, model.widths[2] + model.widths[3])));
  model.cluster_nodes = r.take("clusterNodes", r.u32(0x80), 20 * static_cast<std::int64_t>(r.u32(0x7c)));
  const std::uint32_t gate_count = r.u32(0x68);
  const std::int64_t after_portals = gate_count != 0 ? r.u32(0x70) : objects != 0 ? r.u32(0x44) : r.u32(0xa0);
  model.cluster_portals = r.take("clusterPortals", r.u32(0x84), r.u32(0x84) != 0 ? after_portals - static_cast<std::int64_t>(r.u32(0x84)) : 0, true);
  model.gates.count = gate_count;
  model.gates.records = r.take("gates.records", r.u32(0x70), 12 * static_cast<std::int64_t>(r.u32(0x74)));
  const std::int64_t after_gates = objects != 0 ? r.u32(0x44) : r.u32(0xa0);
  model.gates.lists = r.take("gates.lists", r.u32(0x78), r.u32(0x78) != 0 ? after_gates - static_cast<std::int64_t>(r.u32(0x78)) : 0, true);
  model.object_boxes = r.take("objectBoxes", r.u32(0x44), 24 * static_cast<std::int64_t>(objects));
  if (r.u32(0x48) != 0) {
    model.object_distances = r.take("objectDistances", r.u32(0x48), 32 * static_cast<std::int64_t>(objects));
  }
  model.object_ids = r.take("objectIds", r.u32(0x50), 4 * static_cast<std::int64_t>(objects));

  const std::uint32_t table_off = r.u32(0xa0);
  r.take("tileTable", table_off, 4 * static_cast<std::int64_t>(n));
  model.slots.assign(n, std::nullopt);
  std::uint32_t leaf_tiles = 0;
  for (std::uint32_t s = 0; s < n; ++s) {
    const std::uint32_t at = rd32(bytes, table_off + 4ull * s);
    if (at == 0) {
      continue;
    }
    if (at != r.pos) {
      fail("tile slot " + std::to_string(s) + ": at " + hex(at) + ", expected " + hex(r.pos));
    }
    UvdTileBytes tile = parse_tile(bytes, at, bytes.size(), s);
    if (tile.leaf) {
      ++leaf_tiles;
    }
    model.slots[s] = std::move(tile);
    r.pos = at + (rd32(bytes, at + 0x2c) >> 8u);
  }
  if (leaf_tiles != r.u32(0x8c)) {
    fail("leaf tile count " + std::to_string(r.u32(0x8c)) + " != tiles with the leaf flag " + std::to_string(leaf_tiles));
  }
  const std::uint32_t lm_off = r.u32(0xa4);
  model.gates.trailer = r.take("gates.trailer", r.u32(0x6c), gate_count != 0 ? static_cast<std::int64_t>(lm_off) - static_cast<std::int64_t>(r.u32(0x6c)) : 0, true);
  const UvdBytes row_bytes = r.take("leafMatch", lm_off, 16 * static_cast<std::int64_t>(leaf_tiles));
  parse_child(model, bytes, r, row_bytes);
  return model;
}

}  // namespace fo4::previs
