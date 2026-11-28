#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_assembly.hpp"
#include "nif_build.hpp"
#include "../src/nif/write_model.hpp"
#include "../src/nif/write_refs.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using nif_build::Block;
using nif_build::Writer;

const fo4::nif::Transform k_identity{};

void set_i32(std::vector<std::byte>& bytes, std::size_t at, std::int32_t value) {
  fo4::nif::write_detail::write_int(bytes, at, value);
}

// Constraint: count_at is the extra-data count, which must be 0 and be followed by the controller reference.
Block with_refs(Block block, std::size_t count_at, std::int32_t extra, std::int32_t controller) {
  Writer ref;
  ref.i32(extra);
  block.data.insert(block.data.begin() + static_cast<std::ptrdiff_t>(count_at + 4), ref.bytes().begin(),
                    ref.bytes().end());
  set_i32(block.data, count_at, 1);
  set_i32(block.data, count_at + 8, controller);
  return block;
}

Block dismember_instance() {
  Writer w;
  w.i32(0);
  w.i32(6);
  w.i32(5);
  w.u32(1);
  w.i32(1);
  w.u32(1);
  w.u32(0);
  return Block{"BSDismemberSkinInstance", w.bytes()};
}

Block multi_bound_node() {
  Writer tail;
  tail.i32(6);
  tail.u32(0);
  return nif_build::named_node_block("BSMultiBoundNode", 0, k_identity, {}, tail.bytes());
}

Block tailed_node(const std::string& type, std::size_t tail_bytes) {
  return with_refs(nif_build::named_node_block(type, 0, k_identity, {}, std::vector<std::byte>(tail_bytes)), 4, 5, 2);
}

Block lod_node() {
  Writer tail;
  tail.u16(0);
  tail.u32(0);
  tail.i32(-1);
  return with_refs(nif_build::named_node_block("NiLODNode", 0, k_identity, {}, tail.bytes()), 4, 5, 2);
}

Block dynamic_shape() {
  Writer tail;
  tail.u32(16);
  for (const float value : {7.0f, 8.0f, 9.0f, 10.0f}) tail.f32(value);
  return with_refs(nif_build::shape_with_refs("BSDynamicTriShape", 0, k_identity, nif_build::k_full, 16, 5, 3, 2,
                                              nif_build::triangle_vertices(), {0, 1, 2},
                                              nif_build::k_triangle_sphere, tail.bytes()),
                   4, 6, 4);
}

Block mesh_lod_shape() {
  Writer tail;
  tail.u32(1);
  tail.u32(0);
  tail.u32(0);
  return with_refs(nif_build::shape_with_refs("BSMeshLODTriShape", 0, k_identity, nif_build::k_full, 16, 5, 3, 2,
                                              nif_build::triangle_vertices(), {0, 1, 2},
                                              nif_build::k_triangle_sphere, tail.bytes()),
                   4, 6, 4);
}

Block key_data(const std::string& type, std::uint32_t interpolation, std::size_t value_floats) {
  Writer w;
  w.u32(1);
  w.u32(interpolation);
  w.f32(0.5f);
  const std::size_t floats = interpolation == 2 ? 3 * value_floats : value_floats;
  for (std::size_t i = 0; i < floats; ++i) w.f32(1.0f + static_cast<float>(i));
  return Block{type, w.bytes()};
}

Block tree_node() {
  Writer tail;
  tail.u32(2);
  tail.i32(2);
  tail.i32(3);
  tail.u32(1);
  tail.i32(4);
  return with_refs(nif_build::named_node_block("BSTreeNode", 0, k_identity, {}, tail.bytes()), 4, 5, 2);
}

// Invariant: merge_nif_into and write_detail::ref_offsets each carry a per-type table of where a block holds
// references. For every type both accept, a merge must move exactly the offsets ref_offsets names, by the base size.
TEST(NifRefTables, MergeRemapsExactlyTheOffsetsRefOffsetsNames) {
  const auto base = fo4::nif::read_nif(nif_build::build_nif(
      {"root"}, {nif_build::named_node_block("NiNode", 0, k_identity, {})}, {0}));
  constexpr std::uint32_t k_base_blocks = 1;

  const fo4::nif::Segment segment{0, 2, 0, {}};
  fo4::nif::ScannedConnectPoint point;
  point.parent = "P-Spine";
  point.name = "Socket";
  Writer bool_keys;
  bool_keys.u32(1);
  bool_keys.u32(1);
  bool_keys.f32(0.5f);
  bool_keys.u8(1);

  nif_build::LightingFields lighting;
  lighting.name = 0;
  lighting.texture_set = 4;
  const std::vector<Block> donor_blocks{
      with_refs(nif_build::named_node_block("NiNode", 0, k_identity, {1, 8}), 4, 5, 2),
      with_refs(nif_build::shape_with_refs("BSTriShape", 0, k_identity, nif_build::k_full, 16, 5, 3, 2,
                                           nif_build::triangle_vertices(), {0, 1, 2}, nif_build::k_triangle_sphere),
                4, 6, 4),
      with_refs(nif_build::alpha_block(0x12ED, 128), 4, 4, 5),
      with_refs(nif_build::full_lighting_shader_block(lighting), 8, 4, 2),
      nif_build::texture_set_record({"textures\\a.dds"}),
      nif_build::skin_instance_block(6, {1, 0}, 0),
      nif_build::bone_data_block(2),
      dismember_instance(),
      multi_bound_node(),
      tree_node(),
      tailed_node("BSFadeNode", 0),
      tailed_node("BSLeafAnimNode", 0),
      tailed_node("BSOrderedNode", 17),
      tailed_node("BSValueNode", 5),
      tailed_node("NiBillboardNode", 2),
      tailed_node("NiSwitchNode", 6),
      with_refs(nif_build::sub_index_shape_block(3, 2, nif_build::segment_table_bytes(2, 1, {segment})), 4, 6, 4),
      mesh_lod_shape(),
      dynamic_shape(),
      lod_node(),
      tailed_node("BSRangeNode", 3),
      tailed_node("BSDebrisNode", 3),
      tailed_node("NiSortAdjustNode", 4),
      tailed_node("BSBlastNode", 3),
      tailed_node("BSDamageStage", 3),
      nif_build::connect_parents_block({point}),
      nif_build::connect_children_block(false, {"Socket"}),
      key_data("NiFloatData", 1, 1),
      key_data("NiFloatData", 2, 1),
      key_data("NiPosData", 1, 3),
      Block{"NiBoolData", bool_keys.bytes()},
      Block{"NiTransformData", std::vector<std::byte>(12)},
  };
  const std::vector<std::size_t> expected_refs{4, 5, 2, 3, 0, 4, 0, 4, 1, 5, 2, 2, 2, 2, 2, 2,
                                               4, 5, 5, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0};
  ASSERT_EQ(expected_refs.size(), donor_blocks.size());
  const auto donor = fo4::nif::read_nif(nif_build::build_nif({"root"}, donor_blocks, {0}));

  const auto merged_bytes = fo4::nif::merge_nif_into(base, donor, 0);
  const auto merged_nif = fo4::nif::read_nif(merged_bytes);
  const auto merged = fo4::nif::write_detail::load_model(merged_nif);
  const auto donor_model = fo4::nif::write_detail::load_model(donor);
  ASSERT_EQ(merged.blocks.size(), donor_blocks.size() + std::size_t{k_base_blocks});

  for (std::uint32_t d = 0; d < donor_blocks.size(); ++d) {
    auto expected = donor_model.blocks[d];
    std::size_t moved = 0;
    for (const auto offset : fo4::nif::write_detail::ref_offsets(donor_model, d, true)) {
      const auto value = fo4::nif::write_detail::int_at(expected, offset);
      if (value < 0) continue;
      set_i32(expected, offset, value + static_cast<std::int32_t>(k_base_blocks));
      ++moved;
    }
    EXPECT_EQ(moved, expected_refs[d]) << "donor block " << d << " (" << donor_blocks[d].type << ")";
    EXPECT_EQ(merged.blocks[d + k_base_blocks], expected)
        << "donor block " << d << " (" << donor_blocks[d].type << ") differs from the ref_offsets remap";
  }
}

}  // namespace
