#pragma once

#include "env_node_fixture.hpp"

#include "fo4/nif/nif_import.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// Invariant: the object-lane C API tests and the LOD determinism test must generate from the same fixture bytes.
namespace fo4::test::envnode::capi_lod {

inline constexpr std::string_view k_object_lod_meshes[] = {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"};

// Invariant: MNAM holds one four-slot LOD mesh list, 260 bytes per level slot, in level order.
inline std::vector<std::byte> object_lod_slots() {
  std::vector<std::byte> out;
  for (const auto mesh : k_object_lod_meshes) {
    auto slot = zstring(mesh);
    slot.resize(260, std::byte{0});
    out.insert(out.end(), slot.begin(), slot.end());
  }
  return out;
}

// Invariant: two_tiles adds a reference in cell 40, so every shipped level of the 0,0 stride-64 grid has two origins.
enum class ObjectLodRefs { one, two_tiles };

// Invariant: cell 0,0 holds one reference to the STAT, so the object lane has a candidate to place.
inline std::vector<std::byte> object_lod_plugin(ObjectLodRefs refs = ObjectLodRefs::one) {
  std::vector<Node> placed{rec("REFR", 0x820,
                               {{"NAME", bytes_of({std::uint32_t{0x840}})},
                                {"DATA", floats({100.0f, 200.0f, 300.0f, 0.0f, 0.0f, 90.0f})}})};
  // Invariant: the second reference sits at a different offset inside its cell, so swapped tiles write different bytes.
  if (refs == ObjectLodRefs::two_tiles) {
    placed.push_back(rec("REFR", 0x821,
                         {{"NAME", bytes_of({std::uint32_t{0x840}})},
                          {"DATA", floats({40.0f * 4096.0f + 1500.0f, 2500.0f, 700.0f, 0.0f, 0.0f, 0.0f})}}));
  }
  std::vector<Node> exterior{rec("CELL", 0x802, {{"XCLC", xclc(0, 0)}}),
                             grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {{"VHGT", vhgt(10.0f)}})})})};
  std::vector<Node> world_children{
      rec("CELL", 0x801, {{"DATA", {std::byte{0}}}}, 0x400),
      grp(0x801, 6, {grp(0x801, 8, std::move(placed))}),
      grp(0, 4, {grp(0, 5, std::move(exterior))})};
  std::vector<Node> base{
      top("STAT", {rec("STAT", 0x840, {{"EDID", zstring("Rock")}, {"MNAM", object_lod_slots()}})})};
  base.push_back(top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                              grp(0x800, 1, std::move(world_children))}));
  return make_plugin({}, base);
}

inline std::vector<std::byte> object_lod_mesh() {
  const std::string obj =
      "v 0 0 0\nv 128 0 0\nv 0 128 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
  fo4::nif::ImportOptions options;
  return fo4::nif::import_obj(obj, options);
}

// Invariant: both lanes need the same plugin, meshes and terrain folder, so the setup lives here.
inline void object_lod_prepare(const Fixture& fixture, ObjectLodRefs refs = ObjectLodRefs::one) {
  const auto plugin = object_lod_plugin(refs);
  {
    std::ofstream out(fixture.root / "Test.esp", std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(plugin.data()), static_cast<std::streamsize>(plugin.size()));
  }
  const auto mesh = object_lod_mesh();
  std::filesystem::create_directories(fixture.root / "Meshes");
  for (const auto mesh_name : k_object_lod_meshes) {
    std::ofstream out(fixture.root / "Meshes" / std::string{mesh_name}, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(mesh.data()), static_cast<std::streamsize>(mesh.size()));
  }
  std::filesystem::create_directories(fixture.root / "Meshes" / "Terrain" / "TestWorld");
}

}  // namespace fo4::test::envnode::capi_lod
