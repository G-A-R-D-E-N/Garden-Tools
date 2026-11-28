#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/nif.hpp"
#include "material_file_builder.hpp"
#include "nif_build.hpp"
#include "temp_data_dir.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace resolved_fixture {

struct Scene {
  std::string material;
  std::vector<std::string> slots;
  std::string root;
  bool alpha{false};
  std::uint16_t alpha_flags{0xED};
  std::uint8_t threshold{77};
};

struct Loaded {
  fo4::nif::NifFile file;
  fo4::nif::Shape shape;
};

inline Loaded load(const std::vector<std::byte>& bytes) {
  auto file = fo4::nif::read_nif(bytes);
  auto shapes = fo4::nif::read_shapes(file);
  auto shape = shapes.at(0);
  return Loaded{std::move(file), std::move(shape)};
}

inline std::vector<std::byte> lighting_bytes(const Scene& scene) {
  using namespace nif_build;
  LightingFields fields;
  fields.shader_type = 0;
  fields.name = scene.material.empty() ? -1 : 1;
  fields.texture_set = 3;
  fields.root_material = scene.root.empty() ? -1 : 3;
  std::vector<Block> blocks{named_node_block("NiNode", 2, fo4::nif::Transform{}, {1}),
                            shape_with_refs("BSTriShape", 0, fo4::nif::Transform{}, k_full, 16, -1, 2,
                                            scene.alpha ? 4 : -1, triangle_vertices(), {0, 1, 2}),
                            full_lighting_shader_block(fields), texture_set_record(scene.slots),
                            alpha_block(scene.alpha_flags, scene.threshold)};
  std::vector<std::string> strings{"Tri", scene.material, "Root"};
  if (!scene.root.empty()) strings.push_back(scene.root);
  return build_nif(strings, blocks, {0});
}

inline Loaded lighting(const Scene& scene) { return load(lighting_bytes(scene)); }

inline Loaded effect(const std::string& material, const std::vector<std::string>& textures) {
  using namespace nif_build;
  std::vector<Block> blocks{named_node_block("NiNode", 2, fo4::nif::Transform{}, {1}),
                            shape_with_refs("BSTriShape", 0, fo4::nif::Transform{}, k_full, 16, -1, 2, -1,
                                            triangle_vertices(), {0, 1, 2}),
                            effect_shader_block(1, textures)};
  return load(build_nif({"Tri", material, "Root"}, blocks, {0}));
}

inline fo4::material::MaterialFile bgsm(bool emit = false) {
  fo4::test::MaterialShape shape;
  shape.emit = emit;
  return fo4::material::read_material_file(fo4::test::material_bgsm(shape));
}

inline fo4::material::MaterialFile bgem() {
  return fo4::material::read_material_file(fo4::test::material_bgem(fo4::test::MaterialShape{}));
}

inline void put(fo4::test::TempDataDir& dir, const std::string& path, const fo4::material::MaterialFile& file) {
  dir.write(path, fo4::material::write_material_file(file), 0);
}

inline void touch(fo4::test::TempDataDir& dir, const std::string& path) { dir.write(path, {std::byte{1}}, 0); }

inline fo4::archive::AssetResolver open_dir(const fo4::test::TempDataDir& dir) {
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  return fo4::archive::AssetResolver::open_roots(roots, {});
}

}  // namespace resolved_fixture
