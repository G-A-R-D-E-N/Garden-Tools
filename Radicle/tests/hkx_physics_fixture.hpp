#pragma once

#include "fo4/hkx/layout.hpp"
#include "hkx_mapper_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::test::hkx {

struct PhysShapeSpec {
  std::string class_name{"hknpCapsuleShape"};
  std::vector<std::array<float, 4>> vertices;
  std::vector<std::uint8_t> indices;
  std::vector<std::array<std::uint16_t, 2>> faces;
  std::array<float, 3> a{};
  std::array<float, 3> b{};
  float radius{0.0f};
};

struct PhysBodySpec {
  std::string name;
  int shape{-1};
  std::array<float, 3> position{};
  std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
  std::uint32_t motion{0};
};

struct PhysConstraintSpec {
  std::uint32_t body_a{0};
  std::uint32_t body_b{0};
  std::string data_class;
  std::vector<float> frame_a;
  std::vector<float> frame_b;
  std::uint8_t twist_axis{0};
  std::uint8_t twist_ref_axis{1};
  float twist_min{0.0f};
  float twist_max{0.0f};
  std::uint8_t cone_twist_axis{0};
  std::uint8_t cone_ref_axis{0};
  float cone_max{0.0f};
  std::uint8_t limit_axis{0};
  float hinge_min{0.0f};
  float hinge_max{0.0f};
  std::uint8_t cone_mode{0};
  std::uint8_t plane_mode{1};
  float plane_min{0.0f};
  float plane_max{0.0f};
  float friction{0.0f};
};

struct PhysicsSpec {
  std::vector<PhysShapeSpec> shapes;
  std::vector<PhysBodySpec> bodies;
  std::vector<std::array<float, 3>> motions;
  std::vector<float> inverse_masses;
  std::vector<std::array<float, 3>> inverse_inertias;
  std::vector<PhysConstraintSpec> constraints;
  std::vector<std::int32_t> bone_map;
  bool with_skeleton{true};
  bool ragdoll_points_at_mapper{false};
  std::vector<MapperBoneSpec> animation_rig;
  std::vector<MapperBoneSpec> physics_rig;
  std::vector<MapperRowSpec> rows;
  std::int32_t mapping_type{0};
};

inline std::vector<PhysShapeSpec> capsule_shapes(std::size_t count) {
  PhysShapeSpec shape;
  shape.vertices = {{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0},
                    {0, 0, 1, 0}, {1, 0, 1, 0}, {1, 1, 1, 0}, {0, 1, 1, 0}};
  shape.indices = {0, 1, 2, 3, 4, 5, 6, 7};
  shape.faces = {{0, 4}, {4, 4}};
  return std::vector<PhysShapeSpec>(count, shape);
}

inline PhysicsSpec default_physics_spec() {
  PhysicsSpec spec;
  spec.shapes = capsule_shapes(2);
  spec.bodies = {{"  Root Body ", 0, {1, 2, 3}, {0, 0, 0, 1}, 1}, {"Child Body", 1, {4, 5, 6}, {0, 0, 0, 1}, 0}};
  spec.motions = {{10, 11, 12}, {20, 21, 22}};
  PhysConstraintSpec ragdoll;
  ragdoll.body_a = 1;
  ragdoll.body_b = 0;
  ragdoll.data_class = "hkpRagdollConstraintData";
  ragdoll.frame_a.assign(16, 0.5f);
  ragdoll.frame_b.assign(16, 0.25f);
  ragdoll.twist_axis = 2;
  ragdoll.twist_ref_axis = 0;
  ragdoll.twist_min = -0.5f;
  ragdoll.twist_max = 0.75f;
  ragdoll.cone_twist_axis = 1;
  ragdoll.cone_ref_axis = 2;
  ragdoll.cone_max = 0.625f;
  spec.constraints = {ragdoll};
  spec.bone_map = {0, 1};
  spec.animation_rig = mapper_rig_a();
  spec.physics_rig = {{"RootBone", -1, {0, 0, 0}}, {"ChildBone", 0, {0, 2, 0}}};
  spec.rows = {{0, 0, mapper_lanes(0, 0, 0, 0, 0, 0, 1)}, {1, 1, mapper_lanes(0, 1, 0, 0, 0, 0, 1)}};
  return spec;
}

inline void put16(std::vector<std::byte>& data, std::size_t at, std::uint16_t value) {
  data[at] = static_cast<std::byte>(value & 0xFF);
  data[at + 1] = static_cast<std::byte>(value >> 8);
}

inline std::optional<std::size_t> member_opt(const char* layout, const char* member) {
  const auto* found = fo4::hkx::find_layout(layout);
  if (found == nullptr) return std::nullopt;
  const auto offset = fo4::hkx::find_member_offset(*found, member);
  if (!offset) return std::nullopt;
  return static_cast<std::size_t>(*offset);
}

// Requires every class used here to have a generated layout, so each object is sized from the same table the reader uses.
inline std::vector<std::byte> physics_packfile(const PhysicsSpec& spec) {
  using fo4::hkx::find_layout;
  std::vector<std::byte> names;
  std::vector<std::byte> data;
  std::vector<std::uint32_t> virtuals;
  std::vector<std::uint32_t> local;
  std::vector<std::uint32_t> global;

  const auto place = [&](const std::string& class_name, std::size_t extra = 0) {
    const auto* layout = find_layout(class_name);
    const auto name_at = add_class_name(names, layout->signature, class_name);
    const std::size_t at = data.size();
    data.resize(at + layout->size + extra, std::byte{0});
    virtuals.insert(virtuals.end(), {static_cast<std::uint32_t>(at), 0u, name_at});
    return at;
  };
  const auto text = [&](const std::string& value) {
    const std::size_t at = data.size();
    for (char ch : value) data.push_back(static_cast<std::byte>(ch));
    data.push_back(std::byte{0});
    return at;
  };
  const auto array = [&](std::size_t field, std::size_t at, std::size_t count) {
    link(local, field, at);
    set_count(data, field, count);
    patch_u32(data, field + 12, 0x80000000u | static_cast<std::uint32_t>(count));
  };
  const auto point = [&](std::size_t field, std::size_t target) {
    global.insert(global.end(), {static_cast<std::uint32_t>(field), 2u, static_cast<std::uint32_t>(target)});
  };

  const std::size_t ragdoll = place("hknpRagdollData");

  std::vector<std::size_t> shape_at;
  for (const PhysShapeSpec& shape : spec.shapes) {
    const std::size_t payload = shape.vertices.size() * 16 + ((shape.indices.size() + 3) & ~std::size_t{3}) +
                                shape.faces.size() * 4;
    const std::size_t at = place(shape.class_name, (payload + 15) & ~std::size_t{15});
    shape_at.push_back(at);
    const std::size_t base = at + find_layout(shape.class_name)->size;
    const std::size_t vertices_field = at + member_at("hknpConvexShape", "vertices");
    const bool polytope = shape.class_name != "hknpConvexShape";
    const std::size_t indices_payload = base + shape.vertices.size() * 16;
    const std::size_t faces_payload = indices_payload + ((shape.indices.size() + 3) & ~std::size_t{3});
    if (shape.class_name == "hknpCapsuleShape") {
      for (std::size_t lane = 0; lane < 3; ++lane) {
        patch_float(data, at + member_at("hknpCapsuleShape", "a") + lane * 4, shape.a[lane]);
        patch_float(data, at + member_at("hknpCapsuleShape", "b") + lane * 4, shape.b[lane]);
      }
      patch_float(data, at + member_at("hknpShape", "convexRadius"), shape.radius);
    }
    put16(data, vertices_field, static_cast<std::uint16_t>(shape.vertices.size()));
    put16(data, vertices_field + 2, static_cast<std::uint16_t>(base - vertices_field));
    for (std::size_t i = 0; i < shape.vertices.size(); ++i) {
      for (std::size_t lane = 0; lane < 4; ++lane) patch_float(data, base + i * 16 + lane * 4, shape.vertices[i][lane]);
    }
    if (!polytope) continue;
    const std::size_t indices_field = at + member_at("hknpConvexPolytopeShape", "indices");
    const std::size_t faces_field = at + member_at("hknpConvexPolytopeShape", "faces");
    put16(data, indices_field, static_cast<std::uint16_t>(shape.indices.size()));
    put16(data, indices_field + 2, static_cast<std::uint16_t>(indices_payload - indices_field));
    put16(data, faces_field, static_cast<std::uint16_t>(shape.faces.size()));
    put16(data, faces_field + 2, static_cast<std::uint16_t>(faces_payload - faces_field));
    for (std::size_t i = 0; i < shape.indices.size(); ++i) data[indices_payload + i] = static_cast<std::byte>(shape.indices[i]);
    for (std::size_t i = 0; i < shape.faces.size(); ++i) {
      put16(data, faces_payload + i * 4, shape.faces[i][0]);
      data[faces_payload + i * 4 + 2] = static_cast<std::byte>(shape.faces[i][1]);
    }
  }

  std::vector<std::size_t> constraint_at;
  for (const PhysConstraintSpec& constraint : spec.constraints) {
    if (constraint.data_class.empty()) {
      constraint_at.push_back(0);
      continue;
    }
    const std::size_t at = place(constraint.data_class);
    constraint_at.push_back(at);
    const auto atoms_member = member_opt(constraint.data_class.c_str(), "atoms");
    if (!atoms_member) continue;
    const std::size_t atoms = at + *atoms_member;
    const std::string atoms_type = constraint.data_class + "Atoms";
    if (const auto transforms = member_opt(atoms_type.c_str(), "transforms")) {
      const std::size_t base = atoms + *transforms;
      for (std::size_t i = 0; i < constraint.frame_a.size(); ++i) {
        patch_float(data, base + member_at("hkpSetLocalTransformsConstraintAtom", "transformA") + i * 4, constraint.frame_a[i]);
      }
      for (std::size_t i = 0; i < constraint.frame_b.size(); ++i) {
        patch_float(data, base + member_at("hkpSetLocalTransformsConstraintAtom", "transformB") + i * 4, constraint.frame_b[i]);
      }
    }
    if (constraint.data_class == "hkpRagdollConstraintData") {
      const std::size_t twist = atoms + member_at(atoms_type.c_str(), "twistLimit");
      data[twist + member_at("hkpTwistLimitConstraintAtom", "twistAxis")] = static_cast<std::byte>(constraint.twist_axis);
      data[twist + member_at("hkpTwistLimitConstraintAtom", "refAxis")] = static_cast<std::byte>(constraint.twist_ref_axis);
      patch_float(data, twist + member_at("hkpTwistLimitConstraintAtom", "minAngle"), constraint.twist_min);
      patch_float(data, twist + member_at("hkpTwistLimitConstraintAtom", "maxAngle"), constraint.twist_max);
      const std::size_t cone = atoms + member_at(atoms_type.c_str(), "coneLimit");
      data[cone + member_at("hkpConeLimitConstraintAtom", "twistAxisInA")] = static_cast<std::byte>(constraint.cone_twist_axis);
      data[cone + member_at("hkpConeLimitConstraintAtom", "refAxisInB")] = static_cast<std::byte>(constraint.cone_ref_axis);
      patch_float(data, cone + member_at("hkpConeLimitConstraintAtom", "maxAngle"), constraint.cone_max);
      data[cone + member_at("hkpConeLimitConstraintAtom", "angleMeasurementMode")] = static_cast<std::byte>(constraint.cone_mode);
      const std::size_t planes = atoms + member_at(atoms_type.c_str(), "planesLimit");
      data[planes + member_at("hkpConeLimitConstraintAtom", "angleMeasurementMode")] = static_cast<std::byte>(constraint.plane_mode);
      patch_float(data, planes + member_at("hkpConeLimitConstraintAtom", "minAngle"), constraint.plane_min);
      patch_float(data, planes + member_at("hkpConeLimitConstraintAtom", "maxAngle"), constraint.plane_max);
      patch_float(data, atoms + member_at(atoms_type.c_str(), "angFriction") + member_at("hkpAngFrictionConstraintAtom", "maxFrictionTorque"),
                  constraint.friction);
    } else if (constraint.data_class == "hkpLimitedHingeConstraintData") {
      const std::size_t limit = atoms + member_at(atoms_type.c_str(), "angLimit");
      data[limit + member_at("hkpAngLimitConstraintAtom", "limitAxis")] = static_cast<std::byte>(constraint.limit_axis);
      patch_float(data, limit + member_at("hkpAngLimitConstraintAtom", "minAngle"), constraint.hinge_min);
      patch_float(data, limit + member_at("hkpAngLimitConstraintAtom", "maxAngle"), constraint.hinge_max);
      patch_float(data, atoms + member_at(atoms_type.c_str(), "angFriction") + member_at("hkpAngFrictionConstraintAtom", "maxFrictionTorque"),
                  constraint.friction);
    }
  }

  std::size_t skeleton_a = 0;
  std::size_t skeleton_b = 0;
  std::size_t mapper = 0;
  if (spec.with_skeleton) {
    skeleton_a = place("hkaSkeleton");
    skeleton_b = place("hkaSkeleton");
    mapper = place("hkaSkeletonMapper");
  }

  const auto rig = [&](std::size_t skeleton, const std::vector<MapperBoneSpec>& bones, const char* name) {
    const std::size_t bone_size = find_layout("hkaBone")->size;
    const std::size_t bones_at = data.size();
    data.resize(bones_at + bones.size() * bone_size, std::byte{0});
    for (std::size_t i = 0; i < bones.size(); ++i) {
      link(local, bones_at + i * bone_size + member_at("hkaBone", "name"), text(bones[i].name));
    }
    const std::size_t parents_at = data.size();
    for (const MapperBoneSpec& bone : bones) put_i16(data, bone.parent);
    const std::size_t pose_at = data.size();
    for (const MapperBoneSpec& bone : bones) {
      put_pose(data, bone.position[0], bone.position[1], bone.position[2], 1.0f, bone.rotation);
    }
    link(local, skeleton + member_at("hkaSkeleton", "name"), text(name));
    array(skeleton + member_at("hkaSkeleton", "parentIndices"), parents_at, bones.size());
    array(skeleton + member_at("hkaSkeleton", "bones"), bones_at, bones.size());
    array(skeleton + member_at("hkaSkeleton", "referencePose"), pose_at, bones.size());
  };
  if (spec.with_skeleton) {
    rig(skeleton_a, spec.animation_rig, "Animation");
    rig(skeleton_b, spec.physics_rig, "Physics");

    const std::size_t data_at = mapper + member_at("hkaSkeletonMapper", "mapping");
    const auto field = [data_at](const char* member) { return data_at + member_at("hkaSkeletonMapperData", member); };
    global.insert(global.end(),
                  {static_cast<std::uint32_t>(field("skeletonA")), 2u, static_cast<std::uint32_t>(skeleton_a)});
    global.insert(global.end(),
                  {static_cast<std::uint32_t>(field("skeletonB")), 2u, static_cast<std::uint32_t>(skeleton_b)});
    patch_u32(data, field("mappingType"), static_cast<std::uint32_t>(spec.mapping_type));
    const auto extracted = mapper_lanes(0.25f, 0, 0, 0, 0, 0, 1);
    for (std::size_t lane = 0; lane < 12; ++lane) patch_float(data, field("extractedMotionMapping") + lane * 4, extracted[lane]);

    const std::size_t row_size = find_layout("hkaSkeletonMapperDataSimpleMapping")->size;
    const std::size_t rows_at = data.size();
    data.resize(rows_at + spec.rows.size() * row_size, std::byte{0});
    for (std::size_t i = 0; i < spec.rows.size(); ++i) {
      const std::size_t at = rows_at + i * row_size;
      patch_i16(data, at + member_at("hkaSkeletonMapperDataSimpleMapping", "boneA"), spec.rows[i].bone_a);
      patch_i16(data, at + member_at("hkaSkeletonMapperDataSimpleMapping", "boneB"), spec.rows[i].bone_b);
      for (std::size_t lane = 0; lane < 12; ++lane) {
        patch_float(data, at + member_at("hkaSkeletonMapperDataSimpleMapping", "aFromBTransform") + lane * 4,
                    spec.rows[i].lanes[lane]);
      }
    }
    if (!spec.rows.empty()) array(field("simpleMappings"), rows_at, spec.rows.size());
  }

  const std::size_t body_size = find_layout("hknpBodyCinfo")->size;
  const std::size_t bodies_at = data.size();
  data.resize(bodies_at + spec.bodies.size() * body_size, std::byte{0});
  for (std::size_t i = 0; i < spec.bodies.size(); ++i) {
    const PhysBodySpec& body = spec.bodies[i];
    const std::size_t at = bodies_at + i * body_size;
    if (body.shape >= 0) point(at + member_at("hknpBodyCinfo", "shape"), shape_at[static_cast<std::size_t>(body.shape)]);
    patch_u32(data, at + member_at("hknpBodyCinfo", "motionId"), body.motion);
    link(local, at + member_at("hknpBodyCinfo", "name"), text(body.name));
    for (std::size_t lane = 0; lane < 3; ++lane) patch_float(data, at + member_at("hknpBodyCinfo", "position") + lane * 4, body.position[lane]);
    for (std::size_t lane = 0; lane < 4; ++lane) patch_float(data, at + member_at("hknpBodyCinfo", "orientation") + lane * 4, body.rotation[lane]);
  }
  if (!spec.bodies.empty()) array(ragdoll + member_at("hknpPhysicsSystemData", "bodyCinfos"), bodies_at, spec.bodies.size());

  const std::size_t motion_size = find_layout("hknpMotionCinfo")->size;
  const std::size_t motions_at = data.size();
  data.resize(motions_at + spec.motions.size() * motion_size, std::byte{0});
  for (std::size_t i = 0; i < spec.motions.size(); ++i) {
    for (std::size_t lane = 0; lane < 3; ++lane) {
      patch_float(data, motions_at + i * motion_size + member_at("hknpMotionCinfo", "centerOfMassWorld") + lane * 4,
                  spec.motions[i][lane]);
    }
    if (i < spec.inverse_masses.size()) {
      patch_float(data, motions_at + i * motion_size + member_at("hknpMotionCinfo", "inverseMass"), spec.inverse_masses[i]);
    }
    if (i < spec.inverse_inertias.size()) {
      for (std::size_t lane = 0; lane < 3; ++lane) {
        patch_float(data, motions_at + i * motion_size + member_at("hknpMotionCinfo", "inverseInertiaLocal") + lane * 4,
                    spec.inverse_inertias[i][lane]);
      }
    }
  }
  if (!spec.motions.empty()) array(ragdoll + member_at("hknpPhysicsSystemData", "motionCinfos"), motions_at, spec.motions.size());

  const std::size_t constraint_size = find_layout("hknpConstraintCinfo")->size;
  const std::size_t constraints_at = data.size();
  data.resize(constraints_at + spec.constraints.size() * constraint_size, std::byte{0});
  for (std::size_t i = 0; i < spec.constraints.size(); ++i) {
    const std::size_t at = constraints_at + i * constraint_size;
    if (constraint_at[i] != 0) point(at + member_at("hknpConstraintCinfo", "constraintData"), constraint_at[i]);
    patch_u32(data, at + member_at("hknpConstraintCinfo", "bodyA"), spec.constraints[i].body_a);
    patch_u32(data, at + member_at("hknpConstraintCinfo", "bodyB"), spec.constraints[i].body_b);
  }
  if (!spec.constraints.empty()) {
    array(ragdoll + member_at("hknpPhysicsSystemData", "constraintCinfos"), constraints_at, spec.constraints.size());
  }

  const std::size_t map_at = data.size();
  for (const std::int32_t body : spec.bone_map) put_u32(data, static_cast<std::uint32_t>(body));
  if (!spec.bone_map.empty()) array(ragdoll + member_at("hknpRagdollData", "boneToBodyMap"), map_at, spec.bone_map.size());

  if (spec.with_skeleton) {
    point(ragdoll + member_at("hknpRagdollData", "skeleton"), spec.ragdoll_points_at_mapper ? mapper : skeleton_b);
  }

  return build_packfile({
      TestSection{"__classnames__", names, {}, {}, {}},
      TestSection{"__types__", {}, {}, {}, {}},
      TestSection{"__data__", std::move(data), std::move(local), std::move(global), std::move(virtuals)},
  });
}

}  // namespace fo4::test::hkx
