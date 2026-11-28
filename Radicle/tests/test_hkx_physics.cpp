#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/physics.hpp"
#include "fo4/hkx/physics_preview.hpp"
#include "fo4/hkx/ragdoll_edit.hpp"
#include "fo4/hkx/write.hpp"
#include "hkx_mapper_fixture.hpp"
#include "hkx_physics_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace fo4::hkx;
using namespace fo4::test::hkx;

struct Loaded {
  HkxStatus status{HkxStatus::Ok};
  HkxRagdollModel model;
  HkxPhysicsLoadReport report;
};

Loaded load(const std::vector<std::byte>& bytes) {
  Loaded loaded;
  loaded.status = read_ragdoll(std::span<const std::byte>(bytes), loaded.model, loaded.report);
  return loaded;
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

bool any_finding(const HkxPhysicsLoadReport& report, HkxPhysicsLoadSeverity severity, std::string_view a,
                 std::string_view b = {}) {
  return std::any_of(report.findings.begin(), report.findings.end(), [&](const HkxPhysicsLoadFinding& finding) {
    return finding.severity == severity && contains(finding.message, a) && contains(finding.message, b);
  });
}

bool within(float expected, float actual, float tolerance) { return std::fabs(expected - actual) <= tolerance; }

bool within(const HkxVec3& expected, const HkxVec3& actual, float tolerance) {
  float sum = 0.0f;
  for (std::size_t i = 0; i < 3; ++i) sum += (expected[i] - actual[i]) * (expected[i] - actual[i]);
  return std::sqrt(sum) <= tolerance;
}

bool within(const HkxQuat& expected, const HkxQuat& actual, float tolerance) {
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::fabs(expected[i] - actual[i]) > tolerance) return false;
  }
  return true;
}

float dot(const HkxQuat& a, const HkxQuat& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }

HkxQuat unit(HkxQuat q) {
  const float length = std::sqrt(dot(q, q));
  for (float& value : q) value /= length;
  return q;
}

HkxQuat multiply(const HkxQuat& a, const HkxQuat& b) {
  return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
          a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

HkxVec3 rotate(const HkxQuat& q, const HkxVec3& v) {
  const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  return {v[0] + q[3] * tx + (q[1] * tz - q[2] * ty), v[1] + q[3] * ty + (q[2] * tx - q[0] * tz),
          v[2] + q[3] * tz + (q[0] * ty - q[1] * tx)};
}

HkxVec3 add(const HkxVec3& a, const HkxVec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

HkxVec3 pivot(const std::vector<float>& frame) { return {frame[12], frame[13], frame[14]}; }

std::vector<HkxBoneFrame> world_frames(const HkxSkeleton& skeleton) {
  std::vector<HkxBoneFrame> frames(skeleton.reference_pose.size());
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const HkxQsTransform& local = skeleton.reference_pose[i];
    const int parent = skeleton.parent_indices[i];
    if (parent >= 0 && static_cast<std::size_t>(parent) < i) {
      const HkxBoneFrame& up = frames[static_cast<std::size_t>(parent)];
      frames[i] = {add(up.position, rotate(up.rotation, local.translation)), unit(multiply(up.rotation, local.rotation))};
    } else {
      frames[i] = {local.translation, local.rotation};
    }
  }
  return frames;
}

std::vector<std::byte> bytes_of(std::string_view text) {
  std::vector<std::byte> bytes;
  for (char ch : text) bytes.push_back(static_cast<std::byte>(ch));
  return bytes;
}

int replace_text(std::vector<std::byte>& bytes, std::string_view from, std::string_view to) {
  EXPECT_EQ(from.size(), to.size());
  int hits = 0;
  for (std::size_t at = 0; at + from.size() <= bytes.size(); ++at) {
    bool same = true;
    for (std::size_t i = 0; i < from.size() && same; ++i) same = bytes[at + i] == static_cast<std::byte>(from[i]);
    if (!same) continue;
    for (std::size_t i = 0; i < to.size(); ++i) bytes[at + i] = static_cast<std::byte>(to[i]);
    ++hits;
  }
  return hits;
}

std::filesystem::path fixture_path(const std::filesystem::path& under_meshes) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::current_path(ec);
  for (int level = 0; level < 8 && !dir.empty(); ++level) {
    const auto file = dir / "Radicle" / "tests" / "fixtures" / "hkx" / "Meshes" / under_meshes;
    if (std::filesystem::exists(file, ec)) return file;
    if (dir == dir.root_path()) break;
    dir = dir.parent_path();
  }
  return {};
}

// Requires the committed vanilla fixture; a missing file fails on CI and skips elsewhere, and the test must return then.
void load_fixture(const std::filesystem::path& under_meshes, std::vector<std::byte>& bytes) {
  const auto file = fixture_path(under_meshes);
  if (file.empty()) {
    if (std::getenv("CI") != nullptr) FAIL() << "CI is missing the committed fixture " << under_meshes.string();
    GTEST_SKIP() << "no committed fixture " << under_meshes.string();
  }
  std::ifstream stream(file, std::ios::binary);
  char buffer[4096];
  bytes.clear();
  while (stream.read(buffer, static_cast<std::streamsize>(sizeof buffer)) || stream.gcount() > 0) {
    for (std::streamsize i = 0; i < stream.gcount(); ++i) {
      bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(buffer[i])));
    }
  }
}

const std::filesystem::path kSkeletonFixture =
    std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "skeleton.hkx";
const std::filesystem::path kClothHairFixture =
    std::filesystem::path{"Actors"} / "Character" / "CharacterAssets" / "Hair" / "Female" / "FemaleHair04.hkx";

HkxPhysicsLoadFacts ragdoll_facts() {
  HkxPhysicsLoadFacts facts;
  facts.file_version = 11;
  facts.pointer_size = 8;
  facts.class_counts = {{"hknpRagdollData", 1},
                        {"hknpCapsuleShape", 18},
                        {"hknpPhysicsSceneData", 1},
                        {"hkaSkeleton", 2},
                        {"hkaSkeletonMapper", 2}};
  facts.body_count = 18;
  facts.usable_bodies = 18;
  facts.shape_count = 18;
  facts.constraint_count = 12;
  facts.mappings = 18;
  return facts;
}

}  // namespace

TEST(HkxPhysicsSynthetic, ReadsBodiesShapesConstraintsAndMapping) {
  const Loaded loaded = load(physics_packfile(default_physics_spec()));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxRagdollModel& model = loaded.model;

  ASSERT_EQ(model.shapes.size(), 2u);
  EXPECT_EQ(model.shapes[0].id, 1);
  EXPECT_EQ(model.shapes[1].id, 2);
  EXPECT_EQ(model.shapes[0].kind, HkxPhysicsShapeKind::Capsule);

  ASSERT_EQ(model.bodies.size(), 2u);
  EXPECT_EQ(model.bodies[0].id, 0);
  EXPECT_EQ(model.bodies[0].name, "Root Body");
  EXPECT_EQ(model.bodies[0].shape_id, 1);
  EXPECT_EQ(model.bodies[1].shape_id, 2);
  EXPECT_EQ(model.bodies[0].position, (HkxVec3{1, 2, 3}));
  EXPECT_EQ(model.bodies[1].position, (HkxVec3{4, 5, 6}));
  EXPECT_EQ(model.bodies[0].center_of_mass, (HkxVec3{20, 21, 22}));
  EXPECT_EQ(model.bodies[1].center_of_mass, (HkxVec3{10, 11, 12}));
  EXPECT_EQ(model.bodies[0].bone_index, -1);

  ASSERT_EQ(model.constraints.size(), 1u);
  const HkxPhysicsConstraint& constraint = model.constraints[0];
  EXPECT_EQ(constraint.kind, HkxConstraintKind::Ragdoll);
  EXPECT_EQ(constraint.body_a, 1);
  EXPECT_EQ(constraint.body_b, 0);
  ASSERT_TRUE(constraint.frame_a.has_value());
  ASSERT_TRUE(constraint.frame_b.has_value());
  EXPECT_EQ(constraint.frame_a->size(), 16u);
  EXPECT_EQ(pivot(*constraint.frame_a), (HkxVec3{0.5f, 0.5f, 0.5f}));
  EXPECT_EQ(pivot(*constraint.frame_b), (HkxVec3{0.25f, 0.25f, 0.25f}));
  EXPECT_EQ(constraint.twist_axis, 2);
  EXPECT_EQ(constraint.twist_ref_axis, 0);
  ASSERT_TRUE(constraint.twist_min_angle.has_value());
  EXPECT_FLOAT_EQ(*constraint.twist_min_angle, -0.5f);
  EXPECT_FLOAT_EQ(*constraint.twist_max_angle, 0.75f);
  EXPECT_EQ(constraint.cone_twist_axis, 1);
  EXPECT_EQ(constraint.cone_ref_axis, 2);
  EXPECT_FLOAT_EQ(*constraint.cone_max_angle, 0.625f);

  ASSERT_EQ(model.bone_bindings.size(), 2u);
  EXPECT_EQ(model.bone_bindings[1].bone_index, 1);
  EXPECT_EQ(model.bone_bindings[1].body_id, 1);

  ASSERT_TRUE(model.skeleton.has_value());
  EXPECT_EQ(model.skeleton->name, "Physics");
  EXPECT_EQ(model.skeleton->bone_names, (std::vector<std::string>{"RootBone", "ChildBone"}));
  ASSERT_TRUE(model.animation_skeleton.has_value());
  EXPECT_EQ(model.animation_skeleton->name, "Animation");
  EXPECT_EQ(model.animation_skeleton->bone_names.size(), 3u);
  ASSERT_EQ(model.mappings.size(), 2u);
  EXPECT_EQ(model.mappings[1].bone_a, 1);
  EXPECT_EQ(model.mappings[1].bone_b, 1);
  EXPECT_EQ(model.mappings[1].a_from_b_translation, (HkxVec3{0, 1, 0}));
  EXPECT_EQ(model.mappings[1].a_from_b_rotation, (HkxQuat{0, 0, 0, 1}));

  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::Ok);
  EXPECT_EQ(loaded.report.body_count, 2);
  EXPECT_EQ(loaded.report.usable_bodies, 2);
  EXPECT_EQ(loaded.report.shape_count, 2);
  EXPECT_EQ(loaded.report.constraint_count, 1);
  EXPECT_EQ(loaded.report.mappings, 2);
  EXPECT_EQ(loaded.report.file_version, 11);
  EXPECT_EQ(loaded.report.pointer_size, 8);
  EXPECT_EQ(loaded.report.count_of("hknpCapsuleShape"), 2);
  EXPECT_TRUE(loaded.report.mapper_present());
}

TEST(HkxPhysicsSynthetic, ShapesCarryTheirPolytopePayload) {
  const Loaded loaded = load(physics_packfile(default_physics_spec()));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxPhysicsShape& shape = loaded.model.shapes[0];
  ASSERT_EQ(shape.vertices.size(), 8u);
  EXPECT_EQ(shape.vertices[2], (HkxVec3{1, 1, 0}));
  EXPECT_EQ(shape.vertices[7], (HkxVec3{0, 1, 1}));
  ASSERT_EQ(shape.face_rings.size(), 2u);
  EXPECT_EQ(shape.face_rings[0], (std::vector<std::int32_t>{0, 1, 2, 3}));
  EXPECT_EQ(shape.face_rings[1], (std::vector<std::int32_t>{4, 5, 6, 7}));
}

TEST(HkxPhysicsSynthetic, AFaceRunPastTheIndexListIsDropped) {
  PhysicsSpec spec = default_physics_spec();
  spec.shapes[0].faces = {{0, 4}, {6, 4}, {2, 2}};
  const Loaded loaded = load(physics_packfile(spec));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  ASSERT_EQ(loaded.model.shapes[0].face_rings.size(), 2u);
  EXPECT_EQ(loaded.model.shapes[0].face_rings[1], (std::vector<std::int32_t>{2, 3}));
}

TEST(HkxPhysicsSynthetic, EveryMeasuredConstraintKindReadsItsLimits) {
  PhysicsSpec spec = default_physics_spec();
  PhysConstraintSpec base;
  base.body_a = 1;
  base.body_b = 0;
  base.frame_a.assign(16, 1.0f);
  base.frame_b.assign(16, 2.0f);

  PhysConstraintSpec limited = base;
  limited.data_class = "hkpLimitedHingeConstraintData";
  limited.limit_axis = 2;
  limited.hinge_min = -1.5f;
  limited.hinge_max = 0.25f;
  PhysConstraintSpec hinge = base;
  hinge.data_class = "hkpHingeConstraintData";
  PhysConstraintSpec fixed = base;
  fixed.data_class = "hkpFixedConstraintData";
  PhysConstraintSpec ball = base;
  ball.data_class = "hkpBallAndSocketConstraintData";
  PhysConstraintSpec other = base;
  other.data_class = "hkpStiffSpringConstraintData";
  PhysConstraintSpec bare = base;
  spec.constraints = {limited, hinge, fixed, ball, other, bare};

  const Loaded loaded = load(physics_packfile(spec));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  ASSERT_EQ(loaded.model.constraints.size(), 6u);
  const auto& constraints = loaded.model.constraints;

  EXPECT_EQ(constraints[0].kind, HkxConstraintKind::LimitedHinge);
  EXPECT_EQ(constraints[0].limit_axis, 2);
  EXPECT_FLOAT_EQ(constraints[0].min_angle, -1.5f);
  EXPECT_FLOAT_EQ(constraints[0].max_angle, 0.25f);
  ASSERT_TRUE(constraints[0].frame_a.has_value());
  EXPECT_EQ(pivot(*constraints[0].frame_a), (HkxVec3{1, 1, 1}));
  EXPECT_EQ(pivot(*constraints[0].frame_b), (HkxVec3{2, 2, 2}));
  EXPECT_FALSE(constraints[0].twist_min_angle.has_value());

  EXPECT_EQ(constraints[1].kind, HkxConstraintKind::Hinge);
  EXPECT_TRUE(constraints[1].frame_a.has_value());
  EXPECT_EQ(constraints[2].kind, HkxConstraintKind::Fixed);
  EXPECT_TRUE(constraints[2].frame_b.has_value());

  EXPECT_EQ(constraints[3].kind, HkxConstraintKind::BallAndSocket);
  EXPECT_FALSE(constraints[3].frame_a.has_value());
  EXPECT_FALSE(constraints[3].frame_b.has_value());

  EXPECT_EQ(constraints[4].kind, HkxConstraintKind::Unknown);
  EXPECT_FALSE(constraints[4].frame_a.has_value());
  EXPECT_EQ(constraints[4].twist_ref_axis, 1);

  EXPECT_EQ(constraints[5].kind, HkxConstraintKind::Unknown);
  EXPECT_FALSE(constraints[5].frame_a.has_value());
  for (std::size_t i = 0; i < constraints.size(); ++i) EXPECT_EQ(constraints[i].id, static_cast<std::int32_t>(i));
}

TEST(HkxPhysicsSynthetic, AConstraintNamingNoBodyIsCountedAsSkipped) {
  PhysicsSpec spec = default_physics_spec();
  PhysConstraintSpec broken = spec.constraints[0];
  broken.body_a = 0xFFFFFFFFu;
  spec.constraints.push_back(broken);

  const Loaded loaded = load(physics_packfile(spec));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  EXPECT_EQ(loaded.model.constraints.size(), 1u);
  EXPECT_EQ(loaded.report.skipped_constraints, 1);
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::UnsupportedShapes);
  EXPECT_TRUE(contains(loaded.report.headline(), "1 constraint entry could not be read"));
}

TEST(HkxPhysicsSynthetic, BodiesWithOtherShapesAreKeptAndCounted) {
  PhysicsSpec spec = default_physics_spec();
  PhysShapeSpec other;
  other.class_name = "hknpConvexShape";
  other.vertices = {{0, 0, 0, 0}, {1, 1, 1, 0}};
  spec.shapes.push_back(other);
  spec.bodies[1].shape = 2;
  PhysBodySpec shapeless;
  shapeless.name = "Loose";
  spec.bodies.push_back(shapeless);

  const Loaded loaded = load(physics_packfile(spec));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  EXPECT_EQ(loaded.model.shapes.size(), 2u);
  ASSERT_EQ(loaded.model.bodies.size(), 3u);
  EXPECT_EQ(loaded.model.bodies[2].shape_id, -1);
  EXPECT_EQ(loaded.report.unsupported_shape_bodies, 2);
  EXPECT_EQ(loaded.report.unsupported_shape_classes.at("hknpConvexShape"), 1);
  EXPECT_EQ(loaded.report.unsupported_shape_classes.at("(no shape reference)"), 1);
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::UnsupportedShapes);
  EXPECT_TRUE(contains(loaded.report.headline(), "2 of 3 bodies use shapes FO4IDE cannot preview yet"));
}

TEST(HkxPhysicsSynthetic, ARagdollWithNoBodiesIsNoBodies) {
  PhysicsSpec spec = default_physics_spec();
  spec.bodies.clear();
  spec.constraints.clear();
  const Loaded loaded = load(physics_packfile(spec));
  EXPECT_EQ(loaded.status, HkxStatus::Ok);
  EXPECT_TRUE(loaded.model.bodies.empty());
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::NoBodies);
  EXPECT_TRUE(contains(loaded.report.headline(), "declares no rigid bodies at all"));
}

TEST(HkxPhysicsSynthetic, AMapperThatDoesNotLinkTheRagdollIsMapperMissing) {
  PhysicsSpec no_rows = default_physics_spec();
  no_rows.rows.clear();
  const Loaded empty = load(physics_packfile(no_rows));
  ASSERT_EQ(empty.status, HkxStatus::Ok);
  EXPECT_TRUE(empty.model.mappings.empty());
  EXPECT_EQ(empty.report.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_TRUE(contains(empty.report.headline(), "is present but none of its mappings link this ragdoll"));

  PhysicsSpec chained = default_physics_spec();
  chained.mapping_type = 1;
  const Loaded other_type = load(physics_packfile(chained));
  ASSERT_EQ(other_type.status, HkxStatus::Ok);
  EXPECT_TRUE(other_type.model.mappings.empty());
  EXPECT_FALSE(other_type.model.animation_skeleton.has_value());
  EXPECT_EQ(other_type.report.status, HkxPhysicsLoadStatus::MapperMissing);
}

TEST(HkxPhysicsSynthetic, ARagdollWithoutASkeletonSaysTheMapperIsAbsent) {
  PhysicsSpec spec = default_physics_spec();
  spec.with_skeleton = false;
  const Loaded loaded = load(physics_packfile(spec));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  EXPECT_FALSE(loaded.model.skeleton.has_value());
  EXPECT_TRUE(loaded.model.mappings.empty());
  EXPECT_EQ(loaded.model.bodies.size(), 2u);
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_FALSE(loaded.report.mapper_present());
  EXPECT_TRUE(contains(loaded.report.headline(), "no hkaSkeletonMapper links the animation skeleton to it"));
}

TEST(HkxPhysicsSynthetic, ASkeletonPointerAtAnotherClassIsIgnored) {
  PhysicsSpec spec = default_physics_spec();
  spec.ragdoll_points_at_mapper = true;
  const Loaded loaded = load(physics_packfile(spec));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  EXPECT_FALSE(loaded.model.skeleton.has_value());
  EXPECT_TRUE(loaded.model.mappings.empty());
}

TEST(HkxPhysicsSynthetic, ABodyArrayPastTheSectionIsAParseError) {
  std::vector<std::byte> bytes = physics_packfile(default_physics_spec());
  HkxPackfile pack;
  ASSERT_EQ(read_packfile(bytes, pack), HkxStatus::Ok);
  const std::size_t count_at = pack.sections[pack.data_section].file_offset +
                               member_at("hknpPhysicsSystemData", "bodyCinfos") + 8;
  patch_u32(bytes, count_at, 0x10000u);

  const Loaded loaded = load(bytes);
  EXPECT_EQ(loaded.status, HkxStatus::FixupOutOfRange);
  EXPECT_TRUE(loaded.model.bodies.empty());
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::ParseError);
  EXPECT_EQ(loaded.report.failing_stage, "ragdoll");
  EXPECT_TRUE(contains(loaded.report.headline(), "failed at the ragdoll stage"));
}

TEST(HkxPhysicsSynthetic, AFileWithoutARagdollIsSkeletonOnly) {
  const Loaded loaded = load(mapper_packfile(default_mapper_spec()));
  EXPECT_EQ(loaded.status, HkxStatus::MissingObject);
  EXPECT_TRUE(loaded.model.bodies.empty());
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::SkeletonOnly);
  EXPECT_EQ(loaded.report.severity(), HkxPhysicsLoadSeverity::Warning);
  EXPECT_EQ(loaded.report.count_of("hkaSkeleton"), 2);
}

TEST(HkxPhysicsSynthetic, ThePackfileOverloadReportsTheSameModel) {
  const std::vector<std::byte> bytes = physics_packfile(default_physics_spec());
  HkxPackfile pack;
  ASSERT_EQ(read_packfile(bytes, pack), HkxStatus::Ok);
  HkxRagdollModel model;
  HkxPhysicsLoadReport report;
  ASSERT_EQ(read_ragdoll(pack, model, report), HkxStatus::Ok);
  EXPECT_EQ(model.bodies.size(), 2u);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::Ok);
  EXPECT_EQ(inspect_physics_file(bytes).status, HkxPhysicsLoadStatus::Ok);
}

TEST(HkxPhysicsSynthetic, ASyntheticModelPassesTheValidator) {
  const Loaded loaded = load(physics_packfile(default_physics_spec()));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const auto findings = check_physics_model(loaded.model, 2);
  for (const HkxPhysicsValidationFinding& finding : findings) {
    EXPECT_NE(finding.level, HkxPhysicsValidationLevel::Error) << finding.where << ": " << finding.message;
  }
}

TEST(HkxPhysicsContainer, EmptyAndTinyInputsSayWhy) {
  const Loaded empty = load({});
  EXPECT_NE(empty.status, HkxStatus::Ok);
  EXPECT_EQ(empty.report.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_TRUE(contains(empty.report.headline(), "empty"));

  const Loaded tiny = load({std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5},
                            std::byte{6}, std::byte{7}});
  EXPECT_EQ(tiny.report.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_TRUE(contains(tiny.report.headline(), "too small"));
}

TEST(HkxPhysicsContainer, FourBytePointersAreRefusedWithTheReason) {
  std::vector<std::byte> bytes = physics_packfile(default_physics_spec());
  bytes[0x10] = std::byte{4};
  const Loaded loaded = load(bytes);
  EXPECT_NE(loaded.status, HkxStatus::Ok);
  EXPECT_TRUE(loaded.model.bodies.empty());
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_TRUE(contains(loaded.report.headline(), "4-byte pointers"));
  EXPECT_EQ(loaded.report.pointer_size, 4);
  EXPECT_EQ(loaded.report.file_version, 11);
}

TEST(HkxPhysicsContainer, OtherVersionsAndMagicAreNamed) {
  std::optional<std::int32_t> version;
  std::optional<std::int32_t> pointer;

  std::vector<std::byte> ten = physics_packfile(default_physics_spec());
  ten[0x0C] = std::byte{10};
  EXPECT_EQ(physics_container_problem(ten, version, pointer), "it is a version 10 packfile, and Fallout 4 uses version 11");
  EXPECT_EQ(version, 10);
  EXPECT_FALSE(pointer.has_value());

  std::vector<std::byte> magic = physics_packfile(default_physics_spec());
  magic[0] = std::byte{0};
  EXPECT_TRUE(contains(physics_container_problem(magic, version, pointer), "the packfile magic does not match"));
  EXPECT_FALSE(version.has_value());

  std::vector<std::byte> big_endian = physics_packfile(default_physics_spec());
  big_endian[0x11] = std::byte{0};
  EXPECT_TRUE(contains(physics_container_problem(big_endian, version, pointer), "big-endian"));

  std::vector<std::byte> rules = physics_packfile(default_physics_spec());
  rules[0x13] = std::byte{0};
  EXPECT_EQ(physics_container_problem(rules, version, pointer),
            "its layout rules are 8 1 0 0, which are not the Fallout 4 rules 8 1 0 1");

  EXPECT_EQ(physics_container_problem(physics_packfile(default_physics_spec()), version, pointer), "");
  EXPECT_EQ(version, 11);
  EXPECT_EQ(pointer, 8);
}

TEST(HkxPhysicsContainer, TagfilesAreRefusedByTheirMarker) {
  std::vector<std::byte> binary(0x40 + 8, std::byte{0});
  binary[0] = std::byte{0x57};
  binary[1] = std::byte{0xE0};
  binary[2] = std::byte{0xE0};
  binary[3] = std::byte{0x57};
  binary[4] = std::byte{'T'};
  binary[5] = std::byte{'A'};
  binary[6] = std::byte{'G'};
  binary[7] = std::byte{'0'};
  const Loaded tag = load(binary);
  EXPECT_EQ(tag.report.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_TRUE(contains(tag.report.headline(), "tagfile"));

  const std::string xml =
      "<?xml version=\"1.0\" encoding=\"ascii\"?>\n<hkpackfile classversion=\"11\" contentsversion=\"hk_2014.1.0-r1\">\n</hkpackfile>\n";
  ASSERT_GT(xml.size(), 0x40u);
  const Loaded text = load(bytes_of(xml));
  EXPECT_EQ(text.report.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_TRUE(contains(text.report.headline(), "tagfile"));
}

TEST(HkxPhysicsContainer, ASectionCountPastTheEndOfTheFileIsAParseError) {
  std::vector<std::byte> bytes = physics_packfile(default_physics_spec());
  bytes[0x14] = std::byte{0xFF};
  bytes[0x15] = std::byte{0xFF};
  bytes[0x16] = std::byte{0xFF};
  bytes[0x17] = std::byte{0x7F};
  const Loaded loaded = load(bytes);
  EXPECT_NE(loaded.status, HkxStatus::Ok);
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::ParseError);
  EXPECT_EQ(loaded.report.failing_stage, "container");
  EXPECT_TRUE(contains(loaded.report.headline(), "failed at the container stage"));
  EXPECT_EQ(loaded.report.file_version, 11);
}

TEST(HkxPhysicsContainer, RenamedClassesChangeTheVerdict) {
  std::vector<std::byte> legacy = physics_packfile(default_physics_spec());
  ASSERT_GT(replace_text(legacy, "hknpRagdollData", "hkpCapsuleShape"), 0);
  const Loaded old_file = load(legacy);
  EXPECT_EQ(old_file.status, HkxStatus::MissingObject);
  EXPECT_EQ(old_file.report.status, HkxPhysicsLoadStatus::LegacyPhysics);
  EXPECT_EQ(old_file.report.count_of("hknpRagdollData"), 0);
  EXPECT_EQ(old_file.report.count_of("hkpCapsuleShape"), 1);

  std::vector<std::byte> bare = physics_packfile(default_physics_spec());
  ASSERT_GT(replace_text(bare, "hkaSkeleton", "hkxSkeleton"), 0);
  const Loaded no_mapper = load(bare);
  ASSERT_EQ(no_mapper.status, HkxStatus::Ok);
  EXPECT_EQ(no_mapper.model.bodies.size(), 2u);
  EXPECT_EQ(no_mapper.report.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_FALSE(no_mapper.report.mapper_present());
  EXPECT_EQ(no_mapper.report.count_of("hkaSkeleton"), 0);
}

TEST(HkxPhysicsModel, PosedCenterOfMassRidesTheBodyFrame) {
  HkxRigidBody body;
  body.center_of_mass = {1, 0, 0};
  const float half = std::sqrt(0.5f);
  const HkxVec3 posed = posed_center_of_mass(body, {10, 0, 0}, {0, 0, half, half});
  EXPECT_TRUE(within(HkxVec3{10, 1, 0}, posed, 1e-5f));

  HkxRigidBody turned;
  turned.position = {5, 0, 0};
  turned.rotation = {0, 0, half, half};
  turned.center_of_mass = {5, 2, 0};
  EXPECT_TRUE(within(HkxVec3{12, 0, 0}, posed_center_of_mass(turned, {10, 0, 0}, {0, 0, 0, 1}), 1e-5f));

  HkxRigidBody unmeasured;
  EXPECT_EQ(posed_center_of_mass(unmeasured, {3, 3, 3}, {0, 0, 0, 1}), (HkxVec3{0, 0, 0}));
}

TEST(HkxPhysicsModel, MapPoseFailsClosed) {
  HkxRagdollModel bare;
  bare.skeleton.emplace();
  const std::array<HkxBoneFrame, 1> one{HkxBoneFrame{}};
  std::vector<HkxBoneFrame> out;
  EXPECT_FALSE(map_pose(bare, one, out));
  EXPECT_TRUE(out.empty());

  const Loaded loaded = load(physics_packfile(default_physics_spec()));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  EXPECT_FALSE(map_pose(loaded.model, {}, out));
  EXPECT_FALSE(map_pose(loaded.model, one, out));
  // Constraint: detects a pose from a rig with more bones than the ragdoll's, whose indices are all in range, being mapped anyway.
  std::vector<HkxBoneFrame> longer = world_frames(*loaded.model.animation_skeleton);
  longer.push_back(HkxBoneFrame{});
  EXPECT_FALSE(map_pose(loaded.model, longer, out));
  EXPECT_TRUE(out.empty());
}

TEST(HkxPhysicsModel, MapPoseCarriesTheOffsetWithTheSourceFrame) {
  const Loaded loaded = load(physics_packfile(default_physics_spec()));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const std::vector<HkxBoneFrame> animation = world_frames(*loaded.model.animation_skeleton);

  std::vector<HkxBoneFrame> reference;
  ASSERT_TRUE(map_pose(loaded.model, animation, reference));
  ASSERT_EQ(reference.size(), 2u);
  EXPECT_TRUE(within(HkxVec3{0, 2, 0}, reference[1].position, 1e-5f));

  std::vector<HkxBoneFrame> shifted = animation;
  for (HkxBoneFrame& frame : shifted) frame.position = add(frame.position, {10, -3, 4.5f});
  std::vector<HkxBoneFrame> moved;
  ASSERT_TRUE(map_pose(loaded.model, shifted, moved));
  for (std::size_t i = 0; i < moved.size(); ++i) {
    EXPECT_TRUE(within(add(reference[i].position, {10, -3, 4.5f}), moved[i].position, 1e-4f)) << i;
  }

  const float half = std::sqrt(0.5f);
  const HkxQuat turn{0, 0, half, half};
  std::vector<HkxBoneFrame> rotated = animation;
  for (HkxBoneFrame& frame : rotated) {
    frame.position = rotate(turn, frame.position);
    frame.rotation = unit(multiply(turn, frame.rotation));
  }
  std::vector<HkxBoneFrame> turned;
  ASSERT_TRUE(map_pose(loaded.model, rotated, turned));
  for (std::size_t i = 0; i < turned.size(); ++i) {
    EXPECT_TRUE(within(rotate(turn, reference[i].position), turned[i].position, 1e-4f)) << i;
    EXPECT_GE(std::fabs(dot(unit(multiply(turn, reference[i].rotation)), unit(turned[i].rotation))), 0.999f) << i;
  }
}

TEST(HkxPhysicsModel, UncoveredBonesKeepTheirReferenceFrame) {
  Loaded loaded = load(physics_packfile(default_physics_spec()));
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  loaded.model.mappings.pop_back();
  const std::vector<HkxBoneFrame> animation = world_frames(*loaded.model.animation_skeleton);
  std::vector<HkxBoneFrame> mapped;
  ASSERT_TRUE(map_pose(loaded.model, animation, mapped));
  const std::vector<HkxBoneFrame> reference = world_reference_frames(*loaded.model.skeleton);
  ASSERT_EQ(mapped.size(), reference.size());
  EXPECT_EQ(mapped[1].position, reference[1].position);
  EXPECT_EQ(mapped[1].rotation, reference[1].rotation);
}

TEST(HkxPhysicsValidator, FlagsBrokenSkeletonConsistency) {
  HkxRagdollModel model;
  HkxSkeleton skeleton;
  skeleton.name = "broken";
  skeleton.bone_names = {"A", "B"};
  skeleton.parent_indices = {5};
  HkxQsTransform good;
  good.rotation = {0, 0, 0, 1};
  good.scale = {1, 1, 1};
  HkxQsTransform bad = good;
  bad.translation = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
  bad.scale = {2, 2, 2};
  skeleton.reference_pose = {good, bad};
  model.skeleton = skeleton;
  model.bone_bindings.push_back({7, 0});

  const auto findings = check_physics_model(model);
  const auto has = [&](HkxPhysicsValidationLevel level, std::string_view part) {
    return std::any_of(findings.begin(), findings.end(), [&](const HkxPhysicsValidationFinding& finding) {
      return finding.level == level && contains(finding.message, part);
    });
  };
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "parent index 5"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "2-bone physics skeleton"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "1 parent indices"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "reference pose"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "missing body 0"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Warning, "not identity"));
}

TEST(HkxPhysicsValidator, FlagsBrokenAnimationMappings) {
  HkxRagdollModel model;
  HkxSkeleton physics;
  physics.name = "physics";
  physics.bone_names = {"A", "B"};
  physics.parent_indices = {-1, 0};
  HkxQsTransform pose;
  pose.rotation = {0, 0, 0, 1};
  pose.scale = {1, 1, 1};
  physics.reference_pose = {pose, pose};
  HkxSkeleton animation;
  animation.name = "animation";
  animation.bone_names = {"X"};
  animation.parent_indices = {-1};
  animation.reference_pose = {pose};
  model.skeleton = physics;
  model.animation_skeleton = animation;
  model.mappings.push_back({7, 0, {0, 0, 0}, {0, 0, 0, 1}});

  const auto first = check_physics_model(model);
  const auto has = [](const std::vector<HkxPhysicsValidationFinding>& findings, HkxPhysicsValidationLevel level,
                      std::string_view part) {
    return std::any_of(findings.begin(), findings.end(), [&](const HkxPhysicsValidationFinding& finding) {
      return finding.level == level && contains(finding.message, part);
    });
  };
  EXPECT_TRUE(has(first, HkxPhysicsValidationLevel::Error, "outside the animation skeleton"));
  EXPECT_TRUE(has(first, HkxPhysicsValidationLevel::Warning, "not covered"));

  model.mappings.push_back({1, 0, {0, 0, 0}, {0, 0, 0, 1}});
  EXPECT_TRUE(has(check_physics_model(model), HkxPhysicsValidationLevel::Error, "mapped more than once"));
}

TEST(HkxPhysicsValidator, FlagsBrokenShapesBodiesAndConstraints) {
  HkxRagdollModel model;
  HkxPhysicsShape a;
  a.id = 1;
  a.children = {2};
  a.vertices = {{0, 0, 0}};
  a.face_rings = {{0, 4}};
  HkxPhysicsShape b;
  b.id = 2;
  b.children = {1, 9};
  b.radius = -1.0f;
  HkxPhysicsShape duplicate;
  duplicate.id = 2;
  model.shapes = {a, b, duplicate};

  HkxRigidBody body;
  body.id = 0;
  body.shape_id = 5;
  body.rotation = {0, 0, 0, 0.9f};
  HkxRigidBody degenerate;
  degenerate.id = 1;
  degenerate.shape_id = 1;
  degenerate.rotation = {0, 0, 0, 0};
  model.bodies = {body, degenerate};

  HkxPhysicsConstraint constraint;
  constraint.id = 0;
  constraint.body_a = 0;
  constraint.body_b = 0;
  constraint.min_angle = 2.0f;
  constraint.max_angle = 1.0f;
  constraint.twist_axis = 3;
  constraint.frame_a = std::vector<float>(15, 0.0f);
  model.constraints = {constraint};

  const auto findings = check_physics_model(model);
  const auto has = [&](HkxPhysicsValidationLevel level, std::string_view where, std::string_view part) {
    return std::any_of(findings.begin(), findings.end(), [&](const HkxPhysicsValidationFinding& finding) {
      return finding.level == level && contains(finding.where, where) && contains(finding.message, part);
    });
  };
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "shape 2", "duplicate shape id"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "shape 2", "radius must be a finite non-negative value"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "shape 2", "references missing child shape 9"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Warning, "shape 1", "face references missing polytope vertex 4"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "shape", "is part of a containment cycle ("));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "body 0", "references missing shape 5"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "body 0", "rotation is not a unit quaternion (length 0.9)"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "body 1", "rotation is degenerate"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "constraint 0", "cannot constrain a body to itself"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "constraint 0", "minimum angle is greater than maximum angle"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "constraint 0", "frame A must hold sixteen floats"));
  EXPECT_TRUE(has(HkxPhysicsValidationLevel::Error, "constraint 0", "twist axis 3 is outside a three-column frame"));
}

TEST(HkxPhysicsValidator, FlagsBoneIndicesOutsideTheSkeleton) {
  HkxRagdollModel model;
  HkxPhysicsShape shape;
  shape.id = 1;
  model.shapes = {shape};
  HkxRigidBody body;
  body.id = 0;
  body.shape_id = 1;
  body.bone_index = 4;
  model.bodies = {body};
  model.bone_bindings = {{4, 0}, {4, 0}, {-1, 0}};

  const auto findings = check_physics_model(model, 3);
  const auto has = [&](std::string_view part) {
    return std::any_of(findings.begin(), findings.end(),
                       [&](const HkxPhysicsValidationFinding& finding) { return contains(finding.message, part); });
  };
  EXPECT_TRUE(has("bone index 4 is outside a 3-bone skeleton"));
  EXPECT_TRUE(has("is outside a 3-bone skeleton"));
  EXPECT_TRUE(has("is mapped to more than one rigid body"));
  EXPECT_TRUE(has("is mapped to more than one skeleton bone"));
  EXPECT_TRUE(has("bone index must be non-negative"));
}

TEST(HkxPhysicsClassifier, ClassFamilyFollowsThePrefix) {
  EXPECT_EQ(physics_class_family("hclClothData"), HkxPhysicsClassFamily::Cloth);
  EXPECT_EQ(physics_class_family("hknpCapsuleShape"), HkxPhysicsClassFamily::NewPhysics);
  EXPECT_EQ(physics_class_family("hkpRigidBody"), HkxPhysicsClassFamily::LegacyPhysics);
  EXPECT_FALSE(physics_class_family("hkaSkeleton").has_value());
}

TEST(HkxPhysicsClassifier, AClassListWithNoPhysicsAtAllIsSkeletonOnly) {
  HkxPhysicsLoadFacts facts;
  facts.class_counts = {{"hkaSkeleton", 1}, {"hkaAnimationContainer", 1}};
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::SkeletonOnly);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Warning);
  EXPECT_TRUE(contains(report.headline(), "has a skeleton but no ragdoll"));
  EXPECT_TRUE(contains(report.headline(), "hknpRagdollData"));
  EXPECT_TRUE(contains(report.summary(), "Fallout 4 keeps a ragdoll in the skeleton file itself"));
}

TEST(HkxPhysicsClassifier, AFileWithNeitherSkeletonNorPhysicsSaysSo) {
  HkxPhysicsLoadFacts facts;
  facts.class_counts = {{"hkRootLevelContainer", 1}};
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::SkeletonOnly);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Info);
  EXPECT_TRUE(contains(report.headline(), "no skeleton"));
  EXPECT_TRUE(contains(report.headline(), "hknpRagdollData"));
}

TEST(HkxPhysicsClassifier, OtherPhysicsClassesAreNamedInTheHint) {
  HkxPhysicsLoadFacts facts;
  facts.class_counts = {{"hkaSkeleton", 1}, {"hknpPhysicsSceneData", 2}};
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::SkeletonOnly);
  EXPECT_TRUE(contains(report.hint(), "hknpPhysicsSceneData x2"));
}

TEST(HkxPhysicsClassifier, LegacyHkpBodiesAreReportedAsLegacyPhysics) {
  HkxPhysicsLoadFacts facts;
  facts.file_version = 11;
  facts.pointer_size = 8;
  facts.class_counts = {{"hkpRigidBody", 12},
                        {"hkpSphereShape", 12},
                        {"hkpPhysicsSystem", 1},
                        {"hkpRagdollConstraintData", 11},
                        {"hkaSkeleton", 1}};
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::LegacyPhysics);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Error);
  EXPECT_TRUE(contains(report.headline(), "older hkp format"));
  EXPECT_TRUE(contains(report.headline(), "hkpRigidBody x12"));
  EXPECT_TRUE(contains(report.headline(), "hkpPhysicsSystem x1"));
  EXPECT_TRUE(contains(report.headline(), "hknpRagdollData"));
}

TEST(HkxPhysicsClassifier, LegacyConstraintDataAloneNeverSelectsLegacyPhysics) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.class_counts = {{"hknpRagdollData", 1},          {"hknpCapsuleShape", 18},
                        {"hknpPhysicsSceneData", 1},     {"hkaSkeletonMapper", 2},
                        {"hkpRagdollConstraintData", 9}, {"hkpLimitedHingeConstraintData", 8},
                        {"hkpPositionConstraintMotor", 1}};
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::Ok);
  EXPECT_FALSE(contains(report.headline(), "older hkp format"));
  EXPECT_TRUE(is_physics_constraint_class("hkpRagdollConstraintData"));
  EXPECT_FALSE(is_legacy_physics_class("hkpRagdollConstraintData"));
  EXPECT_TRUE(is_legacy_physics_class("hkpSphereShape"));
  EXPECT_TRUE(is_physics_shape_class("hknpCapsuleShape"));
}

TEST(HkxPhysicsClassifier, ARagdollWithNoUsableBodiesIsNoBodies) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.body_count = 18;
  facts.usable_bodies = 0;
  facts.skipped_bodies = 18;
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::NoBodies);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Error);
  EXPECT_TRUE(contains(report.headline(), "18 of its 18 bodies could not be read"));

  facts.body_count = 0;
  facts.skipped_bodies = 0;
  EXPECT_TRUE(contains(classify_physics_load(facts).headline(), "declares no rigid bodies at all"));
}

TEST(HkxPhysicsClassifier, AMissingMapperSaysWhichKind) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.mappings = 0;
  const HkxPhysicsLoadReport present = classify_physics_load(facts);
  EXPECT_EQ(present.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_EQ(present.severity(), HkxPhysicsLoadSeverity::Warning);
  EXPECT_TRUE(contains(present.headline(), "hkaSkeletonMapper"));
  EXPECT_TRUE(contains(present.headline(),
                       "is present but none of its mappings link this ragdoll to the animation skeleton"));
  EXPECT_TRUE(contains(present.headline(), "cannot follow the animation"));

  facts.class_counts = {{"hknpRagdollData", 1}, {"hknpCapsuleShape", 18}, {"hkaSkeleton", 1}};
  const HkxPhysicsLoadReport absent = classify_physics_load(facts);
  EXPECT_EQ(absent.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_FALSE(absent.mapper_present());
  EXPECT_TRUE(contains(absent.headline(), "no hkaSkeletonMapper links the animation skeleton to it"));
}

TEST(HkxPhysicsClassifier, UnsupportedShapeAndSkippedCountsSurviveAnotherPrimaryStatus) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.unsupported_shape_bodies = 3;
  facts.unsupported_shape_classes = {{"hknpConvexPolytopeShape", 3}};
  facts.skipped_constraints = 2;
  facts.mappings = 0;
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_TRUE(contains(report.headline(), "is present but none of its mappings link this ragdoll"));
  EXPECT_TRUE(any_finding(report, HkxPhysicsLoadSeverity::Info, "3 of 18 bodies use shapes FO4IDE cannot preview yet",
                          "hknpConvexPolytopeShape x3"));
  EXPECT_TRUE(any_finding(report, HkxPhysicsLoadSeverity::Info, "2 constraints entries could not be read"));
}

TEST(HkxPhysicsClassifier, ALegacyObjectBesideARagdollIsAnInfoFinding) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.class_counts = {{"hknpRagdollData", 1}, {"hknpCapsuleShape", 18}, {"hkaSkeletonMapper", 1},
                        {"hkpRigidBody", 2},    {"hkpSphereShape", 2}};
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::Ok);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Info);
  EXPECT_TRUE(any_finding(report, HkxPhysicsLoadSeverity::Info, "older hkp objects", "hkpRigidBody x2"));

  HkxPhysicsLoadFacts unusable = ragdoll_facts();
  unusable.class_counts = {{"hknpRagdollData", 1}, {"hkpRigidBody", 1}};
  unusable.body_count = 0;
  unusable.usable_bodies = 0;
  const HkxPhysicsLoadReport none = classify_physics_load(unusable);
  EXPECT_EQ(none.status, HkxPhysicsLoadStatus::NoBodies);
  EXPECT_TRUE(any_finding(none, HkxPhysicsLoadSeverity::Info, "older hkp objects", "hkpRigidBody x1"));
}

TEST(HkxPhysicsClassifier, UnsupportedShapesAndUnreadEntriesAreCountedNotSilent) {
  HkxPhysicsLoadFacts shapes = ragdoll_facts();
  shapes.unsupported_shape_bodies = 3;
  shapes.unsupported_shape_classes = {{"hknpSphereShape", 2}, {"hknpConvexPolytopeShape", 1}};
  shapes.shape_count = 15;
  const HkxPhysicsLoadReport report = classify_physics_load(shapes);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::UnsupportedShapes);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Warning);
  EXPECT_TRUE(contains(report.headline(), "3 of 18 bodies use shapes FO4IDE cannot preview yet"));
  EXPECT_TRUE(contains(report.headline(), "hknpSphereShape x2, hknpConvexPolytopeShape x1"));
  EXPECT_EQ(report.unsupported_shape_bodies, 3);

  HkxPhysicsLoadFacts body = ragdoll_facts();
  body.usable_bodies = 17;
  body.skipped_bodies = 1;
  const HkxPhysicsLoadReport skipped_body = classify_physics_load(body);
  EXPECT_EQ(skipped_body.status, HkxPhysicsLoadStatus::UnsupportedShapes);
  EXPECT_TRUE(contains(skipped_body.headline(), "1 body entry could not be read"));

  HkxPhysicsLoadFacts constraints = ragdoll_facts();
  constraints.skipped_constraints = 2;
  const HkxPhysicsLoadReport skipped = classify_physics_load(constraints);
  EXPECT_EQ(skipped.status, HkxPhysicsLoadStatus::UnsupportedShapes);
  EXPECT_TRUE(contains(skipped.headline(), "2 constraints entries could not be read"));
  EXPECT_TRUE(contains(skipped.diagnostics("x.hkx"), "constraints: 12 read, 2 skipped"));
}

TEST(HkxPhysicsClassifier, AContainerProblemOrParseStageNamesWhatWasDetected) {
  HkxPhysicsLoadFacts wrong;
  wrong.container_problem = "it uses 4-byte pointers, and Fallout 4 uses 8-byte pointers";
  const HkxPhysicsLoadReport container = classify_physics_load(wrong);
  EXPECT_EQ(container.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_EQ(container.severity(), HkxPhysicsLoadSeverity::Error);
  EXPECT_TRUE(contains(container.headline(), "not a Fallout 4 physics packfile"));
  EXPECT_TRUE(contains(container.headline(), "4-byte pointers"));

  HkxPhysicsLoadFacts parse;
  parse.parse_stage = "ragdoll";
  parse.parse_message = "the section table is not monotonic\nsecond line";
  const HkxPhysicsLoadReport stage = classify_physics_load(parse);
  EXPECT_EQ(stage.status, HkxPhysicsLoadStatus::ParseError);
  EXPECT_EQ(stage.failing_stage, "ragdoll");
  EXPECT_TRUE(contains(stage.headline(), "the section table is not monotonic"));
  EXPECT_FALSE(contains(stage.headline(), "second line"));

  parse.parse_message = " \n ";
  EXPECT_TRUE(contains(classify_physics_load(parse).headline(), "no detail was reported"));
}

TEST(HkxPhysicsClassifier, AnUnusualContainerThatStillReadsIsAnInfoFindingNotAStatus) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.file_version = 10;
  facts.container_notice = "it is a version 10 packfile, and Fallout 4 uses version 11";
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::Ok);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Info);
  EXPECT_TRUE(any_finding(report, HkxPhysicsLoadSeverity::Info, "version 10 packfile"));
}

TEST(HkxPhysicsClassifier, EveryStatusReadsInPlainWords) {
  HkxPhysicsLoadFacts skeleton_only;
  skeleton_only.class_counts = {{"hkaSkeleton", 1}};
  HkxPhysicsLoadFacts legacy;
  legacy.class_counts = {{"hkpRigidBody", 1}};
  HkxPhysicsLoadFacts unsupported = ragdoll_facts();
  unsupported.unsupported_shape_bodies = 1;
  HkxPhysicsLoadFacts mapper = ragdoll_facts();
  mapper.mappings = 0;
  HkxPhysicsLoadFacts no_bodies = ragdoll_facts();
  no_bodies.usable_bodies = 0;
  no_bodies.skipped_bodies = 1;
  no_bodies.body_count = 1;
  HkxPhysicsLoadFacts container;
  container.container_problem = "the file is empty";
  HkxPhysicsLoadFacts parse;
  parse.parse_stage = "container";
  parse.parse_message = "x";

  const std::vector<HkxPhysicsLoadReport> reports{
      classify_physics_load(ragdoll_facts()), classify_physics_load(skeleton_only), classify_physics_load(legacy),
      classify_physics_load(unsupported),     classify_physics_load(mapper),        classify_physics_load(no_bodies),
      classify_physics_load(container),       classify_physics_load(parse)};

  std::vector<int> statuses;
  for (const HkxPhysicsLoadReport& report : reports) {
    EXPECT_FALSE(report.headline().empty());
    EXPECT_GT(report.headline().size(), 40u) << report.status_slug();
    EXPECT_FALSE(contains(report.headline(), report.status_slug())) << report.status_slug();
    EXPECT_FALSE(contains(report.summary(), report.status_slug())) << report.status_slug();
    for (const HkxPhysicsLoadFinding& finding : report.findings) EXPECT_FALSE(finding.message.empty());
    statuses.push_back(static_cast<int>(report.status));
  }
  std::sort(statuses.begin(), statuses.end());
  EXPECT_EQ(std::unique(statuses.begin(), statuses.end()) - statuses.begin(), 8);
}

TEST(HkxPhysicsClassifier, ClassListingsAreOrderedByCountThenName) {
  EXPECT_EQ(describe_physics_classes({}), "none");
  EXPECT_EQ(describe_physics_classes({{"b", 2}, {"a", 2}, {"c", 5}, {"d", 1}}), "c x5, a x2, b x2, d x1");
  const HkxClassCounts physics = physics_classes_of({{"hkaSkeleton", 1}, {"hknpA", 2}, {"hkpB", 3}, {"other", 4}});
  EXPECT_EQ(physics.size(), 2u);
  EXPECT_EQ(physics.at("hknpA"), 2);
}

TEST(HkxPhysicsClassifier, TheDiagnosticsDumpCarriesEverythingSupportNeeds) {
  HkxPhysicsLoadFacts facts = ragdoll_facts();
  facts.class_counts["hkpRagdollConstraintData"] = 9;
  facts.class_counts["hkpLimitedHingeConstraintData"] = 8;
  facts.class_counts["hknpCapsuleShape"] = 19;
  facts.shape_count = 19;
  facts.constraint_count = 17;
  const HkxPhysicsLoadReport report = classify_physics_load(facts);
  const std::string text = report.diagnostics("C:\\Mods\\skeleton.hkx", 43296, "Radicle test build");

  EXPECT_TRUE(contains(text, "Radicle test build"));
  EXPECT_TRUE(contains(text, "file: C:\\Mods\\skeleton.hkx"));
  EXPECT_TRUE(contains(text, "size: 43296 bytes"));
  EXPECT_TRUE(contains(text, "file version: 11"));
  EXPECT_TRUE(contains(text, "pointer size: 8"));
  EXPECT_TRUE(contains(text, "status: OK (ok)"));
  EXPECT_TRUE(contains(text, "hknpCapsuleShape x19"));
  EXPECT_TRUE(contains(text, "mapper: present (18 mappings)"));
  EXPECT_TRUE(contains(text, "hknpBSMaterial: absent"));
  EXPECT_TRUE(contains(text, "shapes: 19 read"));
  EXPECT_TRUE(contains(text, "constraints: 17 read, 0 skipped"));
  EXPECT_TRUE(contains(text, "shape classes: hknpCapsuleShape x19"));
  EXPECT_TRUE(contains(text, "constraint classes: hkpRagdollConstraintData x9, hkpLimitedHingeConstraintData x8"));
  EXPECT_TRUE(contains(text, "class counts:"));
  EXPECT_NE(text.back(), '\n');

  HkxPhysicsLoadFacts wrong;
  wrong.pointer_size = 4;
  wrong.container_problem = "it uses 4-byte pointers, and Fallout 4 uses 8-byte pointers";
  const std::string failure = classify_physics_load(wrong).diagnostics("skeleton.hkx");
  EXPECT_TRUE(contains(failure, "status: WRONG CONTAINER (wrong-container)"));
  EXPECT_TRUE(contains(failure, "pointer size: 4"));
  EXPECT_TRUE(contains(failure, "size: unknown"));
  EXPECT_TRUE(contains(failure, "[error] This file is not a Fallout 4 physics packfile"));
  EXPECT_TRUE(contains(failure, "next: "));
}

TEST(HkxPhysicsRealData, VanillaSkeletonReportsTheMeasuredCensus) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxPhysicsLoadReport& report = loaded.report;
  EXPECT_EQ(report.status, HkxPhysicsLoadStatus::Ok);
  EXPECT_EQ(report.severity(), HkxPhysicsLoadSeverity::Info);
  EXPECT_TRUE(report.failing_stage.empty());
  EXPECT_TRUE(report.unsupported_shape_classes.empty());
  EXPECT_EQ(report.file_version, 11);
  EXPECT_EQ(report.pointer_size, 8);
  EXPECT_EQ(report.class_counts.at("hknpCapsuleShape"), 19);
  EXPECT_EQ(report.count_of("hknpRagdollData"), 1);
  EXPECT_EQ(report.shape_classes().at("hknpCapsuleShape"), 19);
  EXPECT_EQ(report.constraint_classes().at("hkpRagdollConstraintData"), 9);
  EXPECT_EQ(report.constraint_classes().at("hkpLimitedHingeConstraintData"), 8);
  EXPECT_TRUE(report.mapper_present());
  EXPECT_FALSE(report.material_present());
  EXPECT_EQ(report.body_count, 18);
  EXPECT_EQ(report.usable_bodies, 18);
  EXPECT_EQ(report.skipped_bodies, 0);
  EXPECT_EQ(report.skipped_constraints, 0);
  EXPECT_EQ(report.unsupported_shape_bodies, 0);
  EXPECT_EQ(report.mappings, 18);
  EXPECT_EQ(report.shape_count, 19);
  EXPECT_EQ(report.constraint_count, 17);

  EXPECT_EQ(loaded.model.bodies.size(), 18u);
  EXPECT_EQ(loaded.model.shapes.size(), 19u);
  EXPECT_EQ(loaded.model.constraints.size(), 17u);
  EXPECT_EQ(loaded.model.bone_bindings.size(), 18u);

  const std::string text = report.diagnostics("skeleton.hkx", static_cast<std::int64_t>(bytes.size()));
  EXPECT_TRUE(contains(text, "status: OK (ok)"));
  EXPECT_TRUE(contains(text, "mapper: present (18 mappings)"));
  EXPECT_TRUE(contains(text, "shapes: 19 read"));
  EXPECT_TRUE(contains(text, "constraints: 17 read, 0 skipped"));
}

TEST(HkxPhysicsRealData, VanillaBodiesAndShapesCarryTheirMeasuredValues) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxRagdollModel& model = loaded.model;

  const auto thigh = std::find_if(model.bodies.begin(), model.bodies.end(),
                                  [](const HkxRigidBody& body) { return body.name == "Ragdoll_NPC L Thigh"; });
  ASSERT_NE(thigh, model.bodies.end());
  EXPECT_EQ(thigh->id, 1);
  EXPECT_TRUE(within(HkxVec3{-6.6150898933410645f, 4.546909185592085E-4f, 68.91129302978516f}, thigh->position, 1e-3f));
  EXPECT_TRUE(within(HkxQuat{-0.10920292884111404f, 0.7402823567390442f, 0.09750235825777054f, 0.6561630964279175f},
                   thigh->rotation, 1e-3f));

  const auto shape_of_thigh = std::find_if(model.shapes.begin(), model.shapes.end(),
                                           [&](const HkxPhysicsShape& shape) { return shape.id == thigh->shape_id; });
  ASSERT_NE(shape_of_thigh, model.shapes.end());
  EXPECT_EQ(shape_of_thigh->kind, HkxPhysicsShapeKind::Capsule);

  const auto polytope = std::find_if(model.shapes.begin(), model.shapes.end(),
                                     [](const HkxPhysicsShape& shape) { return !shape.vertices.empty(); });
  ASSERT_NE(polytope, model.shapes.end());
  EXPECT_GE(polytope->vertices.size(), 7u);
  EXPECT_GE(polytope->face_rings.size(), 5u);
  for (const auto& ring : polytope->face_rings) {
    EXPECT_GE(ring.size(), 3u);
    for (const std::int32_t index : ring) {
      EXPECT_GE(index, 0);
      EXPECT_LT(static_cast<std::size_t>(index), polytope->vertices.size());
    }
  }
  for (const HkxPhysicsShape& shape : model.shapes) {
    for (const HkxVec3& vertex : shape.vertices) {
      for (float value : vertex) EXPECT_TRUE(std::isfinite(value));
    }
  }

  const auto pairs = [&] {
    std::vector<std::array<std::int32_t, 2>> out;
    for (const HkxPhysicsConstraint& constraint : model.constraints) out.push_back({constraint.body_a, constraint.body_b});
    return out;
  }();
  const std::vector<std::array<std::int32_t, 2>> expected{{1, 0},  {2, 0},   {3, 0},   {4, 1},   {5, 2},   {6, 3},
                                                          {7, 4},  {8, 5},   {9, 6},   {10, 9},  {11, 9},  {12, 9},
                                                          {13, 10}, {14, 11}, {15, 12}, {16, 13}, {17, 15}};
  EXPECT_EQ(pairs, expected);
  EXPECT_EQ(std::count_if(model.constraints.begin(), model.constraints.end(),
                          [](const HkxPhysicsConstraint& c) { return c.kind == HkxConstraintKind::Ragdoll; }),
            9);
  EXPECT_EQ(std::count_if(model.constraints.begin(), model.constraints.end(),
                          [](const HkxPhysicsConstraint& c) { return c.kind == HkxConstraintKind::LimitedHinge; }),
            8);
  EXPECT_EQ(std::count_if(model.constraints.begin(), model.constraints.end(),
                          [](const HkxPhysicsConstraint& c) { return c.kind == HkxConstraintKind::Unknown; }),
            0);
}

TEST(HkxPhysicsRealData, VanillaConstraintsCarryTheirMeasuredLimits) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const auto& constraints = loaded.model.constraints;

  const HkxPhysicsConstraint& ragdoll = constraints[0];
  EXPECT_EQ(ragdoll.kind, HkxConstraintKind::Ragdoll);
  EXPECT_EQ(ragdoll.body_a, 1);
  EXPECT_EQ(ragdoll.body_b, 0);
  ASSERT_TRUE(ragdoll.frame_a.has_value());
  ASSERT_TRUE(ragdoll.frame_b.has_value());
  EXPECT_EQ(ragdoll.frame_a->size(), 16u);
  EXPECT_TRUE(within(HkxVec3{0, 0, 0}, pivot(*ragdoll.frame_a), 1e-3f));
  EXPECT_TRUE(within(HkxVec3{2.897924787248485E-6f, 4.2483024299144745E-4f, 6.615072250366211f}, pivot(*ragdoll.frame_b),
                   1e-3f));
  EXPECT_EQ(ragdoll.twist_axis, 0);
  EXPECT_EQ(ragdoll.twist_ref_axis, 1);
  ASSERT_TRUE(ragdoll.twist_min_angle.has_value());
  ASSERT_TRUE(ragdoll.twist_max_angle.has_value());
  ASSERT_TRUE(ragdoll.cone_max_angle.has_value());
  EXPECT_TRUE(within(-0.2617993950843811f, *ragdoll.twist_min_angle, 1e-4f));
  EXPECT_TRUE(within(0.2617993950843811f, *ragdoll.twist_max_angle, 1e-4f));
  EXPECT_EQ(ragdoll.cone_twist_axis, 0);
  EXPECT_EQ(ragdoll.cone_ref_axis, 0);
  EXPECT_TRUE(within(0.5235987901687622f, *ragdoll.cone_max_angle, 1e-4f));

  const auto hinge = std::find_if(constraints.begin(), constraints.end(), [](const HkxPhysicsConstraint& c) {
    return c.kind == HkxConstraintKind::LimitedHinge && c.body_a == 4;
  });
  ASSERT_NE(hinge, constraints.end());
  EXPECT_EQ(hinge->body_b, 1);
  EXPECT_EQ(hinge->limit_axis, 0);
  EXPECT_TRUE(within(-1.919862151145935f, hinge->min_angle, 1e-4f));
  EXPECT_TRUE(within(0.031415924429893494f, hinge->max_angle, 1e-4f));
  ASSERT_TRUE(hinge->frame_b.has_value());
  EXPECT_TRUE(within(HkxVec3{31.592397689819336f, -0.03480081632733345f, -0.41684818267822266f}, pivot(*hinge->frame_b),
                   1e-3f));

  for (const HkxPhysicsConstraint& constraint : constraints) {
    EXPECT_TRUE(constraint.frame_a.has_value());
    EXPECT_TRUE(constraint.frame_b.has_value());
    if (constraint.kind == HkxConstraintKind::Ragdoll) {
      EXPECT_TRUE(constraint.twist_min_angle.has_value());
      EXPECT_TRUE(constraint.twist_max_angle.has_value());
      EXPECT_TRUE(constraint.cone_max_angle.has_value());
    } else {
      EXPECT_LT(constraint.min_angle, constraint.max_angle);
    }
  }
}

TEST(HkxPhysicsRealData, VanillaSkeletonAndBindingsAreMeasured) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxRagdollModel& model = loaded.model;

  for (const HkxRagdollBoneBinding& binding : model.bone_bindings) {
    EXPECT_EQ(binding.bone_index, binding.body_id);
    EXPECT_GE(binding.bone_index, 0);
    EXPECT_LE(binding.bone_index, 17);
  }

  ASSERT_TRUE(model.skeleton.has_value());
  const HkxSkeleton& skeleton = *model.skeleton;
  EXPECT_EQ(skeleton.name, "Ragdoll_NPC COM");
  ASSERT_EQ(skeleton.bone_names.size(), 18u);
  EXPECT_EQ(skeleton.bone_names[0], "Ragdoll_NPC COM");
  EXPECT_EQ(skeleton.bone_names[1], "Ragdoll_NPC L Thigh");
  EXPECT_EQ(skeleton.bone_names[5], "Ragdoll_NPC R Calf");
  EXPECT_EQ(skeleton.bone_names[8], "Ragdoll_NPC R Foot");
  EXPECT_EQ(skeleton.bone_names[13], "Ragdoll_NPC L Forearm");
  EXPECT_EQ(skeleton.parent_indices,
            (std::vector<std::int16_t>{-1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 9, 9, 9, 10, 11, 12, 13, 15}));

  ASSERT_EQ(skeleton.reference_pose.size(), 18u);
  EXPECT_TRUE(within(HkxVec3{-1.668929871811997E-5f, 2.1100042431498878E-5f, 68.91128540039062f},
                   skeleton.reference_pose[0].translation, 1e-3f));
  EXPECT_TRUE(within(HkxVec3{31.592391967773438f, -0.03480100631713867f, -0.41684722900390625f},
                   skeleton.reference_pose[4].translation, 1e-3f));
  EXPECT_TRUE(within(HkxQuat{-4.6361520844584447E-7f, -0.7071065306663513f, -4.6361529371097276E-7f, 0.7071069478988647f},
                   skeleton.reference_pose[0].rotation, 1e-4f));
  EXPECT_TRUE(within(HkxQuat{-0.008273639716207981f, 0.9874358773231506f, 0.14616334438323975f, -0.05948099493980408f},
                   skeleton.reference_pose[1].rotation, 1e-4f));
  for (const HkxQsTransform& pose : skeleton.reference_pose) EXPECT_TRUE(within(HkxVec3{1, 1, 1}, pose.scale, 1e-3f));

  for (std::size_t i = 0; i < skeleton.parent_indices.size(); ++i) {
    const int parent = skeleton.parent_indices[i];
    if (parent >= 0) {
      EXPECT_LT(static_cast<std::size_t>(parent), i);
    }
  }

  for (const HkxRagdollBoneBinding& binding : model.bone_bindings) {
    const auto body = std::find_if(model.bodies.begin(), model.bodies.end(),
                                   [&](const HkxRigidBody& candidate) { return candidate.id == binding.body_id; });
    ASSERT_NE(body, model.bodies.end());
    EXPECT_FALSE(body->name.empty());
    EXPECT_FALSE(skeleton.bone_names[static_cast<std::size_t>(binding.bone_index)].empty());
  }

  const std::vector<HkxBoneFrame> world = world_reference_frames(skeleton);
  ASSERT_EQ(world.size(), 18u);
  EXPECT_TRUE(within(HkxVec3{-1.668929871811997E-5f, 2.1100042431498878E-5f, 68.91128540039062f}, world[0].position, 1e-3f));
  EXPECT_TRUE(within(HkxVec3{-6.6150898933410645f, 4.546909185592E-4f, 68.91129302978516f}, world[1].position, 1e-3f));
  for (const HkxRagdollBoneBinding& binding : model.bone_bindings) {
    const auto body = std::find_if(model.bodies.begin(), model.bodies.end(),
                                   [&](const HkxRigidBody& candidate) { return candidate.id == binding.body_id; });
    ASSERT_NE(body, model.bodies.end());
    EXPECT_TRUE(within(world[static_cast<std::size_t>(binding.bone_index)].position, body->position, 5e-2f))
        << "bone " << binding.bone_index;
  }
}

TEST(HkxPhysicsRealData, VanillaMotionCentersOfMassFollowTheMotionIdNotTheBodyOrder) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const auto& bodies = loaded.model.bodies;

  EXPECT_TRUE(within(HkxVec3{0.020323412492871284f, -0.0491427443921566f, 69.19477844238281f}, bodies[0].center_of_mass,
                   1e-3f));
  EXPECT_TRUE(within(HkxVec3{-8.432622909545898f, -0.5323612093925476f, 53.22739028930664f}, bodies[1].center_of_mass,
                   1e-3f));
  EXPECT_TRUE(within(HkxVec3{8.431289672851562f, -0.5326367616653442f, 53.22724914550781f}, bodies[2].center_of_mass,
                   1e-3f));
  EXPECT_TRUE(within(HkxVec3{-11.501089096069336f, -3.5246798992156982f, 21.728782653808594f}, bodies[4].center_of_mass,
                   1e-3f));
  for (const HkxRigidBody& body : bodies) {
    EXPECT_NE(body.center_of_mass, (HkxVec3{0, 0, 0})) << body.name;
    for (float value : body.center_of_mass) EXPECT_TRUE(std::isfinite(value));
  }
  EXPECT_GT(std::fabs(bodies[1].center_of_mass[2] - bodies[1].position[2]), 10.0f);
}

TEST(HkxPhysicsRealData, VanillaMapperLinksTheAnimationSkeletonToTheRagdoll) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxRagdollModel& model = loaded.model;

  ASSERT_EQ(model.mappings.size(), 18u);
  ASSERT_TRUE(model.animation_skeleton.has_value());
  EXPECT_EQ(model.animation_skeleton->name, "Root");
  EXPECT_EQ(model.animation_skeleton->bone_names.size(), 95u);

  std::vector<std::int32_t> covered;
  for (const HkxRagdollMapping& mapping : model.mappings) covered.push_back(mapping.bone_b);
  std::sort(covered.begin(), covered.end());
  for (std::size_t i = 0; i < covered.size(); ++i) EXPECT_EQ(covered[i], static_cast<std::int32_t>(i));

  EXPECT_EQ(model.mappings[0].bone_a, 1);
  EXPECT_EQ(model.mappings[0].bone_b, 0);
  EXPECT_EQ(model.mappings[1].bone_a, 3);
  EXPECT_EQ(model.mappings[1].bone_b, 1);
  EXPECT_TRUE(within(HkxVec3{7.62939453125E-6f, -9.5367431640625E-7f, -3.62396240234375E-5f},
                   model.mappings[1].a_from_b_translation, 1e-6f));
  EXPECT_TRUE(within(HkxQuat{-3.6200135946273804E-4f, -0.006596028804779053f, 5.562007427215576E-4f, 0.9999775886535645f},
                   model.mappings[1].a_from_b_rotation, 1e-6f));
  EXPECT_EQ(model.mappings[17].bone_a, 26);
  EXPECT_EQ(model.mappings[17].bone_b, 17);
}

TEST(HkxPhysicsRealData, MappingTheAnimationReferencePoseYieldsTheRagdollReference) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  const HkxRagdollModel& model = loaded.model;
  ASSERT_TRUE(model.animation_skeleton.has_value());
  ASSERT_TRUE(model.skeleton.has_value());

  std::vector<HkxBoneFrame> mapped;
  ASSERT_TRUE(map_pose(model, world_frames(*model.animation_skeleton), mapped));
  const std::vector<HkxBoneFrame> expected = world_reference_frames(*model.skeleton);
  ASSERT_EQ(mapped.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_TRUE(within(expected[i].position, mapped[i].position, 5e-3f)) << "bone " << i;
    EXPECT_GE(std::fabs(dot(unit(mapped[i].rotation), expected[i].rotation)), 0.999f) << "bone " << i;
  }
}

TEST(HkxPhysicsRealData, TheExtractedModelPassesTheValidator) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  ASSERT_EQ(loaded.status, HkxStatus::Ok);
  for (const HkxPhysicsValidationFinding& finding : check_physics_model(loaded.model, 18)) {
    EXPECT_NE(finding.level, HkxPhysicsValidationLevel::Error) << finding.where << ": " << finding.message;
  }
}

TEST(HkxPhysicsRealData, TheClothHairFixtureIsSkeletonOnly) {
  std::vector<std::byte> bytes;
  load_fixture(kClothHairFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;
  const Loaded loaded = load(bytes);
  EXPECT_EQ(loaded.status, HkxStatus::MissingObject);
  EXPECT_TRUE(loaded.model.bodies.empty());
  EXPECT_EQ(loaded.report.status, HkxPhysicsLoadStatus::SkeletonOnly);
  EXPECT_EQ(loaded.report.file_version, 11);
  EXPECT_EQ(loaded.report.count_of("hkaSkeleton"), 1);
}

TEST(HkxPhysicsRealData, RenamedVanillaClassesChangeTheVerdict) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  std::vector<std::byte> four = bytes;
  four[0x10] = std::byte{4};
  const Loaded refused = load(four);
  EXPECT_EQ(refused.report.status, HkxPhysicsLoadStatus::WrongContainer);
  EXPECT_EQ(refused.report.pointer_size, 4);
  EXPECT_TRUE(refused.model.bodies.empty());

  std::vector<std::byte> legacy = bytes;
  ASSERT_GT(replace_text(legacy, "hknpRagdollData", "hkpCapsuleShape"), 0);
  const Loaded old_file = load(legacy);
  EXPECT_EQ(old_file.report.status, HkxPhysicsLoadStatus::LegacyPhysics);
  EXPECT_EQ(old_file.report.count_of("hknpRagdollData"), 0);
  EXPECT_EQ(old_file.report.count_of("hkpCapsuleShape"), 1);

  std::vector<std::byte> bare = bytes;
  ASSERT_GT(replace_text(bare, "hkaSkeleton", "hkxSkeleton"), 0);
  const Loaded no_mapper = load(bare);
  ASSERT_EQ(no_mapper.status, HkxStatus::Ok);
  EXPECT_EQ(no_mapper.model.bodies.size(), 18u);
  EXPECT_EQ(no_mapper.report.status, HkxPhysicsLoadStatus::MapperMissing);
  EXPECT_FALSE(no_mapper.report.mapper_present());
}

namespace {

PhysicsSpec editable_spec() {
  PhysicsSpec spec = default_physics_spec();
  spec.inverse_masses = {0.5f, 0.25f};
  spec.inverse_inertias = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
  spec.shapes[0].a = {0.0f, 0.0f, -1.0f};
  spec.shapes[0].b = {0.0f, 0.0f, 1.0f};
  spec.shapes[0].radius = 0.25f;
  return spec;
}

HkxRagdollMeasures measures_of(const std::vector<std::byte>& bytes) {
  HkxPackfile pack;
  EXPECT_EQ(read_packfile(bytes, pack), HkxStatus::Ok);
  HkxRagdollMeasures measures;
  EXPECT_EQ(read_ragdoll_measures(pack, measures), HkxStatus::Ok);
  return measures;
}

std::size_t differing_bytes(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) count += a[i] != b[i] ? 1 : 0;
  return count;
}

}  // namespace

// Constraint: detects a measures reader that takes mass or capsule data from the wrong member, which feeds the preview and the editor wrong numbers.
TEST(HkxRagdollEdit, MeasuresCarryMotionMassAndCapsuleEndpoints) {
  const std::vector<std::byte> bytes = physics_packfile(editable_spec());
  const HkxRagdollMeasures measures = measures_of(bytes);
  ASSERT_EQ(measures.bodies.size(), 2u);
  EXPECT_EQ(measures.bodies[0].motion, 1);
  EXPECT_FLOAT_EQ(measures.bodies[0].mass(), 4.0f);
  EXPECT_FLOAT_EQ(measures.bodies[1].mass(), 2.0f);
  EXPECT_EQ(measures.bodies[0].inverse_inertia, (HkxVec3{4.0f, 5.0f, 6.0f}));
  EXPECT_EQ(measures.bodies[0].center_of_mass, (HkxVec3{20.0f, 21.0f, 22.0f}));
  ASSERT_EQ(measures.capsules.size(), 2u);
  EXPECT_EQ(measures.capsules[0].shape, 1);
  EXPECT_EQ(measures.capsules[0].a, (HkxVec3{0.0f, 0.0f, -1.0f}));
  EXPECT_EQ(measures.capsules[0].b, (HkxVec3{0.0f, 0.0f, 1.0f}));
  EXPECT_FLOAT_EQ(measures.capsules[0].radius, 0.25f);
}

// Constraint: detects an edit that lands on the wrong field, resizes a row or drops untouched data, which only shows after the file is reopened.
TEST(HkxRagdollEdit, EditedBodyConstraintAndBoneMapReadBackAndNothingElseChanges) {
  const std::vector<std::byte> source = physics_packfile(editable_spec());
  const Loaded before = load(source);
  ASSERT_EQ(before.status, HkxStatus::Ok);

  HkxRagdollEdits edits;
  HkxRagdollBodyEdit body;
  body.body = 0;
  body.position = HkxVec3{7.0f, 8.0f, 9.0f};
  body.rotation = HkxQuat{0.0f, 0.0f, 2.0f, 0.0f};
  body.center_of_mass = HkxVec3{1.5f, 2.5f, 3.5f};
  body.mass = 8.0f;
  edits.bodies.push_back(body);
  HkxRagdollConstraintEdit constraint;
  constraint.constraint = 0;
  constraint.twist_min_angle = -0.25f;
  constraint.twist_max_angle = 0.5f;
  constraint.cone_max_angle = 1.0f;
  constraint.frame_a = std::vector<float>(16, 0.125f);
  edits.constraints.push_back(constraint);
  edits.bone_map.push_back({0, 1});

  const HkxRagdollEditResult result = apply_ragdoll_edits(source, edits);
  ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
  ASSERT_EQ(result.bytes.size(), source.size());
  EXPECT_GT(differing_bytes(source, result.bytes), 0u);
  EXPECT_LE(differing_bytes(source, result.bytes), 200u);

  const Loaded after = load(result.bytes);
  ASSERT_EQ(after.status, HkxStatus::Ok);
  EXPECT_EQ(after.model.bodies[0].position, (HkxVec3{7.0f, 8.0f, 9.0f}));
  EXPECT_EQ(after.model.bodies[0].rotation, (HkxQuat{0.0f, 0.0f, 1.0f, 0.0f}));
  EXPECT_EQ(after.model.bodies[0].center_of_mass, (HkxVec3{1.5f, 2.5f, 3.5f}));
  EXPECT_EQ(after.model.bodies[1].position, before.model.bodies[1].position);
  EXPECT_EQ(after.model.bodies[1].center_of_mass, before.model.bodies[1].center_of_mass);
  EXPECT_EQ(after.model.bodies[0].name, before.model.bodies[0].name);
  const HkxPhysicsConstraint& edited = after.model.constraints[0];
  EXPECT_FLOAT_EQ(*edited.twist_min_angle, -0.25f);
  EXPECT_FLOAT_EQ(*edited.twist_max_angle, 0.5f);
  EXPECT_FLOAT_EQ(*edited.cone_max_angle, 1.0f);
  EXPECT_EQ(*edited.frame_a, std::vector<float>(16, 0.125f));
  EXPECT_EQ(edited.frame_b, before.model.constraints[0].frame_b);
  EXPECT_EQ(edited.twist_axis, before.model.constraints[0].twist_axis);
  ASSERT_EQ(after.model.bone_bindings.size(), 2u);
  EXPECT_EQ(after.model.bone_bindings[0].body_id, 1);
  EXPECT_EQ(after.model.bone_bindings[1].body_id, 1);

  const HkxRagdollMeasures measures = measures_of(result.bytes);
  EXPECT_FLOAT_EQ(measures.bodies[0].mass(), 8.0f);
  EXPECT_EQ(measures.bodies[0].inverse_inertia, (HkxVec3{2.0f, 2.5f, 3.0f}));
  EXPECT_FLOAT_EQ(measures.bodies[1].mass(), 2.0f);
  EXPECT_EQ(measures.capsules, measures_of(source).capsules);

  HkxRagdollEdits again;
  HkxRagdollConstraintEdit second;
  second.constraint = 0;
  second.cone_max_angle = 0.5f;
  again.constraints.push_back(second);
  const HkxRagdollEditResult reopened = apply_ragdoll_edits(result.bytes, again);
  ASSERT_EQ(reopened.status, HkxRagdollEditStatus::Ok) << reopened.message;
  EXPECT_FLOAT_EQ(*load(reopened.bytes).model.constraints[0].cone_max_angle, 0.5f);
}

// Constraint: detects cloth objects blocking ragdoll edits or changing: the cloth object must come through unchanged.
TEST(HkxRagdollEdit, ClothObjectsDoNotBlockEditingAndAreKeptByteForByte) {
  const std::vector<std::byte> base = physics_packfile(editable_spec());
  const HkxClassLayout* cloth = find_layout("hclAction");
  ASSERT_NE(cloth, nullptr);
  const std::vector<std::byte> instance(cloth->size, std::byte{0x5A});
  HkxAppend appended;
  ASSERT_EQ(append_object(base, "hclAction", cloth->signature, instance, appended), HkxStatus::Ok);

  HkxRagdollEdits edits;
  HkxRagdollConstraintEdit constraint;
  constraint.constraint = 0;
  constraint.cone_max_angle = 0.75f;
  edits.constraints.push_back(constraint);
  const HkxRagdollEditResult result = apply_ragdoll_edits(appended.bytes, edits);
  ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
  ASSERT_EQ(result.bytes.size(), appended.bytes.size());

  HkxPackfile pack;
  ASSERT_EQ(read_packfile(result.bytes, pack), HkxStatus::Ok);
  ASSERT_GT(pack.objects.size(), appended.object_index);
  const HkxObjectView& object = pack.objects[appended.object_index];
  EXPECT_EQ(object.class_name, "hclAction");
  ASSERT_GE(object.data.size(), instance.size());
  EXPECT_TRUE(std::equal(instance.begin(), instance.end(), object.data.begin()));
  EXPECT_FLOAT_EQ(*load(result.bytes).model.constraints[0].cone_max_angle, 0.75f);
}

// Constraint: detects a refused edit that still returns bytes or changes the input, which lets a half-applied file reach disk.
TEST(HkxRagdollEdit, RefusalsNameTheirReasonAndNeverMutateOrReturnBytes) {
  const std::vector<std::byte> source = physics_packfile(editable_spec());
  const auto refused = [&](HkxRagdollEdits edits, HkxRagdollEditStatus status) {
    std::vector<std::byte> input = source;
    const HkxRagdollEditResult result = apply_ragdoll_edits(input, edits);
    EXPECT_EQ(result.status, status) << result.message;
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.bytes.empty());
    EXPECT_EQ(input, source);
  };
  const auto constraint_edit = [](auto fill) {
    HkxRagdollEdits edits;
    HkxRagdollConstraintEdit edit;
    edit.constraint = 0;
    fill(edit);
    edits.constraints.push_back(edit);
    return edits;
  };
  const auto body_edit = [](auto fill) {
    HkxRagdollEdits edits;
    HkxRagdollBodyEdit edit;
    edit.body = 0;
    fill(edit);
    edits.bodies.push_back(edit);
    return edits;
  };

  refused({}, HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.twist_min_angle = 1.0f; e.twist_max_angle = 0.5f; }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.twist_max_angle = 4.0f; }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.cone_max_angle = -0.1f; }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.frame_a = std::vector<float>(15, 0.0f); }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.body_a = 5; }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.constraint = 7; e.cone_max_angle = 0.5f; }), HkxRagdollEditStatus::BadTarget);
  refused(constraint_edit([](auto& e) { e.constraint = -1; e.cone_max_angle = 0.5f; }), HkxRagdollEditStatus::BadTarget);
  refused(constraint_edit([](auto& e) { e.twist_min_angle = std::nanf(""); }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.cone_max_angle = std::numeric_limits<float>::infinity(); }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.cone_max_angle = 4.0f; }), HkxRagdollEditStatus::BadValue);
  refused(constraint_edit([](auto& e) { e.min_angle = -1.0f; }), HkxRagdollEditStatus::FieldAbsent);
  refused(body_edit([](auto& e) { e.position = HkxVec3{std::nanf(""), 0.0f, 0.0f}; }), HkxRagdollEditStatus::BadValue);
  refused(body_edit([](auto& e) { e.rotation = HkxQuat{0.0f, 0.0f, 0.0f, 0.0f}; }), HkxRagdollEditStatus::BadValue);
  refused(body_edit([](auto& e) { e.mass = 0.0f; }), HkxRagdollEditStatus::BadValue);
  refused(body_edit([](auto& e) { e.body = 9; e.mass = 1.0f; }), HkxRagdollEditStatus::BadTarget);

  HkxRagdollEdits bone;
  bone.bone_map.push_back({0, 9});
  refused(bone, HkxRagdollEditStatus::BadValue);
  bone.bone_map = {{5, 0}};
  refused(bone, HkxRagdollEditStatus::BadTarget);
  bone.bone_map = {{0, 1}, {0, 0}};
  refused(bone, HkxRagdollEditStatus::BadValue);

  HkxRagdollEdits shape;
  HkxRagdollShapeEdit capsule;
  capsule.shape = 1;
  capsule.radius = 0.5f;
  shape.shapes.push_back(capsule);
  shape.bodies.push_back({});
  refused(shape, HkxRagdollEditStatus::ShapeGeometryUnsupported);

  EXPECT_EQ(apply_ragdoll_edits(bytes_of("not a packfile at all"), body_edit([](auto& e) { e.mass = 1.0f; })).status,
            HkxRagdollEditStatus::NotARagdoll);

  PhysicsSpec massless = editable_spec();
  massless.inverse_masses = {0.5f, 0.0f};
  const std::vector<std::byte> pinned = physics_packfile(massless);
  const HkxRagdollEditResult heavier = apply_ragdoll_edits(pinned, body_edit([](auto& e) { e.mass = 3.0f; }));
  EXPECT_EQ(heavier.status, HkxRagdollEditStatus::BadValue) << heavier.message;
  EXPECT_TRUE(heavier.bytes.empty());
}

// Constraint: detects a mass edit on a shared motion row, which silently changes another body.
TEST(HkxRagdollEdit, ASharedMotionRowRefusesMassAndCenterOfMassButAllowsPose) {
  PhysicsSpec spec = editable_spec();
  spec.bodies[0].motion = 0;
  spec.bodies[1].motion = 0;
  const std::vector<std::byte> source = physics_packfile(spec);

  HkxRagdollEdits mass;
  HkxRagdollBodyEdit body;
  body.body = 0;
  body.mass = 3.0f;
  mass.bodies.push_back(body);
  const HkxRagdollEditResult refused = apply_ragdoll_edits(source, mass);
  EXPECT_EQ(refused.status, HkxRagdollEditStatus::SharedStorage);
  EXPECT_TRUE(refused.bytes.empty());

  HkxRagdollEdits pose;
  body.mass.reset();
  body.position = HkxVec3{1.0f, 1.0f, 1.0f};
  pose.bodies.push_back(body);
  const HkxRagdollEditResult moved = apply_ragdoll_edits(source, pose);
  ASSERT_EQ(moved.status, HkxRagdollEditStatus::Ok) << moved.message;
  EXPECT_EQ(load(moved.bytes).model.bodies[0].position, (HkxVec3{1.0f, 1.0f, 1.0f}));
}

// Constraint: detects a hinge limit edit that misses the limited-hinge atom or is accepted on a kind with none.
TEST(HkxRagdollEdit, LimitedHingeLimitsAreEditableAndOtherKindsRefuse) {
  PhysicsSpec spec = editable_spec();
  PhysConstraintSpec limited;
  limited.body_a = 1;
  limited.body_b = 0;
  limited.data_class = "hkpLimitedHingeConstraintData";
  limited.frame_a.assign(16, 1.0f);
  limited.frame_b.assign(16, 2.0f);
  limited.hinge_min = -1.0f;
  limited.hinge_max = 1.0f;
  spec.constraints.push_back(limited);
  const std::vector<std::byte> source = physics_packfile(spec);

  HkxRagdollEdits edits;
  HkxRagdollConstraintEdit edit;
  edit.constraint = 1;
  edit.min_angle = -0.5f;
  edit.max_angle = 0.75f;
  edits.constraints.push_back(edit);
  const HkxRagdollEditResult result = apply_ragdoll_edits(source, edits);
  ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
  const Loaded after = load(result.bytes);
  EXPECT_FLOAT_EQ(after.model.constraints[1].min_angle, -0.5f);
  EXPECT_FLOAT_EQ(after.model.constraints[1].max_angle, 0.75f);
  EXPECT_FLOAT_EQ(*after.model.constraints[0].twist_min_angle, -0.5f);

  edits.constraints[0].constraint = 0;
  EXPECT_EQ(apply_ragdoll_edits(source, edits).status, HkxRagdollEditStatus::FieldAbsent);

  const auto hinge_edit = [&](auto fill) {
    HkxRagdollEdits one;
    HkxRagdollConstraintEdit row;
    row.constraint = 1;
    fill(row);
    one.constraints.push_back(row);
    return apply_ragdoll_edits(source, one);
  };
  const auto refused = [&](auto fill, HkxRagdollEditStatus status) {
    const HkxRagdollEditResult outcome = hinge_edit(fill);
    EXPECT_EQ(outcome.status, status) << outcome.message;
    EXPECT_TRUE(outcome.bytes.empty());
  };
  refused([](auto& e) { e.min_angle = 1.5f; }, HkxRagdollEditStatus::BadValue);
  refused([](auto& e) { e.max_angle = -1.5f; }, HkxRagdollEditStatus::BadValue);
  refused([](auto& e) { e.max_angle = 4.0f; }, HkxRagdollEditStatus::BadValue);
  refused([](auto& e) { e.min_angle = std::nanf(""); }, HkxRagdollEditStatus::BadValue);
  refused([](auto& e) { e.cone_max_angle = 0.5f; }, HkxRagdollEditStatus::FieldAbsent);
  refused([](auto& e) { e.twist_min_angle = -0.5f; }, HkxRagdollEditStatus::FieldAbsent);

  const HkxRagdollEditResult upper_only = hinge_edit([](auto& e) { e.max_angle = 0.25f; });
  ASSERT_EQ(upper_only.status, HkxRagdollEditStatus::Ok) << upper_only.message;
  const Loaded narrowed = load(upper_only.bytes);
  EXPECT_FLOAT_EQ(narrowed.model.constraints[1].min_angle, -1.0f);
  EXPECT_FLOAT_EQ(narrowed.model.constraints[1].max_angle, 0.25f);
}

namespace {

HkxVec3 resized_end(const HkxVec3& a, const HkxVec3& b, double scale, bool far_end) {
  HkxVec3 out{};
  for (std::size_t i = 0; i < 3; ++i) {
    const double centre = (static_cast<double>(a[i]) + b[i]) * 0.5;
    out[i] = static_cast<float>(centre + (far_end ? 0.5 : -0.5) * scale * (static_cast<double>(b[i]) - a[i]));
  }
  return out;
}

HkxRagdollShapeEdit resize_edit(const HkxRagdollCapsuleMeasure& capsule, double length_scale, float radius_scale) {
  HkxRagdollShapeEdit edit;
  edit.shape = capsule.shape;
  edit.a = resized_end(capsule.a, capsule.b, length_scale, false);
  edit.b = resized_end(capsule.a, capsule.b, length_scale, true);
  edit.radius = capsule.radius * radius_scale;
  return edit;
}

std::array<double, 3> sorted_capsule_inertia(double mass, double radius, double length) {
  const double pi = 3.14159265358979323846;
  const double volume = pi * radius * radius * length + 4.0 / 3.0 * pi * radius * radius * radius;
  const double density = mass / volume;
  const double cylinder = density * pi * radius * radius * length;
  const double spheres = density * 4.0 / 3.0 * pi * radius * radius * radius;
  const double axial = cylinder * radius * radius / 2.0 + spheres * 2.0 / 5.0 * radius * radius;
  const double transverse = cylinder * (length * length / 12.0 + radius * radius / 4.0) +
                            spheres * (2.0 / 5.0 * radius * radius + length * length / 4.0 + 3.0 * radius * length / 8.0);
  std::array<double, 3> lanes{axial, transverse, transverse};
  std::sort(lanes.begin(), lanes.end());
  return lanes;
}

const HkxPhysicsShape* shape_with_id(const HkxRagdollModel& model, std::int32_t id) {
  for (const HkxPhysicsShape& shape : model.shapes) {
    if (shape.id == id) return &shape;
  }
  return nullptr;
}

const HkxRigidBody* owner_of(const HkxRagdollModel& model, std::int32_t shape) {
  for (const HkxRigidBody& body : model.bodies) {
    if (body.shape_id == shape) return &body;
  }
  return nullptr;
}

double length_between(const HkxVec3& a, const HkxVec3& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < 3; ++i) sum += (static_cast<double>(b[i]) - a[i]) * (static_cast<double>(b[i]) - a[i]);
  return std::sqrt(sum);
}

}  // namespace

// Constraint: detects a capsule model that disagrees with the vanilla polytope or compressed mass layout, which would make every real capsule uneditable or silently rewritten wrong.
TEST(HkxRagdollEditRealData, EveryVanillaCapsuleMatchesTheCapsuleModelAndMaterialsDecodeAsTruncatedFloats) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const HkxRagdollMeasures measures = measures_of(bytes);
  ASSERT_EQ(measures.capsules.size(), 19u);
  for (const HkxRagdollCapsuleMeasure& capsule : measures.capsules) {
    EXPECT_TRUE(capsule.editable) << "shape " << capsule.shape << ": " << capsule.reason;
  }
  ASSERT_EQ(measures.materials.size(), 18u);
  for (const HkxRagdollMaterialMeasure& material : measures.materials) {
    EXPECT_EQ(material.dynamic_friction, 0.59765625f);
    EXPECT_EQ(material.static_friction, 0.59765625f);
    EXPECT_EQ(material.restitution, 0.19921875f);
    EXPECT_FALSE(material.shared);
  }
}

// Constraint: detects a resize that leaves a stale vertex, plane offset, volume, inertia lane or motion-row inertia, or that disturbs another capsule.
TEST(HkxRagdollEditRealData, ResizingAnyVanillaCapsuleRederivesItsPolytopeMassAndMotionRowAndNothingElse) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const Loaded before = load(bytes);
  ASSERT_EQ(before.status, HkxStatus::Ok);
  const HkxRagdollMeasures measures = measures_of(bytes);
  for (const HkxRagdollCapsuleMeasure& capsule : measures.capsules) {
    SCOPED_TRACE("shape " + std::to_string(capsule.shape));
    HkxRagdollEdits edits;
    edits.shapes.push_back(resize_edit(capsule, 0.8, 1.25f));
    const HkxRagdollEditResult result = apply_ragdoll_edits(bytes, edits);
    ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
    ASSERT_EQ(result.bytes.size(), bytes.size());
    EXPECT_GT(differing_bytes(bytes, result.bytes), 0u);
    EXPECT_LE(differing_bytes(bytes, result.bytes), 200u);

    const Loaded after = load(result.bytes);
    ASSERT_EQ(after.status, HkxStatus::Ok);
    const HkxRagdollMeasures after_measures = measures_of(result.bytes);
    ASSERT_EQ(after_measures.capsules.size(), measures.capsules.size());
    for (std::size_t i = 0; i < measures.capsules.size(); ++i) {
      if (measures.capsules[i].shape != capsule.shape) {
        EXPECT_EQ(after_measures.capsules[i], measures.capsules[i]);
        continue;
      }
      const HkxRagdollCapsuleMeasure& edited = after_measures.capsules[i];
      EXPECT_TRUE(edited.editable) << edited.reason;
      EXPECT_EQ(edited.a, *edits.shapes[0].a);
      EXPECT_EQ(edited.b, *edits.shapes[0].b);
      EXPECT_EQ(edited.radius, *edits.shapes[0].radius);
    }
    for (const HkxPhysicsShape& shape : before.model.shapes) {
      if (shape.id == capsule.shape) continue;
      ASSERT_NE(shape_with_id(after.model, shape.id), nullptr);
      EXPECT_EQ(shape_with_id(after.model, shape.id)->vertices, shape.vertices);
    }

    const HkxPhysicsShape* shape = shape_with_id(after.model, capsule.shape);
    ASSERT_NE(shape, nullptr);
    ASSERT_EQ(shape->vertices.size(), 8u);
    const HkxVec3 a = *edits.shapes[0].a;
    const HkxVec3 b = *edits.shapes[0].b;
    const double radius = *edits.shapes[0].radius;
    const double length = length_between(a, b);
    const double pad = radius / 99.0;
    const double half = length / 2.0 + pad;
    const double tolerance = 1.0e-4 * std::max(1.0, half);
    for (const HkxVec3& vertex : shape->vertices) {
      double along = 0.0;
      double off_axis = 0.0;
      for (std::size_t i = 0; i < 3; ++i) {
        const double axis = (static_cast<double>(b[i]) - a[i]) / length;
        const double centred = vertex[i] - (static_cast<double>(a[i]) + b[i]) / 2.0;
        along += centred * axis;
        off_axis += centred * centred;
      }
      off_axis = std::sqrt(std::max(0.0, off_axis - along * along));
      EXPECT_NEAR(std::fabs(along), half, tolerance);
      EXPECT_NEAR(off_axis, pad * std::sqrt(2.0), tolerance);
    }

    const HkxRigidBody* owner = owner_of(before.model, capsule.shape);
    if (owner == nullptr) continue;
    const auto index = static_cast<std::size_t>(owner->id);
    const HkxRagdollBodyMeasure& old_body = measures.bodies[index];
    const HkxRagdollBodyMeasure& new_body = after_measures.bodies[index];
    EXPECT_EQ(new_body.inverse_mass, old_body.inverse_mass);
    EXPECT_EQ(new_body.center_of_mass, old_body.center_of_mass);
    const auto expected = sorted_capsule_inertia(old_body.mass(), radius, length);
    std::array<double, 3> stored{};
    for (std::size_t lane = 0; lane < 3; ++lane) stored[lane] = 1.0 / (1.5 * static_cast<double>(new_body.inverse_inertia[lane]));
    std::sort(stored.begin(), stored.end());
    for (std::size_t lane = 0; lane < 3; ++lane) EXPECT_NEAR(stored[lane], expected[lane], 2.0e-3 * expected[lane]);
  }
}

// Constraint: detects a radius-only edit that moves the endpoints or rewrites the centre, or an edit that restates the stored values and still changes bytes.
TEST(HkxRagdollEditRealData, RadiusOnlyAndUnchangedCapsuleEditsTouchOnlyWhatTheyMust) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const HkxRagdollMeasures measures = measures_of(bytes);
  const HkxRagdollCapsuleMeasure capsule = measures.capsules[1];
  HkxRagdollEdits radius;
  HkxRagdollShapeEdit edit;
  edit.shape = capsule.shape;
  edit.radius = capsule.radius + 1.0f;
  radius.shapes.push_back(edit);
  const HkxRagdollEditResult widened = apply_ragdoll_edits(bytes, radius);
  ASSERT_EQ(widened.status, HkxRagdollEditStatus::Ok) << widened.message;
  EXPECT_LE(differing_bytes(bytes, widened.bytes), 200u);
  const HkxRagdollMeasures after = measures_of(widened.bytes);
  EXPECT_EQ(after.capsules[1].a, capsule.a);
  EXPECT_EQ(after.capsules[1].b, capsule.b);
  EXPECT_EQ(after.capsules[1].radius, capsule.radius + 1.0f);

  HkxRagdollEdits same;
  HkxRagdollShapeEdit restated;
  restated.shape = capsule.shape;
  restated.a = capsule.a;
  restated.b = capsule.b;
  restated.radius = capsule.radius;
  same.shapes.push_back(restated);
  const HkxRagdollEditResult unchanged = apply_ragdoll_edits(bytes, same);
  ASSERT_EQ(unchanged.status, HkxRagdollEditStatus::Ok) << unchanged.message;
  EXPECT_EQ(unchanged.bytes, bytes);
}

// Constraint: detects an axis turn or an edit on unproven data that rewrites a packed frame or polytope instead of being refused without bytes.
TEST(HkxRagdollEditRealData, AxisChangesTamperedDataAndNonCapsulesAreRefusedWithoutBytes) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const HkxRagdollMeasures measures = measures_of(bytes);
  const HkxRagdollCapsuleMeasure capsule = measures.capsules[1];
  const auto refused = [&](const std::vector<std::byte>& source, HkxRagdollShapeEdit edit, HkxRagdollEditStatus status,
                           std::string_view text) {
    HkxRagdollEdits edits;
    edits.shapes.push_back(edit);
    const HkxRagdollEditResult result = apply_ragdoll_edits(source, edits);
    EXPECT_EQ(result.status, status) << result.message;
    EXPECT_TRUE(contains(result.message, text)) << result.message;
    EXPECT_TRUE(result.bytes.empty());
  };

  HkxRagdollShapeEdit turned;
  turned.shape = capsule.shape;
  turned.b = HkxVec3{capsule.b[0], capsule.b[1] + 5.0f, capsule.b[2]};
  refused(bytes, turned, HkxRagdollEditStatus::ShapeGeometryUnsupported, "axis");

  const double stored_length = length_between(capsule.a, capsule.b);
  std::array<double, 3> axis{};
  for (std::size_t i = 0; i < 3; ++i) axis[i] = (static_cast<double>(capsule.b[i]) - capsule.a[i]) / stored_length;
  std::array<double, 3> across{axis[1], -axis[0], 0.0};
  if (std::fabs(across[0]) + std::fabs(across[1]) < 0.1) across = {0.0, axis[2], -axis[1]};
  const double across_length = std::sqrt(across[0] * across[0] + across[1] * across[1] + across[2] * across[2]);

  HkxRagdollShapeEdit nudged;
  nudged.shape = capsule.shape;
  nudged.b = HkxVec3{};
  for (std::size_t i = 0; i < 3; ++i) {
    nudged.b->at(i) = static_cast<float>(capsule.b[i] + stored_length * 5.0e-5 * across[i] / across_length);
  }
  refused(bytes, nudged, HkxRagdollEditStatus::ShapeGeometryUnsupported, "axis");

  HkxRagdollShapeEdit spherical;
  spherical.shape = capsule.shape;
  spherical.a = HkxVec3{};
  spherical.b = HkxVec3{};
  for (std::size_t i = 0; i < 3; ++i) {
    const double centre = (static_cast<double>(capsule.a[i]) + capsule.b[i]) / 2.0;
    spherical.a->at(i) = static_cast<float>(centre - 0.2 * axis[i]);
    spherical.b->at(i) = static_cast<float>(centre + 0.2 * axis[i]);
  }
  spherical.radius = 20000.0f;
  refused(bytes, spherical, HkxRagdollEditStatus::ShapeGeometryUnsupported, "too close");

  HkxRagdollShapeEdit degenerate;
  degenerate.shape = capsule.shape;
  degenerate.b = capsule.a;
  refused(bytes, degenerate, HkxRagdollEditStatus::ShapeGeometryUnsupported, "length");

  HkxRagdollShapeEdit thin;
  thin.shape = capsule.shape;
  thin.radius = 0.0f;
  refused(bytes, thin, HkxRagdollEditStatus::ShapeGeometryUnsupported, "radius");

  HkxRagdollShapeEdit not_finite;
  not_finite.shape = capsule.shape;
  not_finite.radius = std::nanf("");
  refused(bytes, not_finite, HkxRagdollEditStatus::ShapeGeometryUnsupported, "finite");

  HkxRagdollShapeEdit missing;
  missing.shape = 100000;
  missing.radius = 1.0f;
  refused(bytes, missing, HkxRagdollEditStatus::BadTarget, "not a shape");

  HkxPackfile pack;
  ASSERT_EQ(read_packfile(bytes, pack), HkxStatus::Ok);
  std::size_t not_capsule = pack.objects.size();
  for (std::size_t i = 0; i < pack.objects.size(); ++i) {
    if (pack.objects[i].class_name != "hknpCapsuleShape") {
      not_capsule = i;
      break;
    }
  }
  ASSERT_LT(not_capsule, pack.objects.size());
  HkxRagdollShapeEdit other;
  other.shape = static_cast<std::int32_t>(not_capsule);
  other.radius = 1.0f;
  refused(bytes, other, HkxRagdollEditStatus::ShapeGeometryUnsupported, "not a capsule");

  const HkxClassLayout* shape_layout = find_layout("hknpShape");
  ASSERT_NE(shape_layout, nullptr);
  const auto radius_at = find_member_offset(*shape_layout, "convexRadius");
  ASSERT_TRUE(radius_at.has_value());
  std::vector<std::byte> tampered = bytes;
  const std::size_t at = pack.sections[pack.data_section].file_offset +
                         pack.objects[static_cast<std::size_t>(capsule.shape)].offset + *radius_at;
  const float wider = capsule.radius * 1.5f;
  std::memcpy(tampered.data() + at, &wider, sizeof wider);
  const HkxRagdollMeasures tampered_measures = measures_of(tampered);
  EXPECT_FALSE(tampered_measures.capsules[1].editable);
  HkxRagdollShapeEdit resize;
  resize.shape = capsule.shape;
  resize.radius = wider + 1.0f;
  refused(tampered, resize, HkxRagdollEditStatus::ShapeGeometryUnsupported, "capsule model");
}

// Constraint: detects a mass or centre-of-mass edit on a vanilla body that rewrites a neighbour's motion row, misses the inverse-inertia scaling, or changes bytes outside the motion row.
TEST(HkxRagdollEditRealData, MassAndCenterOfMassEditsOnVanillaBodiesTouchOnlyTheirMotionRow) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const HkxRagdollMeasures before = measures_of(bytes);
  std::size_t edited = 0;
  for (const HkxRagdollBodyMeasure& body : before.bodies) {
    if (body.motion < 0 || !(body.inverse_mass > 0.0f)) continue;
    SCOPED_TRACE("body " + std::to_string(body.body));
    HkxRagdollBodyEdit edit;
    edit.body = body.body;
    edit.mass = body.mass() * 2.0f;
    edit.center_of_mass = HkxVec3{body.center_of_mass[0] + 0.25f, body.center_of_mass[1], body.center_of_mass[2]};
    HkxRagdollEdits edits;
    edits.bodies.push_back(edit);
    const HkxRagdollEditResult result = apply_ragdoll_edits(bytes, edits);
    if (result.status == HkxRagdollEditStatus::SharedStorage) continue;
    ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
    ++edited;
    EXPECT_LE(differing_bytes(bytes, result.bytes), 64u);

    const HkxRagdollMeasures after = measures_of(result.bytes);
    ASSERT_EQ(after.bodies.size(), before.bodies.size());
    for (std::size_t i = 0; i < before.bodies.size(); ++i) {
      if (before.bodies[i].body != body.body) {
        EXPECT_EQ(after.bodies[i], before.bodies[i]);
        continue;
      }
      EXPECT_NEAR(after.bodies[i].mass(), *edit.mass, 1.0e-3 * *edit.mass);
      EXPECT_EQ(after.bodies[i].center_of_mass, *edit.center_of_mass);
      for (std::size_t lane = 0; lane < 3; ++lane) {
        EXPECT_NEAR(after.bodies[i].inverse_inertia[lane], body.inverse_inertia[lane] * 0.5f,
                    2.0e-2 * std::fabs(body.inverse_inertia[lane]) + 1.0e-6);
      }
    }
    EXPECT_EQ(measures_of(result.bytes).capsules, before.capsules);
  }
  EXPECT_GT(edited, 0u);
}

// Constraint: detects a vanilla ragdoll constraint edit that lands on the wrong limit, spills into a neighbouring constraint, or moves bytes outside the constraint data.
TEST(HkxRagdollEditRealData, TwistAndConeEditsOnVanillaConstraintsTouchOnlyThatConstraint) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const Loaded before = load(bytes);
  ASSERT_EQ(before.status, HkxStatus::Ok);
  std::size_t edited = 0;
  for (const HkxPhysicsConstraint& row : before.model.constraints) {
    if (row.kind != HkxConstraintKind::Ragdoll || !row.twist_min_angle || !row.twist_max_angle || !row.cone_max_angle) continue;
    SCOPED_TRACE("constraint " + std::to_string(row.id));
    HkxRagdollConstraintEdit edit;
    edit.constraint = row.id;
    edit.twist_max_angle = *row.twist_max_angle + 0.0625f;
    edit.cone_max_angle = *row.cone_max_angle * 0.5f;
    HkxRagdollEdits edits;
    edits.constraints.push_back(edit);
    const HkxRagdollEditResult result = apply_ragdoll_edits(bytes, edits);
    if (result.status == HkxRagdollEditStatus::SharedStorage || result.status == HkxRagdollEditStatus::BadValue) continue;
    ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
    ++edited;
    EXPECT_LE(differing_bytes(bytes, result.bytes), 16u);

    const Loaded after = load(result.bytes);
    ASSERT_EQ(after.status, HkxStatus::Ok);
    ASSERT_EQ(after.model.constraints.size(), before.model.constraints.size());
    for (std::size_t i = 0; i < before.model.constraints.size(); ++i) {
      const HkxPhysicsConstraint& old_row = before.model.constraints[i];
      const HkxPhysicsConstraint& new_row = after.model.constraints[i];
      EXPECT_EQ(new_row.twist_min_angle, old_row.twist_min_angle);
      EXPECT_EQ(new_row.frame_a, old_row.frame_a);
      EXPECT_EQ(new_row.frame_b, old_row.frame_b);
      if (old_row.id != row.id) {
        EXPECT_EQ(new_row.twist_max_angle, old_row.twist_max_angle);
        EXPECT_EQ(new_row.cone_max_angle, old_row.cone_max_angle);
        continue;
      }
      EXPECT_FLOAT_EQ(*new_row.twist_max_angle, *edit.twist_max_angle);
      EXPECT_FLOAT_EQ(*new_row.cone_max_angle, *edit.cone_max_angle);
    }
  }
  EXPECT_GT(edited, 0u);
}

// Constraint: detects a capsule resized while another capsule points at the same mass-properties container, which would change the neighbour's mass; a capsule with its own container must stay editable.
TEST(HkxRagdollEditRealData, ACapsuleSharingAPropertiesContainerIsRefusedAndItsNeighboursStayEditable) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const HkxRagdollMeasures measures = measures_of(bytes);
  ASSERT_GE(measures.capsules.size(), 3u);
  HkxPackfile pack;
  ASSERT_EQ(read_packfile(bytes, pack), HkxStatus::Ok);
  const HkxClassLayout* shape_layout = find_layout("hknpShape");
  ASSERT_NE(shape_layout, nullptr);
  const auto properties_at = find_member_offset(*shape_layout, "properties");
  ASSERT_TRUE(properties_at.has_value());
  HkxSection& section = pack.sections[pack.data_section];
  const auto destination_of = [&](std::int32_t shape) -> std::uint32_t* {
    const auto source = static_cast<std::uint32_t>(pack.objects[static_cast<std::size_t>(shape)].offset + *properties_at);
    for (HkxLocalFixup& fixup : section.local_fixups) {
      if (fixup.source == source) return &fixup.destination;
    }
    for (HkxRemoteFixup& fixup : section.global_fixups) {
      if (fixup.source == source) return &fixup.destination;
    }
    return nullptr;
  };
  std::uint32_t* first = destination_of(measures.capsules[1].shape);
  std::uint32_t* second = destination_of(measures.capsules[2].shape);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(*first, *second);
  *second = *first;
  std::vector<std::byte> shared;
  ASSERT_EQ(rewrite_packfile(bytes, pack, shared), HkxStatus::Ok);

  HkxRagdollEdits owner;
  owner.shapes.push_back(resize_edit(measures.capsules[1], 0.8, 1.25f));
  const HkxRagdollEditResult owner_result = apply_ragdoll_edits(shared, owner);
  EXPECT_EQ(owner_result.status, HkxRagdollEditStatus::SharedStorage) << owner_result.message;
  EXPECT_TRUE(owner_result.bytes.empty());

  // Constraint: capsule 2 now reads capsule 1's mass block, so the stored-data check runs first; shared or unsupported geometry both refuse, only Ok is wrong.
  HkxRagdollEdits borrower;
  borrower.shapes.push_back(resize_edit(measures.capsules[2], 0.8, 1.25f));
  const HkxRagdollEditResult borrower_result = apply_ragdoll_edits(shared, borrower);
  EXPECT_TRUE(borrower_result.status == HkxRagdollEditStatus::SharedStorage ||
              borrower_result.status == HkxRagdollEditStatus::ShapeGeometryUnsupported)
      << borrower_result.message;
  EXPECT_TRUE(borrower_result.bytes.empty());

  HkxRagdollEdits neighbour;
  neighbour.shapes.push_back(resize_edit(measures.capsules[0], 0.8, 1.25f));
  const HkxRagdollEditResult own = apply_ragdoll_edits(shared, neighbour);
  EXPECT_EQ(own.status, HkxRagdollEditStatus::Ok) << own.message;
}

// Constraint: detects a shape edit on a capsule used by two bodies, which would resize both.
TEST(HkxRagdollEdit, ASharedCapsuleRefusesGeometryEdits) {
  PhysicsSpec spec = editable_spec();
  spec.bodies[1].shape = spec.bodies[0].shape;
  const std::vector<std::byte> source = physics_packfile(spec);
  const HkxRagdollMeasures measures = measures_of(source);
  ASSERT_FALSE(measures.capsules.empty());
  HkxRagdollEdits edits;
  HkxRagdollShapeEdit edit;
  edit.shape = measures.capsules[0].shape;
  edit.radius = 0.5f;
  edits.shapes.push_back(edit);
  const HkxRagdollEditResult result = apply_ragdoll_edits(source, edits);
  EXPECT_EQ(result.status, HkxRagdollEditStatus::SharedStorage) << result.message;
  EXPECT_TRUE(result.bytes.empty());
}

// Constraint: detects friction read as an IEEE half (about 1.77) or writing it rounded to the wrong width, a material edit that touches a neighbour, or a shared material changed without consent.
TEST(HkxRagdollEditRealData, MaterialEditsWriteTruncatedFloatsToOneMaterialAndRefuseSharedOnes) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const HkxRagdollMeasures measures = measures_of(bytes);
  HkxRagdollEdits edits;
  HkxRagdollMaterialEdit edit;
  edit.material = 3;
  edit.dynamic_friction = 0.7f;
  edit.static_friction = 0.9f;
  edit.restitution = 0.05f;
  edits.materials.push_back(edit);
  const HkxRagdollEditResult result = apply_ragdoll_edits(bytes, edits);
  ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
  ASSERT_EQ(result.bytes.size(), bytes.size());
  EXPECT_LE(differing_bytes(bytes, result.bytes), 6u);
  EXPECT_TRUE(contains(result.message, "material 3 dynamic friction stored as 0.699219"));
  const HkxRagdollMeasures after = measures_of(result.bytes);
  ASSERT_EQ(after.materials.size(), 18u);
  for (std::size_t i = 0; i < after.materials.size(); ++i) {
    if (i == 3) continue;
    EXPECT_EQ(after.materials[i], measures.materials[i]);
  }
  EXPECT_EQ(after.materials[3].dynamic_friction, 0.69921875f);
  EXPECT_EQ(after.materials[3].static_friction, 0.8984375f);
  EXPECT_EQ(after.materials[3].restitution, 0.0498046875f);
  EXPECT_EQ(after.bodies, measures.bodies);
  EXPECT_EQ(after.capsules, measures.capsules);

  const auto refused = [&](HkxRagdollMaterialEdit bad, HkxRagdollEditStatus status) {
    HkxRagdollEdits one;
    one.materials.push_back(bad);
    const HkxRagdollEditResult outcome = apply_ragdoll_edits(bytes, one);
    EXPECT_EQ(outcome.status, status) << outcome.message;
    EXPECT_TRUE(outcome.bytes.empty());
  };
  HkxRagdollMaterialEdit negative;
  negative.material = 0;
  negative.restitution = -0.1f;
  refused(negative, HkxRagdollEditStatus::BadValue);
  HkxRagdollMaterialEdit huge;
  huge.material = 0;
  huge.dynamic_friction = 70000.0f;
  refused(huge, HkxRagdollEditStatus::BadValue);
  HkxRagdollMaterialEdit nan_value;
  nan_value.material = 0;
  nan_value.static_friction = std::nanf("");
  refused(nan_value, HkxRagdollEditStatus::BadValue);
  HkxRagdollMaterialEdit absent;
  absent.material = 18;
  absent.restitution = 0.1f;
  refused(absent, HkxRagdollEditStatus::BadTarget);
  HkxRagdollMaterialEdit empty;
  empty.material = 0;
  refused(empty, HkxRagdollEditStatus::BadValue);

  HkxRagdollEdits twice;
  absent.material = 0;
  twice.materials = {absent, absent};
  EXPECT_EQ(apply_ragdoll_edits(bytes, twice).status, HkxRagdollEditStatus::BadValue);

  HkxPackfile pack;
  ASSERT_EQ(read_packfile(bytes, pack), HkxStatus::Ok);
  const HkxClassLayout* material_layout = find_layout("hknpMaterial");
  ASSERT_NE(material_layout, nullptr);
  const auto dynamic_at = find_member_offset(*material_layout, "dynamicFriction");
  const auto restitution_at = find_member_offset(*material_layout, "restitution");
  const auto shared_at = find_member_offset(*material_layout, "isShared");
  ASSERT_TRUE(dynamic_at && restitution_at && shared_at);
  const std::size_t data_start = pack.sections[pack.data_section].file_offset;
  const std::size_t data_size = pack.sections[pack.data_section].data.size();
  const std::size_t row_size = material_layout->size;
  const auto stored_half = [&](std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[at]) | static_cast<unsigned>(bytes[at + 1]) << 8);
  };
  std::size_t first_row = 0;
  for (std::size_t at = data_start; at + 17 * row_size + material_layout->size <= data_start + data_size; ++at) {
    bool all = true;
    for (std::size_t row = 0; row < 18 && all; ++row) {
      all = stored_half(at + row * row_size + *dynamic_at) == 0x3f19 && stored_half(at + row * row_size + *restitution_at) == 0x3e4c;
    }
    if (all) {
      first_row = at;
      break;
    }
  }
  ASSERT_NE(first_row, 0u);
  std::vector<std::byte> shared = bytes;
  shared[first_row + *shared_at] = std::byte{1};
  const HkxRagdollMeasures shared_measures = measures_of(shared);
  ASSERT_EQ(shared_measures.materials.size(), 18u);
  EXPECT_TRUE(shared_measures.materials[0].shared);
  HkxRagdollMaterialEdit shared_edit;
  shared_edit.material = 0;
  shared_edit.restitution = 0.25f;
  HkxRagdollEdits blocked;
  blocked.materials.push_back(shared_edit);
  const HkxRagdollEditResult stopped = apply_ragdoll_edits(shared, blocked);
  EXPECT_EQ(stopped.status, HkxRagdollEditStatus::SharedStorage) << stopped.message;
  EXPECT_TRUE(stopped.bytes.empty());
  blocked.materials[0].allow_shared = true;
  const HkxRagdollEditResult allowed = apply_ragdoll_edits(shared, blocked);
  ASSERT_EQ(allowed.status, HkxRagdollEditStatus::Ok) << allowed.message;
  EXPECT_EQ(measures_of(allowed.bytes).materials[0].restitution, 0.25f);

  HkxRagdollMaterialEdit lone;
  lone.material = 4;
  lone.restitution = 0.25f;
  HkxRagdollEdits restitution_only;
  restitution_only.materials.push_back(lone);
  const HkxRagdollEditResult single = apply_ragdoll_edits(bytes, restitution_only);
  ASSERT_EQ(single.status, HkxRagdollEditStatus::Ok) << single.message;
  EXPECT_LE(differing_bytes(bytes, single.bytes), 2u);
  const HkxRagdollMeasures lone_after = measures_of(single.bytes);
  EXPECT_EQ(lone_after.materials[4].restitution, 0.25f);
  EXPECT_EQ(lone_after.materials[4].dynamic_friction, measures.materials[4].dynamic_friction);
  EXPECT_EQ(lone_after.materials[4].static_friction, measures.materials[4].static_friction);
}

namespace {

struct PreviewScene {
  HkxRagdollModel model;
  HkxRagdollMeasures measures;
};

std::vector<float> frame_at(float x, float y, float z) {
  return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, x, y, z, 1.0f};
}

void add_capsule_body(PreviewScene& scene, std::int32_t id, const HkxVec3& position, float mass, float inertia) {
  HkxPhysicsShape shape;
  shape.id = 100 + id;
  shape.kind = HkxPhysicsShapeKind::Capsule;
  shape.radius = 0.1f;
  scene.model.shapes.push_back(shape);
  HkxRigidBody body;
  body.id = id;
  body.shape_id = shape.id;
  body.position = position;
  scene.model.bodies.push_back(body);
  HkxRagdollBodyMeasure motion;
  motion.body = id;
  motion.motion = id;
  motion.inverse_mass = 1.0f / mass;
  motion.inverse_inertia = {inertia, inertia, inertia};
  scene.measures.bodies.push_back(motion);
  HkxRagdollCapsuleMeasure capsule;
  capsule.shape = shape.id;
  capsule.a = {-0.4f, 0.0f, 0.0f};
  capsule.b = {0.4f, 0.0f, 0.0f};
  capsule.radius = 0.1f;
  scene.measures.capsules.push_back(capsule);
}

void add_joint(PreviewScene& scene, HkxConstraintKind kind, const HkxVec3& pivot_a, const HkxVec3& pivot_b) {
  HkxPhysicsConstraint joint;
  joint.id = static_cast<std::int32_t>(scene.model.constraints.size());
  joint.kind = kind;
  joint.body_a = 0;
  joint.body_b = 1;
  joint.frame_a = frame_at(pivot_a[0], pivot_a[1], pivot_a[2]);
  joint.frame_b = frame_at(pivot_b[0], pivot_b[1], pivot_b[2]);
  joint.limit_axis = 2;
  if (kind == HkxConstraintKind::LimitedHinge) {
    joint.min_angle = -0.5f;
    joint.max_angle = 0.5f;
  }
  if (kind == HkxConstraintKind::Ragdoll) {
    joint.twist_min_angle = -0.3f;
    joint.twist_max_angle = 0.4f;
    joint.cone_max_angle = 0.5f;
  }
  scene.model.constraints.push_back(joint);
}

PreviewScene single_body(const HkxVec3& position) {
  PreviewScene scene;
  add_capsule_body(scene, 0, position, 2.0f, 12.0f);
  return scene;
}

PreviewScene jointed_pair(HkxConstraintKind kind) {
  PreviewScene scene;
  add_capsule_body(scene, 0, {0.0f, 0.0f, 0.0f}, 50000.0f, 0.001f);
  add_capsule_body(scene, 1, {1.0f, 0.0f, 0.0f}, 1.0f, 12.0f);
  add_joint(scene, kind, {0.5f, 0.0f, 0.0f}, {-0.5f, 0.0f, 0.0f});
  return scene;
}

HkxPreviewOptions in_space() {
  HkxPreviewOptions options;
  options.gravity = 0.0f;
  options.ground = false;
  return options;
}

std::vector<float> flatten(const HkxPhysicsPreview& preview) {
  std::vector<float> out;
  for (const HkxPreviewBodyState& state : preview.bodies()) {
    for (float value : state.position) out.push_back(value);
    for (float value : state.rotation) out.push_back(value);
    for (float value : state.linear_velocity) out.push_back(value);
    for (float value : state.angular_velocity) out.push_back(value);
  }
  return out;
}

HkxPreviewStatus open_scene(HkxPhysicsPreview& preview, const PreviewScene& scene, const HkxPreviewOptions& options) {
  return preview.open(scene.model, scene.measures, options);
}

}  // namespace

// Constraint: detects an integrator that is not semi-implicit Euler at 1/120 s, or a gravity that drifts from the analytic fall.
TEST(HkxPhysicsPreview, FreeFallMatchesSemiImplicitEulerExactly) {
  const PreviewScene scene = single_body({0.0f, 0.0f, 10.0f});
  HkxPreviewOptions options;
  HkxPhysicsPreview preview;
  ASSERT_EQ(open_scene(preview, scene, options), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.step(60), HkxPreviewStatus::Ok);
  const double g = static_cast<double>(options.gravity);
  const double expected = 10.0 - g * kPreviewStep * kPreviewStep * 60.0 * 61.0 / 2.0;
  const std::vector<HkxPreviewBodyState> bodies = preview.bodies();
  ASSERT_EQ(bodies.size(), 1u);
  EXPECT_NEAR(bodies[0].position[2], expected, 1.0e-4);
  EXPECT_NEAR(bodies[0].linear_velocity[2], -g * kPreviewStep * 60.0, 1.0e-4);
  EXPECT_FLOAT_EQ(bodies[0].position[0], 0.0f);
  EXPECT_EQ(preview.steps(), 60);
  EXPECT_FALSE(bodies[0].grounded);
}

// Constraint: detects a ground contact that lets a capsule sink through the plane or that never comes to rest on its radius.
TEST(HkxPhysicsPreview, CapsuleRestsOnTheGroundAtItsRadius) {
  const PreviewScene scene = single_body({0.0f, 0.0f, 1.0f});
  HkxPreviewOptions options;
  options.ground_z = 0.5f;
  HkxPhysicsPreview preview;
  ASSERT_EQ(open_scene(preview, scene, options), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.step(600), HkxPreviewStatus::Ok);
  const HkxPreviewBodyState body = preview.bodies()[0];
  EXPECT_NEAR(body.center_of_mass[2], 0.6f, 0.01f);
  EXPECT_NEAR(body.linear_velocity[2], 0.0f, 0.05f);
  EXPECT_TRUE(body.grounded);
}

// Constraint: detects nondeterminism between two identical runs and a recover that does not return to the start pose.
TEST(HkxPhysicsPreview, RepeatedRunsAreBitIdenticalAndRecoverRestoresTheStart) {
  PreviewScene scene = jointed_pair(HkxConstraintKind::LimitedHinge);
  HkxPreviewOptions options;
  options.ground_z = -0.3f;
  const auto run = [&](HkxPhysicsPreview& preview) {
    EXPECT_EQ(open_scene(preview, scene, options), HkxPreviewStatus::Ok);
    EXPECT_EQ(preview.apply_impulse(1, {0.0f, 3.0f, 0.0f}), HkxPreviewStatus::Ok);
    EXPECT_EQ(preview.step(240), HkxPreviewStatus::Ok);
  };
  HkxPhysicsPreview first;
  HkxPhysicsPreview second;
  run(first);
  run(second);
  const std::vector<float> start = [&] {
    HkxPhysicsPreview fresh;
    EXPECT_EQ(open_scene(fresh, scene, options), HkxPreviewStatus::Ok);
    return flatten(fresh);
  }();
  EXPECT_EQ(flatten(first), flatten(second));
  EXPECT_NE(flatten(first), start);
  first.reset();
  EXPECT_EQ(flatten(first), start);
  EXPECT_EQ(first.steps(), 0);
}

// Constraint: detects an impulse that is not momentum over mass, one that bypasses the clamp, or a non-finite one that is accepted.
TEST(HkxPhysicsPreview, ImpulsesSetVelocityAreClampedAndRefuseNonFiniteValues) {
  const PreviewScene scene = single_body({0.0f, 0.0f, 0.0f});
  HkxPhysicsPreview preview;
  ASSERT_EQ(open_scene(preview, scene, in_space()), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.apply_impulse(0, {2.0f, 0.0f, 0.0f}), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.step(120), HkxPreviewStatus::Ok);
  EXPECT_NEAR(preview.bodies()[0].position[0], 1.0f, 1.0e-4f);

  preview.reset();
  ASSERT_EQ(preview.apply_impulse(0, {1.0e9f, 0.0f, 0.0f}), HkxPreviewStatus::Ok);
  EXPECT_NEAR(preview.bodies()[0].linear_velocity[0], kPreviewMaxImpulse / 2.0f, 1.0f);

  preview.reset();
  EXPECT_EQ(preview.apply_impulse(0, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}), HkxPreviewStatus::BadOption);
  EXPECT_EQ(preview.apply_impulse(7, {1.0f, 0.0f, 0.0f}), HkxPreviewStatus::BadTarget);
  EXPECT_EQ(preview.bodies()[0].linear_velocity[0], 0.0f);
  EXPECT_EQ(preview.step(kPreviewMaxStepsPerCall + 1), HkxPreviewStatus::BadOption);
}

// Constraint: detects a limited hinge that is not held at its limit or a limit override that leaks into the model or the next preview.
TEST(HkxPhysicsPreview, HingeLimitHoldsAndAnOverrideChangesOnlyThePreview) {
  const PreviewScene scene = jointed_pair(HkxConstraintKind::LimitedHinge);
  HkxPhysicsPreview preview;
  ASSERT_EQ(open_scene(preview, scene, in_space()), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.apply_impulse(1, {0.0f, 5.0f, 0.0f}), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.step(120), HkxPreviewStatus::Ok);
  HkxPreviewConstraintState joint = preview.constraints()[0];
  ASSERT_TRUE(joint.hinge_angle.has_value());
  EXPECT_LE(*joint.hinge_angle, 0.5f + 0.05f);
  EXPECT_GE(*joint.hinge_angle, 0.3f);
  EXPECT_LE(joint.anchor_error, 0.03f);
  EXPECT_FALSE(joint.overridden);

  preview.reset();
  HkxRagdollConstraintEdit tighter;
  tighter.constraint = 0;
  tighter.max_angle = 0.2f;
  ASSERT_EQ(preview.override_limits(tighter), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.apply_impulse(1, {0.0f, 5.0f, 0.0f}), HkxPreviewStatus::Ok);
  ASSERT_EQ(preview.step(120), HkxPreviewStatus::Ok);
  joint = preview.constraints()[0];
  EXPECT_TRUE(joint.overridden);
  EXPECT_FLOAT_EQ(*joint.hinge_max, 0.2f);
  EXPECT_LE(*joint.hinge_angle, 0.2f + 0.05f);
  EXPECT_GE(*joint.hinge_angle, 0.1f);

  EXPECT_FLOAT_EQ(scene.model.constraints[0].max_angle, 0.5f);
  HkxPhysicsPreview fresh;
  ASSERT_EQ(open_scene(fresh, scene, in_space()), HkxPreviewStatus::Ok);
  EXPECT_FLOAT_EQ(*fresh.constraints()[0].hinge_max, 0.5f);

  preview.clear_overrides();
  EXPECT_FLOAT_EQ(*preview.constraints()[0].hinge_max, 0.5f);
  EXPECT_FALSE(preview.constraints()[0].overridden);
}

// Constraint: detects an override that is accepted with unordered limits, with a body or frame change, or on a kind that has no such limit.
TEST(HkxPhysicsPreview, LimitOverridesRefuseBadValuesWithoutChangingState) {
  const PreviewScene scene = jointed_pair(HkxConstraintKind::LimitedHinge);
  HkxPhysicsPreview preview;
  ASSERT_EQ(open_scene(preview, scene, in_space()), HkxPreviewStatus::Ok);
  HkxRagdollConstraintEdit edit;
  edit.constraint = 0;
  edit.min_angle = 0.4f;
  edit.max_angle = -0.4f;
  EXPECT_EQ(preview.override_limits(edit), HkxPreviewStatus::BadConstraint);
  edit.min_angle = -0.4f;
  edit.max_angle = 99.0f;
  EXPECT_EQ(preview.override_limits(edit), HkxPreviewStatus::BadConstraint);
  edit.max_angle.reset();
  edit.twist_max_angle = 0.1f;
  EXPECT_EQ(preview.override_limits(edit), HkxPreviewStatus::BadConstraint);
  edit.twist_max_angle.reset();
  edit.frame_a = frame_at(0.0f, 0.0f, 0.0f);
  EXPECT_EQ(preview.override_limits(edit), HkxPreviewStatus::BadOption);
  edit.frame_a.reset();
  edit.min_angle.reset();
  EXPECT_EQ(preview.override_limits(edit), HkxPreviewStatus::BadOption);
  edit.constraint = 9;
  edit.min_angle = -0.1f;
  EXPECT_EQ(preview.override_limits(edit), HkxPreviewStatus::BadTarget);
  const HkxPreviewConstraintState joint = preview.constraints()[0];
  EXPECT_FALSE(joint.overridden);
  EXPECT_FLOAT_EQ(*joint.hinge_min, -0.5f);
  EXPECT_FLOAT_EQ(*joint.hinge_max, 0.5f);
}

// Constraint: detects ragdoll twist or cone limits that are not enforced, so a limb could pass through its authored range.
TEST(HkxPhysicsPreview, RagdollTwistAndConeLimitsHold) {
  const PreviewScene scene = jointed_pair(HkxConstraintKind::Ragdoll);
  HkxPhysicsPreview twisted;
  ASSERT_EQ(open_scene(twisted, scene, in_space()), HkxPreviewStatus::Ok);
  ASSERT_EQ(twisted.apply_impulse(1, {0.0f, 0.0f, 0.0f}, HkxVec3{0.3f, 0.0f, 0.0f}), HkxPreviewStatus::Ok);
  ASSERT_EQ(twisted.step(120), HkxPreviewStatus::Ok);
  const HkxPreviewConstraintState twist = twisted.constraints()[0];
  ASSERT_TRUE(twist.twist_angle.has_value());
  EXPECT_LE(*twist.twist_angle, 0.4f + 0.05f);
  EXPECT_GE(*twist.twist_angle, 0.3f);

  HkxPhysicsPreview swung;
  ASSERT_EQ(open_scene(swung, scene, in_space()), HkxPreviewStatus::Ok);
  ASSERT_EQ(swung.apply_impulse(1, {0.0f, 0.0f, 0.0f}, HkxVec3{0.0f, 0.0f, 0.5f}), HkxPreviewStatus::Ok);
  ASSERT_EQ(swung.step(240), HkxPreviewStatus::Ok);
  const HkxPreviewConstraintState cone = swung.constraints()[0];
  ASSERT_TRUE(cone.cone_angle.has_value());
  EXPECT_LE(*cone.cone_angle, 0.5f + 0.1f);
  EXPECT_GE(*cone.cone_angle, 0.35f);
  EXPECT_LE(cone.anchor_error, 0.05f);
}

// Constraint: detects capsules that pass through each other, or a jointed pair that is pushed apart although its joint owns the contact.
TEST(HkxPhysicsPreview, BodiesCollideUnlessTheyAreJointed) {
  PreviewScene scene;
  add_capsule_body(scene, 0, {0.0f, 0.0f, 0.0f}, 1.0f, 12.0f);
  add_capsule_body(scene, 1, {0.0f, 0.15f, 0.0f}, 1.0f, 12.0f);
  HkxPhysicsPreview apart;
  ASSERT_EQ(open_scene(apart, scene, in_space()), HkxPreviewStatus::Ok);
  ASSERT_EQ(apart.step(20), HkxPreviewStatus::Ok);
  std::vector<HkxPreviewBodyState> bodies = apart.bodies();
  EXPECT_GE(bodies[1].position[1] - bodies[0].position[1], 0.2f - 1.0e-3f);
  EXPECT_LT(bodies[0].position[1], 0.0f);
  EXPECT_GT(bodies[1].position[1], 0.15f);

  HkxPreviewOptions no_contact = in_space();
  no_contact.body_collision = false;
  HkxPhysicsPreview ghost;
  ASSERT_EQ(open_scene(ghost, scene, no_contact), HkxPreviewStatus::Ok);
  ASSERT_EQ(ghost.step(20), HkxPreviewStatus::Ok);
  bodies = ghost.bodies();
  EXPECT_NEAR(bodies[1].position[1] - bodies[0].position[1], 0.15f, 1.0e-4f);

  add_joint(scene, HkxConstraintKind::BallAndSocket, {0.0f, 0.075f, 0.0f}, {0.0f, -0.075f, 0.0f});
  HkxPhysicsPreview joined;
  ASSERT_EQ(open_scene(joined, scene, in_space()), HkxPreviewStatus::Ok);
  ASSERT_EQ(joined.step(20), HkxPreviewStatus::Ok);
  bodies = joined.bodies();
  EXPECT_NEAR(bodies[1].position[1] - bodies[0].position[1], 0.15f, 1.0e-3f);
}

// Constraint: detects a playback pose that moves unbound bodies, ignores the animation, or leaves the preview half-posed after a refusal.
TEST(HkxPhysicsPreview, PoseMovesBoundBodiesAndRefusesAnUnmappablePose) {
  PreviewScene scene = jointed_pair(HkxConstraintKind::BallAndSocket);
  add_capsule_body(scene, 2, {5.0f, 0.0f, 0.0f}, 1.0f, 12.0f);
  HkxSkeleton skeleton;
  skeleton.bone_names = {"Root", "Tip"};
  skeleton.parent_indices = {-1, 0};
  HkxQsTransform root;
  root.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  root.scale = {1.0f, 1.0f, 1.0f};
  HkxQsTransform tip = root;
  tip.translation = {1.0f, 0.0f, 0.0f};
  skeleton.reference_pose = {root, tip};
  scene.model.skeleton = skeleton;
  scene.model.mappings = {HkxRagdollMapping{0, 0, {}, {0.0f, 0.0f, 0.0f, 1.0f}}, HkxRagdollMapping{1, 1, {}, {0.0f, 0.0f, 0.0f, 1.0f}}};
  scene.model.bone_bindings = {{0, 0}, {1, 1}};

  HkxPhysicsPreview preview;
  ASSERT_EQ(open_scene(preview, scene, in_space()), HkxPreviewStatus::Ok);
  const std::vector<HkxBoneFrame> raised = {HkxBoneFrame{{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f, 1.0f}},
                                            HkxBoneFrame{{1.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
  ASSERT_EQ(preview.set_pose(raised), HkxPreviewStatus::Ok);
  std::vector<HkxPreviewBodyState> bodies = preview.bodies();
  EXPECT_NEAR(bodies[0].position[2], 5.0f, 1.0e-5f);
  EXPECT_NEAR(bodies[1].position[2], 5.0f, 1.0e-5f);
  EXPECT_NEAR(bodies[1].position[0], 1.0f, 1.0e-5f);
  EXPECT_FLOAT_EQ(bodies[2].position[2], 0.0f);
  ASSERT_EQ(preview.step(10), HkxPreviewStatus::Ok);
  preview.reset();
  EXPECT_NEAR(preview.bodies()[0].position[2], 5.0f, 1.0e-5f);

  const std::vector<float> before = flatten(preview);
  const std::vector<HkxBoneFrame> one_bone = {raised[0]};
  std::string wrong_size;
  EXPECT_EQ(preview.set_pose(one_bone, &wrong_size), HkxPreviewStatus::PoseUnavailable);
  EXPECT_NE(wrong_size.find("the pose has 1 bones but the ragdoll animation skeleton has 2"), std::string::npos) << wrong_size;
  EXPECT_EQ(flatten(preview), before);
}

// Constraint: detects a scene the preview should refuse being simulated anyway, or a refusal that leaves a usable preview open.
TEST(HkxPhysicsPreview, UnprovenScenesAreRefusedAndLeaveThePreviewClosed) {
  const auto refuses = [](const PreviewScene& scene, HkxPreviewStatus expected) {
    HkxPhysicsPreview preview;
    std::string message;
    EXPECT_EQ(preview.open(scene.model, scene.measures, in_space(), &message), expected) << message;
    EXPECT_FALSE(preview.is_open());
    EXPECT_EQ(preview.step(1), HkxPreviewStatus::BadTarget);
    EXPECT_FALSE(message.empty());
  };

  PreviewScene box = single_body({0.0f, 0.0f, 0.0f});
  box.model.shapes[0].kind = HkxPhysicsShapeKind::Box;
  refuses(box, HkxPreviewStatus::UnsupportedShape);

  PreviewScene massless = single_body({0.0f, 0.0f, 0.0f});
  massless.measures.bodies[0].inverse_mass = 0.0f;
  refuses(massless, HkxPreviewStatus::BadMass);
  PreviewScene feather = single_body({0.0f, 0.0f, 0.0f});
  feather.measures.bodies[0].inverse_mass = 1.0e6f;
  refuses(feather, HkxPreviewStatus::BadMass);

  PreviewScene crowd;
  for (std::int32_t i = 0; i <= kPreviewMaxBodies; ++i) add_capsule_body(crowd, i, {static_cast<float>(i), 0.0f, 0.0f}, 1.0f, 12.0f);
  refuses(crowd, HkxPreviewStatus::TooManyBodies);
  refuses(PreviewScene{}, HkxPreviewStatus::NoBodies);

  PreviewScene unlimited = jointed_pair(HkxConstraintKind::Ragdoll);
  unlimited.model.constraints[0].cone_max_angle.reset();
  refuses(unlimited, HkxPreviewStatus::MissingLimits);

  PreviewScene stretched = jointed_pair(HkxConstraintKind::BallAndSocket);
  (*stretched.model.constraints[0].frame_a)[0] = 2.0f;
  refuses(stretched, HkxPreviewStatus::BadFrame);
  PreviewScene mirrored = jointed_pair(HkxConstraintKind::BallAndSocket);
  (*mirrored.model.constraints[0].frame_b)[10] = -1.0f;
  refuses(mirrored, HkxPreviewStatus::BadFrame);
  PreviewScene unframed = jointed_pair(HkxConstraintKind::BallAndSocket);
  unframed.model.constraints[0].frame_a.reset();
  refuses(unframed, HkxPreviewStatus::BadFrame);

  PreviewScene stray = jointed_pair(HkxConstraintKind::BallAndSocket);
  stray.model.constraints[0].body_b = 9;
  refuses(stray, HkxPreviewStatus::BadTarget);
}

// Constraint: detects a cone edit written onto an atom that stores another angle measurement mode, which silently turns the stored limit into a different one; a twist-only edit on the same constraint must stay allowed.
TEST(HkxRagdollEdit, AConeEditIsRefusedWhenTheAtomStoresAnotherMeasurementMode) {
  const std::vector<std::byte> defaults = physics_packfile(editable_spec());
  const Loaded plain = load(defaults);
  ASSERT_EQ(plain.status, HkxStatus::Ok);
  ASSERT_TRUE(plain.model.constraints[0].cone_angle_mode.has_value());
  ASSERT_TRUE(plain.model.constraints[0].plane_angle_mode.has_value());
  EXPECT_EQ(*plain.model.constraints[0].cone_angle_mode, 0);
  EXPECT_EQ(*plain.model.constraints[0].plane_angle_mode, 1);

  PhysicsSpec spec = editable_spec();
  spec.constraints[0].cone_mode = 3;
  const std::vector<std::byte> source = physics_packfile(spec);
  const Loaded foreign = load(source);
  ASSERT_EQ(foreign.status, HkxStatus::Ok);
  ASSERT_TRUE(foreign.model.constraints[0].cone_angle_mode.has_value());
  EXPECT_EQ(*foreign.model.constraints[0].cone_angle_mode, 3);

  HkxRagdollEdits cone;
  HkxRagdollConstraintEdit edit;
  edit.constraint = 0;
  edit.cone_max_angle = 0.4f;
  cone.constraints.push_back(edit);
  std::vector<std::byte> input = source;
  const HkxRagdollEditResult refused = apply_ragdoll_edits(input, cone);
  EXPECT_EQ(refused.status, HkxRagdollEditStatus::AngleModeUnsupported) << refused.message;
  EXPECT_TRUE(contains(refused.message, "cone angle measurement mode 3")) << refused.message;
  EXPECT_TRUE(refused.bytes.empty());
  EXPECT_EQ(input, source);
  EXPECT_STREQ(ragdoll_edit_status_name(refused.status), "angle_mode_unsupported");

  HkxRagdollEdits both = cone;
  both.constraints[0].twist_max_angle = 0.5f;
  const HkxRagdollEditResult whole = apply_ragdoll_edits(source, both);
  EXPECT_EQ(whole.status, HkxRagdollEditStatus::AngleModeUnsupported) << whole.message;
  EXPECT_TRUE(whole.bytes.empty());

  HkxRagdollEdits twist;
  edit.cone_max_angle.reset();
  edit.twist_max_angle = 0.5f;
  twist.constraints.push_back(edit);
  const HkxRagdollEditResult allowed = apply_ragdoll_edits(source, twist);
  ASSERT_EQ(allowed.status, HkxRagdollEditStatus::Ok) << allowed.message;
  EXPECT_LE(differing_bytes(source, allowed.bytes), 4u);
  const Loaded after = load(allowed.bytes);
  EXPECT_FLOAT_EQ(*after.model.constraints[0].twist_max_angle, 0.5f);
  EXPECT_EQ(after.model.constraints[0].cone_max_angle, foreign.model.constraints[0].cone_max_angle);
  EXPECT_EQ(*after.model.constraints[0].cone_angle_mode, 3);
}

// Constraint: detects a mode gate that refuses vanilla cone edits, or a model that never reads the modes; the claim that vanilla stores cone mode 0 and plane mode 1 comes from BGS and is UNVERIFIED until this runs (issue 1731).
TEST(HkxRagdollEditRealData, VanillaRagdollConstraintsStoreTheModesTheEditsAreDefinedForAndAConeEditStaysConfined) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const Loaded before = load(bytes);
  ASSERT_EQ(before.status, HkxStatus::Ok);
  std::int32_t first = -1;
  for (const HkxPhysicsConstraint& constraint : before.model.constraints) {
    if (constraint.kind != HkxConstraintKind::Ragdoll) continue;
    if (first < 0) first = constraint.id;
    ASSERT_TRUE(constraint.cone_angle_mode.has_value()) << constraint.id;
    ASSERT_TRUE(constraint.plane_angle_mode.has_value()) << constraint.id;
    EXPECT_EQ(*constraint.cone_angle_mode, 0) << constraint.id;
    EXPECT_EQ(*constraint.plane_angle_mode, 1) << constraint.id;
  }
  ASSERT_GE(first, 0);

  HkxRagdollEdits edits;
  HkxRagdollConstraintEdit edit;
  edit.constraint = first;
  edit.cone_max_angle = 0.4f;
  edits.constraints.push_back(edit);
  const HkxRagdollEditResult result = apply_ragdoll_edits(bytes, edits);
  ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
  EXPECT_LE(differing_bytes(bytes, result.bytes), 4u);
  const Loaded after = load(result.bytes);
  ASSERT_EQ(after.status, HkxStatus::Ok);
  EXPECT_FLOAT_EQ(*after.model.constraints[static_cast<std::size_t>(first)].cone_max_angle, 0.4f);
}

namespace {

PhysicsSpec plane_friction_spec() {
  PhysicsSpec spec = editable_spec();
  spec.constraints[0].plane_min = -0.2f;
  spec.constraints[0].plane_max = 0.2f;
  spec.constraints[0].friction = 1.0f;
  PhysConstraintSpec limited;
  limited.body_a = 1;
  limited.body_b = 0;
  limited.data_class = "hkpLimitedHingeConstraintData";
  limited.frame_a.assign(16, 1.0f);
  limited.frame_b.assign(16, 2.0f);
  limited.hinge_min = -1.0f;
  limited.hinge_max = 1.0f;
  limited.friction = 2.0f;
  spec.constraints.push_back(limited);
  PhysConstraintSpec hinge;
  hinge.body_a = 0;
  hinge.body_b = 1;
  hinge.data_class = "hkpHingeConstraintData";
  hinge.frame_a.assign(16, 3.0f);
  hinge.frame_b.assign(16, 4.0f);
  spec.constraints.push_back(hinge);
  return spec;
}

HkxRagdollEdits one_constraint_edit(std::int32_t constraint, const std::function<void(HkxRagdollConstraintEdit&)>& fill) {
  HkxRagdollEdits edits;
  HkxRagdollConstraintEdit edit;
  edit.constraint = constraint;
  fill(edit);
  edits.constraints.push_back(edit);
  return edits;
}

}  // namespace

// Constraint: detects a plane or friction edit that lands on the cone or twist atom, writes outside its own floats, or is dropped because only those fields were set (an edit with no write that is still reported as applied).
TEST(HkxRagdollEdit, PlaneLimitAndConstraintFrictionEditsReadBackAndTouchOnlyTheirFloats) {
  const std::vector<std::byte> source = physics_packfile(plane_friction_spec());
  const Loaded before = load(source);
  ASSERT_EQ(before.status, HkxStatus::Ok);
  ASSERT_EQ(before.model.constraints.size(), 3u);
  EXPECT_FLOAT_EQ(*before.model.constraints[0].plane_min_angle, -0.2f);
  EXPECT_FLOAT_EQ(*before.model.constraints[0].plane_max_angle, 0.2f);
  EXPECT_FLOAT_EQ(*before.model.constraints[0].friction, 1.0f);
  EXPECT_FLOAT_EQ(*before.model.constraints[1].friction, 2.0f);
  EXPECT_FALSE(before.model.constraints[1].plane_min_angle.has_value());
  EXPECT_FALSE(before.model.constraints[2].friction.has_value());

  const HkxRagdollEditResult planes = apply_ragdoll_edits(
      source, one_constraint_edit(0, [](auto& e) { e.plane_min_angle = -0.1f; e.plane_max_angle = 0.15f; }));
  ASSERT_EQ(planes.status, HkxRagdollEditStatus::Ok) << planes.message;
  EXPECT_GT(differing_bytes(source, planes.bytes), 0u);
  EXPECT_LE(differing_bytes(source, planes.bytes), 8u);
  const Loaded planed = load(planes.bytes);
  EXPECT_FLOAT_EQ(*planed.model.constraints[0].plane_min_angle, -0.1f);
  EXPECT_FLOAT_EQ(*planed.model.constraints[0].plane_max_angle, 0.15f);
  EXPECT_EQ(planed.model.constraints[0].cone_max_angle, before.model.constraints[0].cone_max_angle);
  EXPECT_EQ(planed.model.constraints[0].twist_min_angle, before.model.constraints[0].twist_min_angle);
  EXPECT_EQ(planed.model.constraints[0].friction, before.model.constraints[0].friction);

  const HkxRagdollEditResult one_sided = apply_ragdoll_edits(source, one_constraint_edit(0, [](auto& e) { e.plane_max_angle = 0.5f; }));
  ASSERT_EQ(one_sided.status, HkxRagdollEditStatus::Ok) << one_sided.message;
  EXPECT_LE(differing_bytes(source, one_sided.bytes), 4u);
  const Loaded kept = load(one_sided.bytes);
  EXPECT_FLOAT_EQ(*kept.model.constraints[0].plane_min_angle, -0.2f);
  EXPECT_FLOAT_EQ(*kept.model.constraints[0].plane_max_angle, 0.5f);

  const HkxRagdollEditResult friction = apply_ragdoll_edits(source, one_constraint_edit(0, [](auto& e) { e.friction = 3.5f; }));
  ASSERT_EQ(friction.status, HkxRagdollEditStatus::Ok) << friction.message;
  EXPECT_GT(differing_bytes(source, friction.bytes), 0u);
  EXPECT_LE(differing_bytes(source, friction.bytes), 4u);
  const Loaded rubbed = load(friction.bytes);
  EXPECT_FLOAT_EQ(*rubbed.model.constraints[0].friction, 3.5f);
  EXPECT_EQ(rubbed.model.constraints[0].plane_max_angle, before.model.constraints[0].plane_max_angle);
  EXPECT_EQ(rubbed.model.constraints[1].friction, before.model.constraints[1].friction);

  const HkxRagdollEditResult hinge = apply_ragdoll_edits(source, one_constraint_edit(1, [](auto& e) { e.friction = 0.0f; }));
  ASSERT_EQ(hinge.status, HkxRagdollEditStatus::Ok) << hinge.message;
  EXPECT_LE(differing_bytes(source, hinge.bytes), 4u);
  const Loaded released = load(hinge.bytes);
  EXPECT_FLOAT_EQ(*released.model.constraints[1].friction, 0.0f);
  EXPECT_FLOAT_EQ(released.model.constraints[1].min_angle, before.model.constraints[1].min_angle);
  EXPECT_FLOAT_EQ(*released.model.constraints[0].friction, 1.0f);
}

// Constraint: detects a plane or friction edit that is accepted on a constraint without that atom, out of range, not finite, or in another angle measurement mode, or a refusal that returns bytes.
TEST(HkxRagdollEdit, PlaneAndFrictionEditsRefuseWithANamedReasonAndWriteNothing) {
  const std::vector<std::byte> source = physics_packfile(plane_friction_spec());
  const auto refused = [&](std::int32_t constraint, HkxRagdollEditStatus status, const std::function<void(HkxRagdollConstraintEdit&)>& fill,
                           std::string_view reason) {
    std::vector<std::byte> input = source;
    const HkxRagdollEditResult result = apply_ragdoll_edits(input, one_constraint_edit(constraint, fill));
    EXPECT_EQ(result.status, status) << result.message;
    EXPECT_TRUE(contains(result.message, reason)) << result.message;
    EXPECT_TRUE(result.bytes.empty());
    EXPECT_EQ(input, source);
  };
  refused(0, HkxRagdollEditStatus::BadValue, [](auto& e) { e.plane_min_angle = 0.5f; e.plane_max_angle = 0.1f; }, "minimum must not exceed");
  refused(0, HkxRagdollEditStatus::BadValue, [](auto& e) { e.plane_max_angle = 4.0f; }, "-pi and pi");
  refused(0, HkxRagdollEditStatus::BadValue, [](auto& e) { e.plane_min_angle = std::nanf(""); e.plane_max_angle = 0.1f; }, "-pi and pi");
  refused(1, HkxRagdollEditStatus::FieldAbsent, [](auto& e) { e.plane_max_angle = 0.1f; }, "no plane limit");
  refused(0, HkxRagdollEditStatus::BadValue, [](auto& e) { e.friction = -0.5f; }, "not negative");
  refused(0, HkxRagdollEditStatus::BadValue, [](auto& e) { e.friction = std::nanf(""); }, "not negative");
  refused(0, HkxRagdollEditStatus::BadValue, [](auto& e) { e.friction = std::numeric_limits<float>::infinity(); }, "not negative");
  refused(2, HkxRagdollEditStatus::FieldAbsent, [](auto& e) { e.friction = 1.0f; }, "no angular friction");

  PhysicsSpec foreign = plane_friction_spec();
  foreign.constraints[0].plane_mode = 0;
  foreign.constraints[0].cone_mode = 3;
  const std::vector<std::byte> other = physics_packfile(foreign);
  const HkxRagdollEditResult far_plane = apply_ragdoll_edits(other, one_constraint_edit(0, [](auto& e) { e.plane_max_angle = 4.0f; }));
  EXPECT_EQ(far_plane.status, HkxRagdollEditStatus::BadValue) << far_plane.message;
  EXPECT_TRUE(contains(far_plane.message, "-pi and pi")) << far_plane.message;
  const HkxRagdollEditResult far_cone = apply_ragdoll_edits(other, one_constraint_edit(0, [](auto& e) { e.cone_max_angle = 4.0f; }));
  EXPECT_EQ(far_cone.status, HkxRagdollEditStatus::BadValue) << far_cone.message;
  EXPECT_TRUE(contains(far_cone.message, "within 0 and pi")) << far_cone.message;
  const HkxRagdollEditResult plane = apply_ragdoll_edits(other, one_constraint_edit(0, [](auto& e) { e.plane_max_angle = 0.1f; }));
  EXPECT_EQ(plane.status, HkxRagdollEditStatus::AngleModeUnsupported) << plane.message;
  EXPECT_TRUE(contains(plane.message, "plane angle measurement mode 0")) << plane.message;
  EXPECT_TRUE(plane.bytes.empty());

  const HkxRagdollEditResult friction = apply_ragdoll_edits(other, one_constraint_edit(0, [](auto& e) { e.friction = 2.5f; }));
  ASSERT_EQ(friction.status, HkxRagdollEditStatus::Ok) << friction.message;
  EXPECT_FLOAT_EQ(*load(friction.bytes).model.constraints[0].friction, 2.5f);
}

// Constraint: detects plane and friction edits that miss on the vanilla file, where the stored atoms are not the synthetic ones; vanilla plane values and the claim that its plane atoms store mode 1 are UNVERIFIED until this runs (issue 1731).
TEST(HkxRagdollEditRealData, PlaneLimitAndFrictionEditsOnVanillaConstraintsTouchOnlyTheirFloats) {
  std::vector<std::byte> bytes;
  load_fixture(kSkeletonFixture, bytes);
  if (IsSkipped() || HasFatalFailure()) return;

  const Loaded before = load(bytes);
  ASSERT_EQ(before.status, HkxStatus::Ok);
  std::int32_t ragdoll = -1;
  std::int32_t hinge = -1;
  for (const HkxPhysicsConstraint& constraint : before.model.constraints) {
    if (constraint.kind == HkxConstraintKind::Ragdoll && ragdoll < 0) ragdoll = constraint.id;
    if (constraint.kind == HkxConstraintKind::LimitedHinge && hinge < 0) hinge = constraint.id;
  }
  ASSERT_GE(ragdoll, 0);
  const auto at = [](const Loaded& loaded, std::int32_t id) -> const HkxPhysicsConstraint& {
    return loaded.model.constraints[static_cast<std::size_t>(id)];
  };
  ASSERT_TRUE(at(before, ragdoll).friction.has_value());
  ASSERT_TRUE(at(before, ragdoll).plane_min_angle.has_value());

  HkxRagdollEdits edits;
  HkxRagdollConstraintEdit edit;
  edit.constraint = ragdoll;
  edit.plane_min_angle = -0.1f;
  edit.plane_max_angle = 0.1f;
  edit.friction = *at(before, ragdoll).friction + 1.5f;
  edits.constraints.push_back(edit);
  const HkxRagdollEditResult result = apply_ragdoll_edits(bytes, edits);
  ASSERT_EQ(result.status, HkxRagdollEditStatus::Ok) << result.message;
  EXPECT_GT(differing_bytes(bytes, result.bytes), 0u);
  EXPECT_LE(differing_bytes(bytes, result.bytes), 12u);
  const Loaded after = load(result.bytes);
  ASSERT_EQ(after.status, HkxStatus::Ok);
  EXPECT_FLOAT_EQ(*at(after, ragdoll).plane_min_angle, -0.1f);
  EXPECT_FLOAT_EQ(*at(after, ragdoll).plane_max_angle, 0.1f);
  EXPECT_FLOAT_EQ(*at(after, ragdoll).friction, *at(before, ragdoll).friction + 1.5f);
  EXPECT_EQ(at(after, ragdoll).cone_max_angle, at(before, ragdoll).cone_max_angle);
  EXPECT_EQ(at(after, ragdoll).twist_min_angle, at(before, ragdoll).twist_min_angle);
  for (std::size_t i = 0; i < before.model.constraints.size(); ++i) {
    if (static_cast<std::int32_t>(i) == ragdoll) continue;
    EXPECT_EQ(after.model.constraints[i].friction, before.model.constraints[i].friction) << i;
    EXPECT_EQ(after.model.constraints[i].plane_max_angle, before.model.constraints[i].plane_max_angle) << i;
  }

  ASSERT_GE(hinge, 0) << "the vanilla skeleton must hold a limited hinge constraint";
  ASSERT_TRUE(at(before, hinge).friction.has_value());
  HkxRagdollEdits hinge_edits;
  HkxRagdollConstraintEdit hinge_edit;
  hinge_edit.constraint = hinge;
  hinge_edit.friction = *at(before, hinge).friction + 0.5f;
  hinge_edits.constraints.push_back(hinge_edit);
  const HkxRagdollEditResult spun = apply_ragdoll_edits(bytes, hinge_edits);
  ASSERT_EQ(spun.status, HkxRagdollEditStatus::Ok) << spun.message;
  EXPECT_LE(differing_bytes(bytes, spun.bytes), 4u);
  EXPECT_FLOAT_EQ(*at(load(spun.bytes), hinge).friction, *at(before, hinge).friction + 0.5f);
}
