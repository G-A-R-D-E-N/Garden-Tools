#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/physics.hpp"
#include "hkx_mapper_fixture.hpp"
#include "hkx_physics_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  EXPECT_TRUE(contains(loaded.report.headline(), "2 of 3 bodies use shapes BGS cannot preview yet"));
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
  EXPECT_TRUE(any_finding(report, HkxPhysicsLoadSeverity::Info, "3 of 18 bodies use shapes BGS cannot preview yet",
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
  EXPECT_TRUE(contains(report.headline(), "3 of 18 bodies use shapes BGS cannot preview yet"));
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
