#include "fo4/nif/nif_audit.hpp"

#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using fo4::nif::NifAuditFinding;
using fo4::nif::Transform;
using namespace nif_build;

namespace {

std::vector<fo4::nif::NifAuditFinding> audit_bytes(const std::vector<std::byte>& bytes) {
  return fo4::nif::audit_nif(fo4::nif::read_nif(std::span<const std::byte>(bytes)));
}

Block texture_set_block(const std::vector<std::string>& paths) {
  Writer writer;
  writer.u32(static_cast<std::uint32_t>(paths.size()));
  for (const auto& path : paths) writer.sized(path);
  return Block{"BSShaderTextureSet", writer.bytes()};
}

Block node_with_extras(std::string type, std::int32_t name, const Transform& xf,
                       const std::vector<std::int32_t>& children,
                       const std::vector<std::int32_t>& extras) {
  Writer writer;
  writer.i32(name);
  writer.u32(static_cast<std::uint32_t>(extras.size()));
  for (const auto ref : extras) writer.i32(ref);
  writer.i32(-1);
  writer.u32(0);
  writer.transform(xf);
  writer.i32(-1);
  writer.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) writer.i32(child);
  return Block{std::move(type), writer.bytes()};
}

std::vector<std::string> clean_texture_paths() {
  std::vector<std::string> paths(10, "textures\\clean.dds");
  return paths;
}

const NifAuditFinding* find_rule(const std::vector<NifAuditFinding>& findings,
                                 std::string_view rule) {
  const auto found = std::find_if(findings.begin(), findings.end(),
                                  [&](const NifAuditFinding& finding) {
                                    return finding.rule == rule;
                                  });
  return found == findings.end() ? nullptr : &*found;
}

}  // namespace

// Detects: a rule that must not fire on a structurally valid Fallout 4 NIF, which would make the audit unusable.
TEST(NifAudit, CleanFallout4NifHasNoFindings) {
  const auto nif = build_nif({"root", "shape", "material"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, -1,
                                              triangle_vertices(), {0, 1, 2}),
                              lighting_shader_block(2, 3), texture_set_block(clean_texture_paths())},
                             {0});
  EXPECT_TRUE(audit_bytes(nif).empty());
}

// Detects: a root rule that must report both no root and two roots, not just the first root.
TEST(NifAudit, DetectsZeroAndTwoRoots) {
  const auto none = build_nif({"root"}, {node_block("BSFadeNode", Transform{}, {})}, {});
  const auto zero = audit_bytes(none);
  ASSERT_NE(find_rule(zero, fo4::nif::k_audit_root_count), nullptr);

  const auto two = build_nif({"a", "b"},
                             {node_block("BSFadeNode", Transform{}, {}),
                              node_block("BSFadeNode", Transform{}, {})},
                             {0, 1});
  const auto both = audit_bytes(two);
  const auto* finding = find_rule(both, fo4::nif::k_audit_root_count);
  ASSERT_NE(finding, nullptr);
  EXPECT_NE(finding->message.find("2"), std::string::npos);
}

// Detects: a root that must be a node; a shape root leaves the node walk with nowhere to start.
TEST(NifAudit, DetectsRootThatIsNotANode) {
  const auto nif = build_nif({"shape"}, {triangle_block("BSTriShape", Transform{})}, {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_root_not_node);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 0U);
}

// Detects: geometry that must be referenced by the node tree, which no engine draw call can reach.
TEST(NifAudit, DetectsShapeOutsideAnyNode) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {}),
                              shape_block("BSTriShape", 0, Transform{}, k_full, 16,
                                          triangle_vertices(), {0, 1, 2})},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_shape_outside_node);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
}

// Detects: an extra-data list that must stay inside the block table; read_nif does not check it.
TEST(NifAudit, DetectsExtraDataReferenceOutsideTheBlockTable) {
  const auto nif = build_nif({"root"},
                             {node_with_extras("BSFadeNode", 0, Transform{}, {}, {99})}, {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_extra_ref);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 0U);
}

// Detects: geometry block types that must not appear in a Fallout 4 file.
TEST(NifAudit, DetectsOlderGameGeometryBlock) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_block("NiTriShape", 0, Transform{}, k_full, 16,
                                          triangle_vertices(), {0, 1, 2})},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_geometry_not_fo4);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
}

// Detects: a shape shader that must be a Fallout 4 shader property block.
TEST(NifAudit, DetectsShapeShaderThatIsNotFallout4) {
  const auto nif = build_nif({"root", "shape"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, -1,
                                              triangle_vertices(), {0, 1, 2}),
                              Block{"BSShaderPPLightingProperty", {}}},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_shader_not_fo4);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
}

// Detects: a texture set that must carry exactly the ten Fallout 4 slots.
TEST(NifAudit, DetectsTextureSetWithAForeignSlotCount) {
  const auto nif = build_nif({"root", "shape", "material"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, -1,
                                              triangle_vertices(), {0, 1, 2}),
                              lighting_shader_block(2, 3),
                              texture_set_block({"textures\\a.dds", "textures\\b.dds"})},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_texture_slots);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 3U);
  EXPECT_NE(finding->message.find("2"), std::string::npos);
}

// Detects: a material name that must not carry surrounding whitespace, which normalisation would change.
TEST(NifAudit, DetectsShaderNameWithSurroundingWhitespace) {
  const auto nif = build_nif({"root", "shape", " material "},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, -1,
                                              triangle_vertices(), {0, 1, 2}),
                              lighting_shader_block(2, 3), texture_set_block(clean_texture_paths())},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_shader_name_space);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 2U);
}

// Detects: texture paths that must be relative data paths; whitespace, an absolute path or a parent walk cannot resolve.
TEST(NifAudit, DetectsTexturePathsThatAreNotRelativeDataPaths) {
  auto paths = clean_texture_paths();
  paths[0] = " textures\\padded.dds ";
  paths[1] = "C:\\absolute\\path.dds";
  paths[2] = "..\\escape.dds";
  const auto nif = build_nif({"root", "shape", "material"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, -1,
                                              triangle_vertices(), {0, 1, 2}),
                              lighting_shader_block(2, 3), texture_set_block(paths)},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* padded = find_rule(findings, fo4::nif::k_audit_texture_path_space);
  ASSERT_NE(padded, nullptr);
  EXPECT_EQ(padded->block, 3U);
  const auto* absolute = find_rule(findings, fo4::nif::k_audit_texture_path_absolute);
  ASSERT_NE(absolute, nullptr);
  EXPECT_NE(absolute->message.find("slot 1"), std::string::npos);
  const auto* parent = find_rule(findings, fo4::nif::k_audit_texture_path_parent);
  ASSERT_NE(parent, nullptr);
  EXPECT_NE(parent->message.find("slot 2"), std::string::npos);
}

// Detects: a child reference below the -1 null sentinel, which the reader skips but must still be reported.
TEST(NifAudit, DetectsAChildReferenceBelowTheNullSentinel) {
  const auto negative = build_nif({"root"}, {node_block("BSFadeNode", Transform{}, {-2})}, {0});
  const auto audit_below = audit_bytes(negative);
  const auto* below = find_rule(audit_below, fo4::nif::k_audit_child_below_null);
  ASSERT_NE(below, nullptr);
  EXPECT_EQ(below->block, 0U);
  EXPECT_NE(below->message.find("-2"), std::string::npos);
}

// Detects: the same child listed twice in one node, which must be refused because it double-instances the subtree.
TEST(NifAudit, DetectsADuplicateChildReference) {
  const auto nif = build_nif({"root", "child"},
                             {node_block("BSFadeNode", Transform{}, {1, 1}),
                              node_block("NiNode", Transform{}, {})},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_child_duplicate);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 0U);
}

// Detects: a child cycle that read_nif accepts, because the extra parent edge must stay hidden from its chain check.
TEST(NifAudit, DetectsAChildCycle) {
  const auto nif = build_nif({"root", "a", "b", "c"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              node_block("NiNode", Transform{}, {2}),
                              node_block("NiNode", Transform{}, {3}),
                              node_block("NiNode", Transform{}, {1})},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_child_cycle);
  ASSERT_NE(finding, nullptr);
}

// Detects: a block claimed as the child of two different nodes, which must be refused.
TEST(NifAudit, DetectsMultipleParentsForOneBlock) {
  const auto nif = build_nif({"root", "left", "right", "shared"},
                             {node_block("BSFadeNode", Transform{}, {1, 2}),
                              node_block("NiNode", Transform{}, {3}),
                              node_block("NiNode", Transform{}, {3}),
                              node_block("NiNode", Transform{}, {})},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_child_multiple_parents);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 3U);
}

// Detects: shape references that must point into the block table, on the skin, shader and alpha fields.
TEST(NifAudit, DetectsShapeReferencesOutsideTheBlockTable) {
  const auto nif = build_nif({"root", "shape"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, 9, 9, 9,
                                              triangle_vertices(), {0, 1, 2})},
                             {0});
  const auto findings = audit_bytes(nif);
  const auto* finding = find_rule(findings, fo4::nif::k_audit_shape_reference);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
  EXPECT_NE(finding->message.find("outside the block table"), std::string::npos);
}

// Detects: a skin bone reference that must point at a node block, which the reader never checks.
TEST(NifAudit, DetectsSkinBoneReferenceThatIsNotANode) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {}),
                              skin_instance_block(2, {0, 9}),
                              bone_data_block(2)},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_skin_bone);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
  EXPECT_NE(finding->message.find("9"), std::string::npos);
}

// Detects: a skin data reference that must name a BSSkin::BoneData block, on the wrong-type and out-of-range paths.
TEST(NifAudit, DetectsSkinDataReferenceThatIsNotBoneData) {
  const auto wrong_type = build_nif({"root"},
                                    {node_block("BSFadeNode", Transform{}, {}),
                                     skin_instance_block(0, {0})},
                                    {0});
  const auto audit_wrong = audit_bytes(wrong_type);
  const auto* wrong = find_rule(audit_wrong, fo4::nif::k_audit_skin_data);
  ASSERT_NE(wrong, nullptr);
  EXPECT_NE(wrong->message.find("BSSkin::BoneData"), std::string::npos);

  const auto beyond = build_nif({"root"},
                                {node_block("BSFadeNode", Transform{}, {}),
                                 skin_instance_block(9, {0})},
                                {0});
  const auto audit_out_of_range = audit_bytes(beyond);
  const auto* out_of_range = find_rule(audit_out_of_range, fo4::nif::k_audit_skin_data);
  ASSERT_NE(out_of_range, nullptr);
  EXPECT_NE(out_of_range->message.find("outside the block table"), std::string::npos);
}

// Detects: a skin whose transform count must match its bone list, which the reader tolerates until asked.
TEST(NifAudit, DetectsSkinBoneCountMismatch) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {}),
                              skin_instance_block(2, {0, 0}),
                              bone_data_block(1)},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_skin_bone_count);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
  EXPECT_NE(finding->message.find("2 bone references"), std::string::npos);
}

namespace {

Block multi_bound_node_block(std::int32_t bound) {
  Writer writer;
  writer.i32(0);
  writer.u32(0);
  writer.i32(-1);
  writer.u32(0);
  writer.transform(Transform{});
  writer.i32(-1);
  writer.u32(0);
  writer.i32(bound);
  return Block{"BSMultiBoundNode", writer.bytes()};
}

Block multi_bound_block(std::int32_t data) {
  Writer writer;
  writer.i32(data);
  return Block{"BSMultiBound", writer.bytes()};
}

Block multi_bound_data_block(std::string_view type) {
  Writer writer;
  for (int index = 0; index < 8; ++index) writer.f32(0.0f);
  return Block{std::string{type}, writer.bytes()};
}

}  // namespace

// Detects: a skin reference that must point at a BSSkin::Instance block, which every shipped skin does.
TEST(NifAudit, AcceptsASkinReferenceToAnInstance) {
  const auto nif = build_nif({"root", "shape"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, 2, -1, -1,
                                              triangle_vertices(), {0, 1, 2}),
                              skin_instance_block(3, {0}), bone_data_block(1)},
                             {0});
  EXPECT_TRUE(audit_bytes(nif).empty());
}

// Detects: a skin reference to a block that is not a BSSkin::Instance, which must be reported.
TEST(NifAudit, DetectsASkinReferenceThatIsNotAnInstance) {
  const auto nif = build_nif({"root", "shape"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, 0, -1, -1,
                                              triangle_vertices(), {0, 1, 2})},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_skin_reference);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
  EXPECT_NE(finding->message.find("BSSkin::Instance"), std::string::npos);
}

// Detects: the measured bound chain, which must accept both BSMultiBoundAABB and BSMultiBoundOBB data.
TEST(NifAudit, AcceptsABoundChainWithObbData) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              multi_bound_node_block(2), multi_bound_block(3),
                              multi_bound_data_block("BSMultiBoundOBB")},
                             {0});
  EXPECT_TRUE(audit_bytes(nif).empty());
}

// Detects: a bound reference that must point at a BSMultiBound block instead of any other block.
TEST(NifAudit, DetectsABoundReferenceThatIsNotABound) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              multi_bound_node_block(0), multi_bound_block(3),
                              multi_bound_data_block("BSMultiBoundAABB")},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_multi_bound);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 1U);
}

// Detects: bound data that must be an AABB or OBB block, not an arbitrary block.
TEST(NifAudit, DetectsBoundDataThatIsNotABoundDataBlock) {
  const auto nif = build_nif({"root"},
                             {node_block("BSFadeNode", Transform{}, {1}),
                              multi_bound_node_block(2), multi_bound_block(0),
                              multi_bound_data_block("BSMultiBoundAABB")},
                             {0});
  const auto audit_finding = audit_bytes(nif);
  const auto* finding = find_rule(audit_finding, fo4::nif::k_audit_multi_bound);
  ASSERT_NE(finding, nullptr);
  EXPECT_EQ(finding->block, 2U);
}
