#include "fo4/previs/uvd.hpp"
#include "fo4/previs/uvd_tile_model.hpp"
#include "uvd_tile_internal.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <string>

namespace fo4::previs {

using namespace detail;

namespace {

constexpr std::uint64_t k_max_tile_bytes = std::uint64_t{1} << 24;

class Sink {
public:
  explicit Sink(std::size_t reserved) : out(reserved, std::byte{0}), pos(reserved) {}

  UvdBytes out;
  std::uint64_t pos;

  void place(std::uint64_t at, std::span<const std::byte> bytes) {
    if (out.size() < at + bytes.size()) {
      out.resize(at + bytes.size(), std::byte{0});
    }
    std::copy(bytes.begin(), bytes.end(), out.begin() + static_cast<std::ptrdiff_t>(at));
  }

  std::uint32_t put(std::span<const std::byte> bytes, bool whole = false) {
    if (bytes.empty()) {
      return 0;
    }
    const std::uint64_t at = pos;
    place(at, bytes);
    pos = whole ? at + bytes.size() : align16(at + bytes.size());
    return static_cast<std::uint32_t>(at);
  }

  void w32(std::uint64_t at, std::uint32_t v) { wr32(out, at, v); }
};

void require_records(const UvdBytes& bytes, std::size_t unit, const char* label) {
  if (bytes.size() % unit != 0) {
    fail(std::string("model: ") + label + " is not a whole number of " + std::to_string(unit) + "-byte records");
  }
}

void require_size(const char* label, std::uint64_t got, std::uint64_t want) {
  if (got != want) {
    fail(std::string("model: ") + label + " is " + std::to_string(got) + ", expected " + std::to_string(want));
  }
}

struct KdOffsets {
  std::uint32_t word{0};
  std::uint32_t node_off{0};
  std::uint32_t leaf_off{0};
  std::uint32_t split_count{0};
  std::uint32_t split_off{0};
};

KdOffsets write_kd(Sink& s, const UvdKdBytes& kd) {
  if (kd.map_width > 31) {
    fail("model: kd map width exceeds 31 bits");
  }
  if (kd.node_count >= (1u << 27)) {
    fail("model: kd node count exceeds 27 bits");
  }
  require_records(kd.splits, 4, "kd splits");
  KdOffsets o;
  o.node_off = s.put(kd.nodes);
  o.split_off = s.put(kd.splits);
  o.leaf_off = s.put(kd.leaf_map);
  o.split_count = static_cast<std::uint32_t>(kd.splits.size() / 4);
  o.word = (kd.node_count << 5u) | kd.map_width;
  return o;
}

UvdBytes write_tile(const UvdTileBytes& tile) {
  if (tile.leaf != tile.kd.has_value()) {
    fail(tile.leaf ? "model: leaf tile without a kd tree" : "model: interior tile carries a kd tree");
  }
  if (tile.flags > 127) {
    fail("model: tile flags exceed 7 bits");
  }
  require_records(tile.cells, 36, "tile cells");
  require_records(tile.planes, 16, "tile planes");
  require_records(tile.bsp, 8, "tile bsp");
  if (tile.cells.size() / 36 > 0xffff) {
    fail("model: more than 65535 cells in a tile");
  }
  if (tile.cluster_count > 0xffff) {
    fail("model: cluster count exceeds 16 bits");
  }
  Sink s(k_uvd_tile_header_bytes);
  for (std::size_t k = 0; k < 6; ++k) wrf(s.out, 4 * k, tile.box[k]);
  s.pos = k_uvd_tile_header_bytes;
  s.put(tile.cells);
  s.pos = align16(k_uvd_tile_header_bytes + tile.cells.size());
  s.w32(0x38, static_cast<std::uint32_t>(k_uvd_tile_header_bytes));
  s.w32(0x3c, s.put(tile.portals, true));
  s.w32(0x48, s.put(tile.planes));
  s.w32(0x4c, static_cast<std::uint32_t>(tile.planes.size() / 16));
  if (tile.kd) {
    const KdOffsets kd = write_kd(s, *tile.kd);
    s.w32(0x18, kd.word);
    s.w32(0x1c, kd.node_off);
    s.w32(0x20, kd.leaf_off);
    s.w32(0x24, kd.split_count);
    s.w32(0x28, kd.split_off);
  }
  s.w32(0x40, s.put(tile.bsp));
  s.w32(0x44, static_cast<std::uint32_t>(tile.bsp.size() / 8));
  if (s.pos >= k_max_tile_bytes) {
    fail("model: a tile reaches 16 MiB");
  }
  s.w32(0x2c, static_cast<std::uint32_t>((s.pos << 8u) | (static_cast<std::uint64_t>(tile.flags) << 1u) | (tile.leaf ? 1u : 0u)));
  wrf(s.out, 0x30, tile.expand);
  wr16(s.out, 0x34, static_cast<std::uint16_t>(tile.cells.size() / 36));
  wr16(s.out, 0x36, static_cast<std::uint16_t>(tile.cluster_count));
  s.out.resize(s.pos, std::byte{0});
  return std::move(s.out);
}

}  // namespace

UvdBytes write_uvd_tiles(const UvdTileModel& model) {
  const std::size_t n = model.slots.size();
  if (n == 0) {
    fail("model: no tile slots");
  }
  if (n >= (std::size_t{1} << 27)) {
    fail("model: slot count exceeds 27 bits");
  }
  if (model.banner.size() + k_uvd_banner_offset >= k_uvd_tome_header_bytes) {
    fail("model: banner does not fit inside the header");
  }
  if (model.banner.find('\0') != std::string::npos) {
    fail("model: banner contains a NUL");
  }
  Sink s(k_uvd_tome_header_bytes);
  const auto put = [&s](const UvdBytes& bytes, std::uint32_t hdr, bool whole = false) { s.w32(hdr, s.put(bytes, whole)); };
  wr32(s.out, 0, k_uvd_magic);
  wrf(s.out, 0x0c, model.lod_base);
  s.w32(0x10, model.reserved10);
  for (std::size_t a = 0; a < 3; ++a) {
    wrf(s.out, 0x14 + 4 * a, model.world_min[a]);
    wrf(s.out, 0x20 + 4 * a, model.world_max[a]);
  }
  std::copy_n(reinterpret_cast<const std::byte*>(model.banner.data()), model.banner.size(), s.out.begin() + static_cast<std::ptrdiff_t>(k_uvd_banner_offset));
  s.w32(0x2c, static_cast<std::uint32_t>(n << 5u));
  s.w32(0x90, static_cast<std::uint32_t>(n));
  s.w32(0x94, model.path_bits);
  require_records(model.top_kd.splits, 4, "top kd splits");
  put(model.top_kd.nodes, 0x30);
  s.w32(0x38, static_cast<std::uint32_t>(model.top_kd.splits.size() / 4));
  put(model.top_kd.splits, 0x3c);
  require_size("slotPaths bytes", model.slot_paths.size(), uvd_slot_path_bytes(n, model.path_bits));
  put(model.slot_paths, 0x98);
  require_size("lod bytes", model.lod.size(), 4 * n);
  put(model.lod, 0x9c);
  require_size("cellStarts bytes", model.cell_starts.size(), 4 * (n + 1));
  put(model.cell_starts, 0x88);
  std::uint32_t width_word = 0;
  for (std::size_t k = 0; k < 4; ++k) {
    if (model.widths[k] > 31) {
      fail("model: list width exceeds 5 bits");
    }
    width_word |= model.widths[k] << (5 * k);
  }
  s.w32(0x54, width_word);
  require_size("runListA bytes", model.run_list_a.bytes.size(), uvd_run_list_bytes(model.run_list_a.count, model.widths[0] + model.widths[1]));
  s.w32(0x5c, model.run_list_a.count);
  put(model.run_list_a.bytes, 0x58);
  require_size("runListB bytes", model.run_list_b.bytes.size(), uvd_run_list_bytes(model.run_list_b.count, model.widths[2] + model.widths[3]));
  s.w32(0x64, model.run_list_b.count);
  put(model.run_list_b.bytes, 0x60);
  require_records(model.cluster_nodes, 20, "cluster nodes");
  s.w32(0x7c, static_cast<std::uint32_t>(model.cluster_nodes.size() / 20));
  put(model.cluster_nodes, 0x80);
  put(model.cluster_portals, 0x84, true);
  require_records(model.gates.records, 12, "gate records");
  s.w32(0x68, model.gates.count);
  s.w32(0x74, static_cast<std::uint32_t>(model.gates.records.size() / 12));
  put(model.gates.records, 0x70);
  put(model.gates.lists, 0x78, true);
  require_records(model.object_boxes, 24, "object boxes");
  const std::size_t objects = model.object_boxes.size() / 24;
  s.w32(0x40, static_cast<std::uint32_t>(objects));
  put(model.object_boxes, 0x44);
  if (model.object_distances) {
    require_size("objectDistances bytes", model.object_distances->size(), 32 * objects);
  }
  put(model.object_distances ? *model.object_distances : UvdBytes{}, 0x48);
  require_size("objectIds bytes", model.object_ids.size(), 4 * objects);
  put(model.object_ids, 0x50);

  const std::uint64_t table_at = s.pos;
  s.w32(0xa0, static_cast<std::uint32_t>(table_at));
  s.pos = align16(table_at + 4 * n);
  UvdBytes table(4 * n, std::byte{0});
  std::uint32_t leaf_tiles = 0;
  for (std::size_t slot = 0; slot < n; ++slot) {
    if (!model.slots[slot]) {
      continue;
    }
    const UvdBytes tile = write_tile(*model.slots[slot]);
    wr32(table, 4 * slot, static_cast<std::uint32_t>(s.pos));
    s.place(s.pos, tile);
    s.pos += tile.size();
    if (model.slots[slot]->leaf) {
      ++leaf_tiles;
    }
  }
  s.place(table_at, table);
  s.w32(0x8c, leaf_tiles);
  s.w32(0x6c, model.gates.trailer.empty() ? 0u : static_cast<std::uint32_t>(s.pos));
  s.put(model.gates.trailer, true);
  s.pos = align16(s.pos);

  require_size("leaf match rows", model.child.rows.size(), leaf_tiles);
  const std::uint64_t rows_at = s.pos;
  s.w32(0xa4, static_cast<std::uint32_t>(rows_at));
  const std::uint64_t child_at = align16(rows_at + 16ull * leaf_tiles);
  s.w32(0xa8, static_cast<std::uint32_t>(child_at));
  const std::size_t count = model.child.trees.size();
  s.w32(0xac, static_cast<std::uint32_t>(count));
  UvdBytes desc(20 * count, std::byte{0});
  s.pos = align16(child_at + 20ull * count);
  std::size_t next_tree = 0;
  const auto emit_tree = [&](std::size_t i) {
    const auto& t = model.child.trees[i];
    if (!t) {
      return;
    }
    const KdOffsets kd = write_kd(s, *t);
    const std::array<std::uint32_t, 5> words{kd.word, kd.node_off, kd.leaf_off, kd.split_count, kd.split_off};
    for (std::size_t k = 0; k < 5; ++k) wr32(desc, 20 * i + 4 * k, words[k]);
  };
  UvdBytes row_buf(16ull * leaf_tiles, std::byte{0});
  for (std::size_t k = 0; k < model.child.rows.size(); ++k) {
    const UvdLeafRowBytes& row = model.child.rows[k];
    if (!row.block.empty()) {
      while (next_tree < row.trees_before && next_tree < count) emit_tree(next_tree++);
      wr32(row_buf, 16 * k + 4, s.put(row.block, true));
    }
    wr32(row_buf, 16 * k, row.words[0]);
    wr32(row_buf, 16 * k + 8, row.words[1]);
    wr32(row_buf, 16 * k + 12, row.words[2]);
  }
  while (next_tree < count) emit_tree(next_tree++);
  s.place(rows_at, row_buf);
  s.place(child_at, desc);
  const std::uint64_t size = align16(s.pos);
  if (size > std::numeric_limits<std::uint32_t>::max()) {
    fail("model: tome exceeds 4 GiB");
  }
  s.out.resize(size, std::byte{0});
  s.w32(8, static_cast<std::uint32_t>(size));
  s.w32(4, crc32c(std::span<const std::byte>(s.out).subspan(8)));
  return std::move(s.out);
}

}  // namespace fo4::previs
