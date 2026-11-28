#include "fo4/environment/environment.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_relocate_nav.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using fo4::forms::CellNavigationStatus;
using fo4::test::envnode::Node;
using fo4::test::envnode::bytes_of;
using fo4::test::envnode::floats;
using fo4::test::envnode::grp;
using fo4::test::envnode::make_plugin;
using fo4::test::envnode::rec;
using fo4::test::envnode::top;
using fo4::test::envnode::zstring;

constexpr std::uint32_t k_world = 0x800;
constexpr std::uint32_t k_cell_a = 0x810;
constexpr std::uint32_t k_cell_b = 0x814;
constexpr std::uint32_t k_cell_c = 0x815;
constexpr std::uint32_t k_cell_d = 0x816;
constexpr std::uint32_t k_mesh_a = 0x830;
constexpr std::uint32_t k_mesh_b = 0x831;
constexpr std::uint32_t k_mesh_c = 0x832;
constexpr std::uint32_t k_mesh_d = 0x833;
constexpr std::uint32_t k_navi = 0xFF1;
constexpr std::uint32_t k_reference = 0x821;
constexpr std::uint16_t k_none = 0xFFFF;
constexpr float k_dx = 8.0f * 4096.0f;
constexpr float k_dy = 3.0f * 4096.0f;

struct Link {
  std::uint32_t type;
  std::uint32_t target;
  std::uint16_t triangle;
  std::uint8_t edge;
  friend bool operator==(const Link&, const Link&) = default;
};

fo4::FormKey base_key(std::uint32_t id) {
  return {fo4::PluginKey::from_filename("Base.esm"), fo4::LocalFormId{id}};
}

std::uint32_t block_label(std::int32_t x, std::int32_t y, bool block) {
  const auto layout = fo4::forms::exterior_cell_grid_layout(x, y);
  return fo4::forms::exterior_grid_group_label_u32(block ? layout.block_x : layout.subblock_x,
                                                   block ? layout.block_y : layout.subblock_y);
}

std::vector<std::byte> xclc(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  out.u32(0);
  return out.bytes;
}

// Triangle t holds links 3t..3t+2 in slots 0..2; slots without a link are -1 with the boundary bit set.
std::vector<std::byte> navmesh_with_links(std::int16_t grid_x, std::int16_t grid_y, const std::vector<Link>& links) {
  const float bx = static_cast<float>(grid_x) * 4096.0f;
  const float by = static_cast<float>(grid_y) * 4096.0f;
  const std::size_t triangles = std::max<std::size_t>(1, (links.size() + 2) / 3);
  fo4::test::ByteWriter out;
  out.u32(15);
  out.u32(0xA5D3DE3Cu);
  out.u32(k_world);
  out.u16(static_cast<std::uint16_t>(grid_y));
  out.u16(static_cast<std::uint16_t>(grid_x));
  out.u32(3);
  for (const auto& vertex : {std::array<float, 3>{bx + 1.0f, by + 2.0f, 7.0f},
                             std::array<float, 3>{bx + 4000.5f, by + 10.0f, 8.0f},
                             std::array<float, 3>{bx + 4095.25f, by + 4000.0f, 9.0f}}) {
    for (const auto value : vertex) out.f32(value);
  }
  out.u32(static_cast<std::uint32_t>(triangles));
  for (std::size_t t = 0; t < triangles; ++t) {
    for (std::uint16_t i = 0; i < 3; ++i) out.u16(i);
    std::uint32_t flags = 0x800;
    for (std::size_t s = 0; s < 3; ++s) {
      const auto k = t * 3 + s;
      out.u16(k < links.size() ? static_cast<std::uint16_t>(k) : k_none);
      flags |= k < links.size() ? (1u << s) : (0x10000u << s);
    }
    out.f32(4.5f);
    out.u8(0xAA);
    out.u32(flags);
  }
  out.u32(static_cast<std::uint32_t>(links.size()));
  for (const auto& link : links) {
    out.u32(link.type);
    out.u32(link.target);
    out.u16(link.triangle);
    out.u8(link.edge);
  }
  out.u32(0);
  out.u32(0);
  out.u32(0);
  out.u32(1);
  out.f32(bx + 50.5f);
  out.f32(by + 60.25f);
  out.f32(11.0f);
  out.u16(0);
  out.u32(0);
  out.u32(1);
  out.f32(4095.0f);
  out.f32(3990.0f);
  out.f32(bx + 1.0f);
  out.f32(by + 2.0f);
  out.f32(7.0f);
  out.f32(bx + 4095.25f);
  out.f32(by + 4000.0f);
  out.f32(9.0f);
  out.u32(1);
  out.u16(0);
  return out.bytes;
}

const std::vector<Link> k_links_a{{0, k_mesh_b, 0, 0}, {1, k_mesh_a, 0, 1}, {0, k_mesh_d, 0, 2}, {2, k_mesh_b, 1, 1}};
const std::vector<Link> k_links_b{{0, k_mesh_a, 0, 0}, {0, k_mesh_c, 0, 1}, {0, k_mesh_a, 1, 2}, {0, k_mesh_c, 1, 0}};
const std::vector<Link> k_links_c{{0, k_mesh_b, 0, 1}};

struct NaviEntry {
  std::uint32_t mesh;
  std::uint32_t flags;
  std::array<float, 3> location;
  std::vector<std::uint32_t> a;
  std::vector<std::uint32_t> b;
  bool island;
  std::int16_t grid_x;
  std::int16_t grid_y;
};

std::vector<std::byte> navi_entry(const NaviEntry& entry) {
  fo4::test::ByteWriter out;
  out.u32(entry.mesh);
  out.u32(entry.flags);
  for (const auto value : entry.location) out.f32(value);
  out.f32(0.25f);
  out.u32(static_cast<std::uint32_t>(entry.a.size()));
  for (const auto raw : entry.a) out.u32(raw);
  out.u32(static_cast<std::uint32_t>(entry.b.size()));
  for (const auto raw : entry.b) out.u32(raw);
  out.u32(0);
  out.u8(entry.island ? 1 : 0);
  if (entry.island) {
    const auto [x, y, z] = entry.location;
    for (const auto value : {x - 50.0f, y - 50.0f, z - 5.0f, x + 50.0f, y + 50.0f, z + 5.0f}) out.f32(value);
    out.u32(1);
    for (std::uint16_t i = 0; i < 3; ++i) out.u16(i);
    out.u32(2);
    for (const auto value : {x - 50.0f, y - 50.0f, z, x + 50.0f, y + 50.0f, z}) out.f32(value);
  }
  out.u32(0xA5D3DE3Cu);
  out.u32(k_world);
  out.u16(static_cast<std::uint16_t>(entry.grid_y));
  out.u16(static_cast<std::uint16_t>(entry.grid_x));
  return out.bytes;
}

std::vector<std::byte> navi_paths() {
  fo4::test::ByteWriter out;
  out.u32(2);
  out.u32(2);
  out.u32(k_mesh_a);
  out.u32(k_mesh_b);
  out.u32(2);
  out.u32(k_mesh_c);
  out.u32(k_mesh_d);
  out.u32(1);
  out.u32(k_mesh_c);
  out.u32(0);
  return out.bytes;
}

struct BaseOptions {
  bool navi{true};
  bool neighbour_b{true};
  bool corrupt_b{false};
  std::int16_t navi_grid_x{1};
};

Node cell_node(std::uint32_t cell, std::int32_t x, std::int32_t y = 2) {
  return rec("CELL", cell, {{"EDID", zstring("Cell")}, {"DATA", {std::byte{0x02}, std::byte{0x00}}}, {"XCLC", xclc(x, y)}});
}

std::vector<Node> children_of(std::uint32_t cell, std::vector<Node> navmeshes) {
  return {grp(cell, 6, {grp(cell, 9, std::move(navmeshes))})};
}

std::vector<std::byte> base_plugin(const BaseOptions& options = {}) {
  std::vector<Node> cells;
  const auto add = [&](std::uint32_t cell, std::int32_t x, std::uint32_t mesh, const std::vector<Link>& links,
                       bool corrupt = false) {
    auto nvnm = navmesh_with_links(static_cast<std::int16_t>(x), 2, links);
    if (corrupt) nvnm.resize(nvnm.size() - 3);
    cells.push_back(cell_node(cell, x));
    cells.push_back(children_of(cell, {rec("NAVM", mesh, {{"NVNM", nvnm}})})[0]);
  };
  add(k_cell_a, 1, k_mesh_a, k_links_a);
  if (options.neighbour_b) add(k_cell_b, 2, k_mesh_b, k_links_b, options.corrupt_b);
  add(k_cell_c, 3, k_mesh_c, k_links_c);
  add(k_cell_d, 4, k_mesh_d, {{0, k_mesh_c, 0, 0}});
  std::vector<Node> groups{
      top("WRLD", {rec("WRLD", k_world, {{"EDID", zstring("World")}}),
                   grp(k_world, 1, {grp(block_label(1, 2, true), 4, {grp(block_label(1, 2, false), 5, std::move(cells))})})})};
  if (options.navi) {
    const auto x = 4096.0f;
    const auto y = 8192.0f;
    std::vector<fo4::test::envnode::Sub> subs{{"NVER", bytes_of({15})}};
    subs.push_back({"NVMI", navi_entry({k_mesh_a, 0x20, {x + 100.0f, y + 200.0f, 50.0f}, {k_mesh_b}, {k_mesh_d}, true,
                                        options.navi_grid_x, 2})});
    subs.push_back({"NVMI", navi_entry({k_mesh_b, 0, {2 * x + 10.0f, y + 20.0f, 30.0f}, {k_mesh_c}, {k_mesh_a}, false, 2, 2})});
    subs.push_back({"NVMI", navi_entry({k_mesh_c, 0, {3 * x + 10.0f, y + 20.0f, 30.0f}, {k_mesh_b}, {}, false, 3, 2})});
    subs.push_back({"NVMI", navi_entry({k_mesh_d, 0, {4 * x + 10.0f, y + 20.0f, 30.0f}, {}, {}, false, 4, 2})});
    subs.push_back({"NVPP", navi_paths()});
    groups.push_back(top("NAVI", {rec("NAVI", k_navi, std::move(subs))}));
  }
  return make_plugin({}, groups);
}

std::vector<std::byte> patch_plugin(std::int32_t cell_x, std::int32_t cell_y, std::int16_t mesh_grid_x,
                                    std::int16_t mesh_grid_y, const std::vector<Link>& links, bool with_mesh = true) {
  std::vector<Node> temporary{rec("REFR", k_reference,
                                  {{"NAME", bytes_of({0x2100})}, {"DATA", floats({10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f})}})};
  if (with_mesh) temporary.push_back(rec("NAVM", k_mesh_a, {{"NVNM", navmesh_with_links(mesh_grid_x, mesh_grid_y, links)}}));
  std::vector<Node> cell{cell_node(k_cell_a, cell_x, cell_y), children_of(k_cell_a, std::move(temporary))[0]};
  return make_plugin({"Base.esm"}, {top("WRLD", {rec("WRLD", k_world, {{"EDID", zstring("World")}}),
                                                  grp(k_world, 1, {grp(block_label(cell_x, cell_y, true), 4,
                                                                       {grp(block_label(cell_x, cell_y, false), 5, std::move(cell))})})})});
}

fo4::Plugin read_patch(std::int32_t cell_x, std::int32_t cell_y, std::int16_t mesh_grid_x, std::int16_t mesh_grid_y,
                       const std::vector<Link>& links = k_links_a, bool with_mesh = true) {
  return fo4::read_plugin(patch_plugin(cell_x, cell_y, mesh_grid_x, mesh_grid_y, links, with_mesh), "Patch.esp");
}

fo4::env::Environment make_env(const BaseOptions& options = {}) {
  return fo4::env::Environment::from_plugins({fo4::read_plugin(base_plugin(options), "Base.esm")});
}

fo4::forms::CellNavigationResult relocate_navigation(fo4::Plugin& patch, const fo4::env::Environment& env) {
  return fo4::forms::relocate_cell_navigation(patch, env, base_key(k_cell_a), 1, 2, 9, 5);
}

fo4::Plugin moved_patch(const std::vector<Link>& links = k_links_a) {
  auto patch = read_patch(1, 2, 1, 2, links);
  EXPECT_EQ(fo4::forms::relocate_exterior_cell(patch, base_key(k_cell_a), 9, 5).status,
            fo4::forms::CellRelocateStatus::relocated);
  return patch;
}

std::vector<std::byte> subrecord_data(const fo4::Record& record, std::string_view signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) return subrecord.data;
  }
  return {};
}

std::vector<std::byte> nvnm_in(const fo4::Plugin& plugin, std::uint32_t mesh) {
  const auto* record = fo4::find_record(plugin, base_key(mesh));
  return record == nullptr ? std::vector<std::byte>{} : subrecord_data(*record, "NVNM");
}

template <typename T>
T value_at(const std::vector<std::byte>& data, std::size_t offset) {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

template <typename T>
void set_at(std::vector<std::byte>& data, std::size_t offset, T value) {
  std::memcpy(data.data() + offset, &value, sizeof(T));
}

constexpr std::size_t k_triangles_at = 60;

struct Parsed {
  std::vector<std::array<std::uint16_t, 3>> edges;
  std::vector<std::uint32_t> flags;
  std::vector<Link> links;
};

Parsed parse_mesh(const std::vector<std::byte>& nvnm) {
  Parsed out;
  const auto triangles = value_at<std::uint32_t>(nvnm, k_triangles_at - 4);
  for (std::uint32_t t = 0; t < triangles; ++t) {
    const auto base = k_triangles_at + t * 21;
    out.edges.push_back({value_at<std::uint16_t>(nvnm, base + 6), value_at<std::uint16_t>(nvnm, base + 8),
                         value_at<std::uint16_t>(nvnm, base + 10)});
    out.flags.push_back(value_at<std::uint32_t>(nvnm, base + 17));
  }
  const auto links_at = k_triangles_at + triangles * 21;
  const auto count = value_at<std::uint32_t>(nvnm, links_at);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto base = links_at + 4 + i * 11;
    out.links.push_back({value_at<std::uint32_t>(nvnm, base), value_at<std::uint32_t>(nvnm, base + 4),
                         value_at<std::uint16_t>(nvnm, base + 8), value_at<std::uint8_t>(nvnm, base + 10)});
  }
  return out;
}

// The only bytes that may differ are the named triangle slots and flags, the link count and the dropped link records.
std::vector<std::byte> with_edits(std::vector<std::byte> nvnm,
                                  const std::vector<std::tuple<std::size_t, std::size_t, std::uint16_t, std::uint32_t>>& triangles,
                                  const std::vector<std::size_t>& dropped) {
  const auto count = value_at<std::uint32_t>(nvnm, k_triangles_at - 4);
  for (const auto& [t, slot, value, flags] : triangles) {
    set_at(nvnm, k_triangles_at + t * 21 + 6 + slot * 2, value);
    set_at(nvnm, k_triangles_at + t * 21 + 17, flags);
  }
  const auto links_at = k_triangles_at + count * 21;
  const auto total = value_at<std::uint32_t>(nvnm, links_at);
  for (auto it = dropped.rbegin(); it != dropped.rend(); ++it) {
    const auto first = nvnm.begin() + static_cast<std::ptrdiff_t>(links_at + 4 + *it * 11);
    nvnm.erase(first, first + 11);
  }
  set_at(nvnm, links_at, static_cast<std::uint32_t>(total - dropped.size()));
  return nvnm;
}

struct NaviView {
  bool found{false};
  std::array<float, 3> location{};
  std::vector<std::uint32_t> a;
  std::vector<std::uint32_t> b;
  std::array<float, 6> island{};
  std::array<float, 6> island_vertices{};
  std::int16_t grid_y{0};
  std::int16_t grid_x{0};
};

NaviView navi_of(const fo4::Plugin& plugin, std::uint32_t mesh) {
  NaviView view;
  const auto* record = fo4::find_record(plugin, base_key(k_navi));
  if (record == nullptr) return view;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != fo4::Signature::from_string("NVMI")) continue;
    const auto& data = subrecord.data;
    if (value_at<std::uint32_t>(data, 0) != mesh) continue;
    view.found = true;
    for (std::size_t i = 0; i < 3; ++i) view.location[i] = value_at<float>(data, 8 + i * 4);
    std::size_t cursor = 24;
    const auto a = value_at<std::uint32_t>(data, cursor);
    for (std::uint32_t i = 0; i < a; ++i) view.a.push_back(value_at<std::uint32_t>(data, cursor + 4 + i * 4));
    cursor += 4 + a * 4;
    const auto b = value_at<std::uint32_t>(data, cursor);
    for (std::uint32_t i = 0; i < b; ++i) view.b.push_back(value_at<std::uint32_t>(data, cursor + 4 + i * 4));
    cursor += 4 + b * 4;
    cursor += 4 + value_at<std::uint32_t>(data, cursor) * 8;
    if (value_at<std::uint8_t>(data, cursor++) != 0) {
      for (std::size_t i = 0; i < 6; ++i) view.island[i] = value_at<float>(data, cursor + i * 4);
      cursor += 24;
      cursor += 4 + value_at<std::uint32_t>(data, cursor) * 6;
      cursor += 4;
      for (std::size_t i = 0; i < 6; ++i) view.island_vertices[i] = value_at<float>(data, cursor + i * 4);
      cursor += 24;
    }
    view.grid_y = value_at<std::int16_t>(data, cursor + 8);
    view.grid_x = value_at<std::int16_t>(data, cursor + 10);
  }
  return view;
}

std::size_t count_records(const fo4::Plugin& plugin, std::string_view signature) {
  std::size_t count = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string(signature)) ++count;
  });
  return count;
}

}  // namespace

// A navmesh left linked to its old neighbours would route the game across a gap where no navmesh exists.
TEST(CellRelocateNavigation, DropsTheMovedMeshLinksToOtherNavmeshesAndResetsTheirTriangleSlots) {
  auto patch = moved_patch();
  const auto before = nvnm_in(patch, k_mesh_a);
  const auto env = make_env();

  const auto result = relocate_navigation(patch, env);

  ASSERT_EQ(result.status, CellNavigationStatus::updated);
  EXPECT_EQ(result.links_removed, 3u);
  EXPECT_EQ(result.back_links_removed, 2u);
  const auto after = nvnm_in(patch, k_mesh_a);
  EXPECT_EQ(parse_mesh(after).links, (std::vector<Link>{{1, k_mesh_a, 0, 1}}));
  EXPECT_EQ(after, with_edits(before, {{0, 0, k_none, 0x802}, {0, 1, 0, 0x802}, {0, 2, k_none, 0x802}, {1, 0, k_none, 0x60800}}, {0, 2, 3}));
}

// A neighbour that kept its link to a moved navmesh would name triangles of a mesh that is no longer adjacent.
TEST(CellRelocateNavigation, OverridesTheNeighbourIntoThePatchAndDropsOnlyItsLinksBack) {
  auto patch = moved_patch();
  const auto env = make_env();
  const auto base = fo4::read_plugin(base_plugin(), "Base.esm");
  ASSERT_EQ(nvnm_in(patch, k_mesh_b).size(), 0u);

  const auto result = relocate_navigation(patch, env);

  ASSERT_EQ(result.status, CellNavigationStatus::updated);
  EXPECT_EQ(result.neighbour_overrides, std::vector<fo4::FormKey>{base_key(k_mesh_b)});
  const auto parsed = parse_mesh(nvnm_in(patch, k_mesh_b));
  EXPECT_EQ(parsed.links, (std::vector<Link>{{0, k_mesh_c, 0, 1}, {0, k_mesh_c, 1, 0}}));
  EXPECT_EQ(parsed.edges[0], (std::array<std::uint16_t, 3>{k_none, 0, k_none}));
  EXPECT_EQ(parsed.flags[0], 0x802u);
  EXPECT_EQ(parsed.edges[1], (std::array<std::uint16_t, 3>{1, k_none, k_none}));
  EXPECT_EQ(parsed.flags[1], 0x60801u);
  EXPECT_EQ(nvnm_in(patch, k_mesh_b),
            with_edits(nvnm_in(base, k_mesh_b), {{0, 0, k_none, 0x802}, {0, 1, 0, 0x802}, {0, 2, k_none, 0x802}, {1, 0, 1, 0x60801}}, {0, 2}));
  EXPECT_NE(fo4::find_record(patch, base_key(k_cell_b)), nullptr);
}

// Overriding every neighbour of the cell would add records the move never changed, and lose to other mods' versions.
TEST(CellRelocateNavigation, LeavesUnlinkedAndOneWayNeighboursAndUnrelatedRecordsOutOfThePatch) {
  auto patch = moved_patch();
  const auto env = make_env();
  const auto reference_before = subrecord_data(*fo4::find_record(patch, base_key(k_reference)), "DATA");

  const auto result = relocate_navigation(patch, env);

  ASSERT_EQ(result.status, CellNavigationStatus::updated);
  EXPECT_EQ(fo4::find_record(patch, base_key(k_mesh_c)), nullptr);
  EXPECT_EQ(fo4::find_record(patch, base_key(k_mesh_d)), nullptr);
  EXPECT_EQ(fo4::find_record(patch, base_key(k_cell_c)), nullptr);
  EXPECT_EQ(fo4::find_record(patch, base_key(k_cell_d)), nullptr);
  EXPECT_EQ(subrecord_data(*fo4::find_record(patch, base_key(k_reference)), "DATA"), reference_before);
  EXPECT_EQ(count_records(patch, "NAVM"), 2u);
  EXPECT_FALSE(navi_of(patch, k_mesh_c).found);
  EXPECT_FALSE(navi_of(patch, k_mesh_d).found);
}

// A stale entry for the moved navmesh keeps the old location and grid, so the navigation index would place it in the old cell.
TEST(CellRelocateNavigation, MovesTheNaviEntriesOfTheTouchedNavmeshesAndLeavesThePathsAlone) {
  auto patch = moved_patch();
  const auto env = make_env();

  const auto result = relocate_navigation(patch, env);

  ASSERT_EQ(result.status, CellNavigationStatus::updated);
  EXPECT_EQ(result.navi_entries_updated, 2u);
  EXPECT_EQ(result.navi_entries_absent, 0u);
  EXPECT_EQ(result.stale_preferred_paths, 1u);
  const auto moved = navi_of(patch, k_mesh_a);
  ASSERT_TRUE(moved.found);
  EXPECT_EQ(moved.location, (std::array<float, 3>{4196.0f + k_dx, 8392.0f + k_dy, 50.0f}));
  EXPECT_EQ(moved.island, (std::array<float, 6>{4146.0f + k_dx, 8342.0f + k_dy, 45.0f, 4246.0f + k_dx, 8442.0f + k_dy, 55.0f}));
  EXPECT_EQ(moved.island_vertices, (std::array<float, 6>{4146.0f + k_dx, 8342.0f + k_dy, 50.0f, 4246.0f + k_dx, 8442.0f + k_dy, 50.0f}));
  EXPECT_EQ(moved.grid_x, 9);
  EXPECT_EQ(moved.grid_y, 5);
  EXPECT_EQ(moved.a, std::vector<std::uint32_t>{});
  EXPECT_EQ(moved.b, std::vector<std::uint32_t>{});
  const auto neighbour = navi_of(patch, k_mesh_b);
  ASSERT_TRUE(neighbour.found);
  EXPECT_EQ(neighbour.location, (std::array<float, 3>{8202.0f, 8212.0f, 30.0f}));
  EXPECT_EQ(neighbour.a, std::vector<std::uint32_t>{k_mesh_c});
  EXPECT_EQ(neighbour.b, std::vector<std::uint32_t>{});
  EXPECT_EQ(neighbour.grid_x, 2);
  const auto* record = fo4::find_record(patch, base_key(k_navi));
  const auto base = fo4::read_plugin(base_plugin(), "Base.esm");
  const auto* source = fo4::find_record(base, base_key(k_navi));
  ASSERT_NE(record, nullptr);
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(subrecord_data(*record, "NVPP"), subrecord_data(*source, "NVPP"));
  EXPECT_EQ(subrecord_data(*record, "NVER"), subrecord_data(*source, "NVER"));
  EXPECT_EQ(count_records(patch, "NAVI"), 1u);
}

// A cell without navmeshes has nothing to edit and must not turn into a changed plugin.
TEST(CellRelocateNavigation, CellWithoutNavmeshesChangesNothing) {
  auto patch = read_patch(9, 5, 9, 5, {}, false);
  const auto before = fo4::write_plugin(patch);
  const auto env = make_env();

  EXPECT_EQ(relocate_navigation(patch, env).status, CellNavigationStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(patch), before);
}

// With no NAVI anywhere there is no entry to rewrite, so an isolated navmesh is not an edit either.
TEST(CellRelocateNavigation, AnIsolatedNavmeshWithoutNaviEntryChangesNothing) {
  auto patch = moved_patch({});
  const auto before = fo4::write_plugin(patch);
  const auto env = make_env({.navi = false});

  const auto result = relocate_navigation(patch, env);

  EXPECT_EQ(result.status, CellNavigationStatus::unchanged);
  EXPECT_EQ(result.navi_entries_absent, 1u);
  EXPECT_EQ(fo4::write_plugin(patch), before);
}

// An isolated navmesh still has an entry that names the old grid, with no neighbour to copy.
TEST(CellRelocateNavigation, AnIsolatedNavmeshStillMovesItsNaviEntryWithoutTouchingNeighbours) {
  auto patch = moved_patch({});
  const auto env = make_env();

  const auto result = relocate_navigation(patch, env);

  ASSERT_EQ(result.status, CellNavigationStatus::updated);
  EXPECT_EQ(result.links_removed, 0u);
  EXPECT_EQ(result.neighbour_overrides.size(), 0u);
  EXPECT_EQ(count_records(patch, "NAVM"), 1u);
  const auto moved = navi_of(patch, k_mesh_a);
  ASSERT_TRUE(moved.found);
  EXPECT_EQ(moved.grid_x, 9);
  EXPECT_EQ(moved.a, std::vector<std::uint32_t>{});
  EXPECT_EQ(moved.b, std::vector<std::uint32_t>{});
}

// A refusal half way through must not leave the neighbour override or the NAVI copy behind.
TEST(CellRelocateNavigation, RefusalsLeaveThePatchByteIdentical) {
  struct Case {
    BaseOptions options;
    CellNavigationStatus status;
  };
  const std::vector<Case> cases{
      {{.neighbour_b = false}, CellNavigationStatus::neighbour_missing},
      {{.corrupt_b = true}, CellNavigationStatus::malformed},
      {{.navi_grid_x = 7}, CellNavigationStatus::navi_unsupported},
  };
  for (const auto& entry : cases) {
    auto patch = moved_patch();
    const auto before = fo4::write_plugin(patch);
    const auto env = make_env(entry.options);

    EXPECT_EQ(relocate_navigation(patch, env).status, entry.status);
    EXPECT_EQ(fo4::write_plugin(patch), before);
  }
}

// A navmesh that still names the old grid means relocate_exterior_cell did not run, and rewriting NAVI would then be wrong.
TEST(CellRelocateNavigation, RefusesAMeshOrCellThatIsNotAtTheNewGrid) {
  auto stale_mesh = read_patch(9, 5, 1, 2);
  auto stale_cell = read_patch(1, 2, 1, 2);
  const auto env = make_env();
  const auto before_mesh = fo4::write_plugin(stale_mesh);
  const auto before_cell = fo4::write_plugin(stale_cell);

  EXPECT_EQ(relocate_navigation(stale_mesh, env).status, CellNavigationStatus::not_relocated);
  EXPECT_EQ(relocate_navigation(stale_cell, env).status, CellNavigationStatus::not_relocated);
  EXPECT_EQ(fo4::write_plugin(stale_mesh), before_mesh);
  EXPECT_EQ(fo4::write_plugin(stale_cell), before_cell);
}

TEST(CellRelocateNavigation, ReportsAMissingCellAndAnUnmovedCell) {
  auto patch = moved_patch();
  const auto env = make_env();
  const auto before = fo4::write_plugin(patch);

  EXPECT_EQ(fo4::forms::relocate_cell_navigation(patch, env, base_key(0x999), 1, 2, 9, 5).status,
            CellNavigationStatus::cell_missing);
  EXPECT_EQ(fo4::forms::relocate_cell_navigation(patch, env, base_key(k_cell_a), 9, 5, 9, 5).status,
            CellNavigationStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(patch), before);
}
