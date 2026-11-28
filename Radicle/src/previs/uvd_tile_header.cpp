#include "fo4/previs/uvd_tile_header.hpp"

#include "uvd_tile_internal.hpp"

#include <algorithm>
#include <string>

namespace fo4::previs {

using namespace detail;

namespace {

using RunPairs = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

std::uint64_t read_packed_value(const UvdBytes& buf, std::uint64_t index, std::uint32_t width) {
  std::uint64_t v = 0;
  for (std::uint32_t b = 0; b < width; ++b) {
    const std::uint64_t bit = index * width + b;
    v |= ((std::to_integer<std::uint64_t>(buf[bit >> 3u]) >> (bit & 7u)) & 1u) << b;
  }
  return v;
}

void write_packed_value(UvdBytes& buf, std::uint64_t index, std::uint32_t width, std::uint64_t value) {
  for (std::uint32_t b = 0; b < width; ++b) {
    if (((value >> b) & 1u) == 0) {
      continue;
    }
    const std::uint64_t bit = index * width + b;
    buf[bit >> 3u] |= static_cast<std::byte>(1u << (bit & 7u));
  }
}

RunPairs decode_run(const UvdRunListBytes& list, std::uint32_t w0, std::uint32_t w1) {
  const std::uint32_t width = w0 + w1;
  if (static_cast<std::uint64_t>(list.count) * width > list.bytes.size() * 8ull) {
    fail("run list is shorter than its entries");
  }
  if (width == 0 && list.count != 0) {
    fail("run list with zero width and a nonzero count");
  }
  RunPairs pairs(list.count);
  for (std::uint32_t i = 0; i < list.count; ++i) {
    const std::uint64_t v = read_packed_value(list.bytes, i, width);
    pairs[i] = {static_cast<std::uint32_t>(v & ((std::uint64_t{1} << w0) - 1)), static_cast<std::uint32_t>(v >> w0)};
  }
  return pairs;
}

UvdRunListBytes encode_run(const RunPairs& entries, std::uint32_t w0, std::uint32_t w1) {
  UvdRunListBytes list;
  list.count = static_cast<std::uint32_t>(entries.size());
  list.bytes.assign(uvd_run_list_bytes(entries.size(), w0 + w1), std::byte{0});
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if ((static_cast<std::uint64_t>(entries[i].first) >> w0) != 0 || (static_cast<std::uint64_t>(entries[i].second) >> w1) != 0) {
      fail("model: run list entry wider than its widths");
    }
    write_packed_value(list.bytes, i, w0 + w1, entries[i].first | (static_cast<std::uint64_t>(entries[i].second) << w0));
  }
  return list;
}

template <std::size_t N>
std::vector<std::array<float, N>> decode_floats(const UvdBytes& buf, std::size_t count, const char* label) {
  if (buf.size() < count * N * 4) {
    fail(std::string(label) + " is shorter than its records");
  }
  std::vector<std::array<float, N>> out(count);
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t k = 0; k < N; ++k) out[i][k] = rdf(buf, 4 * (i * N + k));
  }
  return out;
}

template <std::size_t N>
UvdBytes encode_floats(const std::vector<std::array<float, N>>& records) {
  UvdBytes buf(records.size() * N * 4, std::byte{0});
  for (std::size_t i = 0; i < records.size(); ++i) {
    for (std::size_t k = 0; k < N; ++k) wrf(buf, 4 * (i * N + k), records[i][k]);
  }
  return buf;
}

}  // namespace

UvdSlotPaths uvd_slot_paths(std::span<const std::uint8_t> symbols) {
  const std::size_t n = symbols.size();
  std::vector<std::int64_t> parent(n, -1);
  std::vector<std::uint8_t> side(n, 0);
  std::size_t next = 1;
  for (std::size_t i = 0; i < n; ++i) {
    if (symbols[i] == 3) {
      continue;
    }
    if (next < n) {
      parent[next] = static_cast<std::int64_t>(i);
      side[next] = 0;
    }
    if (next + 1 < n) {
      parent[next + 1] = static_cast<std::int64_t>(i);
      side[next + 1] = 1;
    }
    next += 2;
  }
  std::vector<std::uint32_t> depth(n, 0);
  std::uint32_t max_depth = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (parent[i] >= 0) {
      depth[i] = depth[static_cast<std::size_t>(parent[i])] + 1;
    }
    max_depth = std::max(max_depth, depth[i]);
  }
  if (max_depth > 62) {
    fail("slot paths: a tree deeper than 62 levels");
  }
  UvdSlotPaths paths;
  paths.path_bits = max_depth + 1;
  paths.bytes.assign(uvd_slot_path_bytes(n, paths.path_bits), std::byte{0});
  std::vector<std::uint8_t> steps;
  for (std::size_t i = 0; i < n; ++i) {
    steps.clear();
    for (std::int64_t j = static_cast<std::int64_t>(i); parent[static_cast<std::size_t>(j)] >= 0; j = parent[static_cast<std::size_t>(j)]) {
      steps.push_back(side[static_cast<std::size_t>(j)]);
    }
    std::reverse(steps.begin(), steps.end());
    const std::uint32_t len = static_cast<std::uint32_t>(steps.size());
    std::uint64_t v = std::uint64_t{1} << (paths.path_bits - 1 - len);
    for (std::uint32_t k = 0; k < len; ++k) {
      if (steps[k] == 0) {
        v += std::uint64_t{1} << (paths.path_bits - len + k);
      }
    }
    write_packed_value(paths.bytes, i, paths.path_bits, v);
  }
  return paths;
}

UvdHeader decode_uvd_header(const UvdTileModel& model) {
  const std::size_t n = model.slots.size();
  if (model.object_boxes.size() % 24 != 0 || model.cluster_nodes.size() % 20 != 0 || model.gates.records.size() % 12 != 0) {
    fail("header regions are not whole records");
  }
  const std::size_t objects = model.object_boxes.size() / 24;
  UvdHeader h;
  h.banner = model.banner;
  h.lod_base = model.lod_base;
  h.reserved10 = model.reserved10;
  h.world_min = model.world_min;
  h.world_max = model.world_max;
  h.widths = model.widths;
  h.top_symbols = decode_uvd_symbols(model.top_kd.nodes, n);
  if (model.top_kd.splits.size() % 4 != 0) {
    fail("top kd splits are not whole floats");
  }
  h.top_splits.resize(model.top_kd.splits.size() / 4);
  for (std::size_t i = 0; i < h.top_splits.size(); ++i) h.top_splits[i] = rdf(model.top_kd.splits, 4 * i);
  h.lod.resize(n);
  for (std::size_t i = 0; i < n; ++i) h.lod[i] = rdf(model.lod, 4 * i);
  h.objects.boxes = decode_floats<6>(model.object_boxes, objects, "object boxes");
  if (model.object_distances) {
    h.objects.distances = decode_floats<8>(*model.object_distances, objects, "object distances");
  }
  h.objects.ids.resize(objects);
  if (model.object_ids.size() < 4 * objects) {
    fail("object ids are shorter than the object count");
  }
  for (std::size_t i = 0; i < objects; ++i) h.objects.ids[i] = rd32(model.object_ids, 4 * i);
  h.run_a = decode_run(model.run_list_a, model.widths[0], model.widths[1]);
  h.run_b = decode_run(model.run_list_b, model.widths[2], model.widths[3]);
  h.clusters.resize(model.cluster_nodes.size() / 20);
  for (std::size_t i = 0; i < h.clusters.size(); ++i) {
    const std::size_t o = 20 * i;
    const auto q = [&](std::size_t k) { return rd16(model.cluster_nodes, o + 8 + 2 * k); };
    UvdCluster& c = h.clusters[i];
    c.portal_start = rd32(model.cluster_nodes, o);
    c.portal_count = rd32(model.cluster_nodes, o + 4);
    c.x = {q(0), q(5)};
    c.y = {q(1), q(2)};
    c.z = {q(3), q(4)};
  }
  h.cluster_portals = model.cluster_portals;
  h.gates.count = model.gates.count;
  h.gates.records.resize(model.gates.records.size() / 12);
  for (std::size_t i = 0; i < h.gates.records.size(); ++i) {
    for (std::size_t k = 0; k < 3; ++k) h.gates.records[i][k] = rd32(model.gates.records, 12 * i + 4 * k);
  }
  h.gates.lists = model.gates.lists;
  h.gates.trailer = model.gates.trailer;
  h.child_trees.resize(model.child.trees.size());
  for (std::size_t i = 0; i < model.child.trees.size(); ++i) {
    if (model.child.trees[i]) {
      h.child_trees[i] = decode_uvd_kd(*model.child.trees[i]);
    }
  }
  h.child_rows = model.child.rows;
  return h;
}

UvdTileModel encode_uvd_header(const UvdHeader& h, const UvdTileModel& model) {
  const std::size_t n = model.slots.size();
  if (h.top_symbols.size() != n || h.lod.size() != n) {
    fail("model: header slot arrays differ from the slot count");
  }
  UvdTileModel out = model;
  for (std::size_t i = 0; i < n; ++i) {
    if (out.slots[i]) {
      out.slots[i]->leaf = h.top_symbols[i] == 3;
    }
  }
  const UvdSlotPaths paths = uvd_slot_paths(h.top_symbols);
  out.cell_starts.assign(4 * (n + 1), std::byte{0});
  std::uint32_t run = 0;
  for (std::size_t i = 0; i < n; ++i) {
    wr32(out.cell_starts, 4 * i, run);
    run += out.slots[i] ? static_cast<std::uint32_t>(out.slots[i]->cells.size() / 36) : 0u;
  }
  wr32(out.cell_starts, 4 * n, run);
  out.lod.assign(4 * n, std::byte{0});
  for (std::size_t i = 0; i < n; ++i) wrf(out.lod, 4 * i, out.slots[i] ? h.lod[i] : -1.0f);
  out.top_kd.nodes = encode_uvd_nodes(h.top_symbols);
  out.top_kd.splits.assign(4 * h.top_splits.size(), std::byte{0});
  for (std::size_t i = 0; i < h.top_splits.size(); ++i) wrf(out.top_kd.splits, 4 * i, h.top_splits[i]);
  out.banner = h.banner;
  out.lod_base = h.lod_base;
  out.reserved10 = h.reserved10;
  out.world_min = h.world_min;
  out.world_max = h.world_max;
  out.widths = h.widths;
  out.path_bits = paths.path_bits;
  out.slot_paths = paths.bytes;
  out.object_boxes = encode_floats<6>(h.objects.boxes);
  out.object_distances.reset();
  if (h.objects.distances) {
    out.object_distances = encode_floats<8>(*h.objects.distances);
  }
  out.object_ids.assign(4 * h.objects.ids.size(), std::byte{0});
  for (std::size_t i = 0; i < h.objects.ids.size(); ++i) wr32(out.object_ids, 4 * i, h.objects.ids[i]);
  out.run_list_a = encode_run(h.run_a, h.widths[0], h.widths[1]);
  out.run_list_b = encode_run(h.run_b, h.widths[2], h.widths[3]);
  out.cluster_nodes.assign(20 * h.clusters.size(), std::byte{0});
  for (std::size_t i = 0; i < h.clusters.size(); ++i) {
    const std::size_t o = 20 * i;
    const UvdCluster& c = h.clusters[i];
    wr32(out.cluster_nodes, o, c.portal_start);
    wr32(out.cluster_nodes, o + 4, c.portal_count);
    const std::array<std::uint16_t, 6> box{c.x[0], c.y[0], c.y[1], c.z[0], c.z[1], c.x[1]};
    for (std::size_t k = 0; k < 6; ++k) wr16(out.cluster_nodes, o + 8 + 2 * k, box[k]);
  }
  out.cluster_portals = h.cluster_portals;
  out.gates.count = h.gates.count;
  out.gates.records.assign(12 * h.gates.records.size(), std::byte{0});
  for (std::size_t i = 0; i < h.gates.records.size(); ++i) {
    for (std::size_t k = 0; k < 3; ++k) wr32(out.gates.records, 12 * i + 4 * k, h.gates.records[i][k]);
  }
  out.gates.lists = h.gates.lists;
  out.gates.trailer = h.gates.trailer;
  out.child.trees.assign(h.child_trees.size(), std::nullopt);
  for (std::size_t i = 0; i < h.child_trees.size(); ++i) {
    if (h.child_trees[i]) {
      out.child.trees[i] = encode_uvd_kd(*h.child_trees[i]);
    }
  }
  out.child.rows = h.child_rows;
  return out;
}

}  // namespace fo4::previs
