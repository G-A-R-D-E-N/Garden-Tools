#include "fo4/hkx/physics.hpp"

#include "fo4/hkx/layout.hpp"
#include "fo4/hkx/skeleton_mapper.hpp"
#include "hkx_read_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace fo4::hkx {
namespace {

using detail::Elements;
using detail::elements_at;
using detail::holds_layout;
using detail::in_section;
using detail::local_target;
using detail::member_field;
using detail::pointer_target;
using detail::read_string;
using detail::value_at;

constexpr std::string_view kCapsuleClass = "hknpCapsuleShape";

const char* status_text(HkxStatus status) {
  switch (status) {
    case HkxStatus::Ok: return "ok";
    case HkxStatus::Truncated: return "the file is truncated";
    case HkxStatus::BadMagic: return "the packfile magic is wrong";
    case HkxStatus::UnsupportedLayout: return "the packfile layout is not supported";
    case HkxStatus::MissingSection: return "a required section is missing";
    case HkxStatus::BadSection: return "a section is malformed";
    case HkxStatus::FixupOutOfRange: return "a pointer or array lies outside its section";
    case HkxStatus::DuplicateFixup: return "a pointer is fixed up twice";
    case HkxStatus::BadClassName: return "a class name entry is malformed";
    case HkxStatus::MissingObject: return "an object is missing";
  }
  return "unknown error";
}

std::optional<std::size_t> object_at(const HkxPackfile& pack, std::size_t offset) {
  const auto found = std::lower_bound(pack.objects.begin(), pack.objects.end(), offset,
                                      [](const HkxObjectView& object, std::size_t value) { return object.offset < value; });
  if (found == pack.objects.end() || found->offset != offset) return std::nullopt;
  return static_cast<std::size_t>(found - pack.objects.begin());
}

std::optional<std::size_t> pointer_object(const HkxPackfile& pack, const HkxSection& section, std::size_t field) {
  if (!in_section(section, field, 8)) return std::nullopt;
  const auto target = pointer_target(pack, section, field);
  if (!target) return std::nullopt;
  return object_at(pack, *target);
}

std::optional<float> float_at(const HkxSection& section, std::size_t at) {
  if (!in_section(section, at, 4)) return std::nullopt;
  return value_at<float>(section.data, at);
}

std::optional<std::int32_t> byte_at(const HkxSection& section, std::size_t at) {
  if (!in_section(section, at, 1)) return std::nullopt;
  return static_cast<std::int32_t>(value_at<std::uint8_t>(section.data, at));
}

std::optional<std::vector<float>> floats_at(const HkxSection& section, std::size_t at, std::size_t count) {
  if (!in_section(section, at, count * 4)) return std::nullopt;
  std::vector<float> out(count);
  for (std::size_t i = 0; i < count; ++i) out[i] = value_at<float>(section.data, at + i * 4);
  return out;
}

std::optional<std::size_t> offset_of(std::string_view class_name, std::string_view member) {
  const HkxClassLayout* layout = find_layout(class_name);
  if (!layout) return std::nullopt;
  const auto offset = find_member_offset(*layout, member);
  if (!offset) return std::nullopt;
  return static_cast<std::size_t>(*offset);
}

std::string trimmed(const std::string& text) {
  const auto blank = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v'; };
  std::size_t from = 0;
  std::size_t to = text.size();
  while (from < to && blank(text[from])) ++from;
  while (to > from && blank(text[to - 1])) --to;
  return text.substr(from, to - from);
}

struct RelArray {
  std::size_t at{0};
  std::size_t count{0};
};

// Requires the payload to lie inside the object's own bytes, so a shape never reads a neighbouring object.
std::optional<RelArray> rel_array(const HkxObjectView& object, std::size_t field, std::size_t element_size) {
  if (field > object.data.size() || object.data.size() - field < 4) return std::nullopt;
  const std::size_t count = value_at<std::uint16_t>(object.data, field);
  const std::size_t at = field + value_at<std::uint16_t>(object.data, field + 2);
  if (at > object.data.size() || count * element_size > object.data.size() - at) return std::nullopt;
  return RelArray{at, count};
}

HkxPhysicsShape read_capsule(const HkxObjectView& object, std::size_t index) {
  HkxPhysicsShape shape;
  shape.id = static_cast<std::int32_t>(index);
  shape.kind = HkxPhysicsShapeKind::Capsule;

  const HkxClassLayout* capsule = find_layout(kCapsuleClass);
  if (!capsule || !holds_layout(object, *capsule)) return shape;

  if (const auto vertices_at = offset_of("hknpConvexShape", "vertices")) {
    if (const auto rows = rel_array(object, *vertices_at, 16); rows && rows->count > 0) {
      for (std::size_t i = 0; i < rows->count; ++i) {
        const std::size_t row = rows->at + i * 16;
        shape.vertices.push_back({value_at<float>(object.data, row), value_at<float>(object.data, row + 4),
                                  value_at<float>(object.data, row + 8)});
      }
    }
  }

  const auto indices_at = offset_of("hknpConvexPolytopeShape", "indices");
  std::optional<std::vector<std::int32_t>> indices;
  if (indices_at) {
    if (const auto rows = rel_array(object, *indices_at, 1)) {
      indices.emplace();
      for (std::size_t i = 0; i < rows->count; ++i) {
        indices->push_back(static_cast<std::int32_t>(value_at<std::uint8_t>(object.data, rows->at + i)));
      }
    }
  }

  const auto faces_at = offset_of("hknpConvexPolytopeShape", "faces");
  const HkxClassLayout* face_layout = find_layout("hknpConvexPolytopeShapeFace");
  if (faces_at && face_layout && face_layout->size > 0 && indices) {
    const auto first_offset = find_member_offset(*face_layout, "firstIndex");
    const auto count_offset = find_member_offset(*face_layout, "numIndices");
    const auto rows = rel_array(object, *faces_at, face_layout->size);
    if (first_offset && count_offset && rows && rows->count > 0) {
      for (std::size_t i = 0; i < rows->count; ++i) {
        const std::size_t row = rows->at + i * face_layout->size;
        const std::size_t first = value_at<std::uint16_t>(object.data, row + *first_offset);
        const std::size_t count = value_at<std::uint8_t>(object.data, row + *count_offset);
        if (first + count > indices->size()) continue;
        shape.face_rings.emplace_back(indices->begin() + static_cast<std::ptrdiff_t>(first),
                                      indices->begin() + static_cast<std::ptrdiff_t>(first + count));
      }
    }
  }
  return shape;
}

HkxConstraintKind constraint_kind_of(std::string_view class_name) {
  if (class_name == "hkpRagdollConstraintData") return HkxConstraintKind::Ragdoll;
  if (class_name == "hkpLimitedHingeConstraintData") return HkxConstraintKind::LimitedHinge;
  if (class_name == "hkpHingeConstraintData") return HkxConstraintKind::Hinge;
  if (class_name == "hkpBallAndSocketConstraintData") return HkxConstraintKind::BallAndSocket;
  if (class_name == "hkpFixedConstraintData") return HkxConstraintKind::Fixed;
  return HkxConstraintKind::Unknown;
}

// Requires the object view to hold the whole constraint-data class, so no atom is read from a neighbouring object.
void measure_constraint(const HkxSection& section, const HkxObjectView& object, HkxPhysicsConstraint& out) {
  const std::string class_name(object.class_name);
  out.kind = constraint_kind_of(class_name);
  if (out.kind == HkxConstraintKind::Unknown) return;

  const HkxClassLayout* layout = find_layout(class_name);
  if (!layout || !holds_layout(object, *layout)) return;
  const auto atoms_offset = find_member_offset(*layout, "atoms");
  if (!atoms_offset) return;
  const std::size_t atoms = object.offset + *atoms_offset;
  const std::string atoms_type = class_name + "Atoms";

  const auto member_in = [](std::size_t base, std::string_view type, std::string_view member) -> std::optional<std::size_t> {
    const auto offset = offset_of(type, member);
    if (!offset) return std::nullopt;
    return base + *offset;
  };

  if (const auto transforms = member_in(atoms, atoms_type, "transforms")) {
    if (const auto at = member_in(*transforms, "hkpSetLocalTransformsConstraintAtom", "transformA")) {
      out.frame_a = floats_at(section, *at, 16);
    }
    if (const auto at = member_in(*transforms, "hkpSetLocalTransformsConstraintAtom", "transformB")) {
      out.frame_b = floats_at(section, *at, 16);
    }
  }

  const auto narrow = [&](const std::optional<std::size_t>& base, std::string_view type, std::string_view member) {
    return base ? byte_at(section, member_in(*base, type, member).value_or(section.data.size())) : std::nullopt;
  };
  const auto real = [&](const std::optional<std::size_t>& base, std::string_view type, std::string_view member) {
    return base ? float_at(section, member_in(*base, type, member).value_or(section.data.size())) : std::nullopt;
  };

  if (out.kind == HkxConstraintKind::Ragdoll) {
    const auto twist = member_in(atoms, atoms_type, "twistLimit");
    if (twist) {
      constexpr std::string_view type = "hkpTwistLimitConstraintAtom";
      out.twist_axis = narrow(twist, type, "twistAxis").value_or(0);
      out.twist_ref_axis = narrow(twist, type, "refAxis").value_or(1);
      out.twist_min_angle = real(twist, type, "minAngle");
      out.twist_max_angle = real(twist, type, "maxAngle");
    }
    const auto cone = member_in(atoms, atoms_type, "coneLimit");
    if (cone) {
      constexpr std::string_view type = "hkpConeLimitConstraintAtom";
      out.cone_twist_axis = narrow(cone, type, "twistAxisInA").value_or(0);
      out.cone_ref_axis = narrow(cone, type, "refAxisInB").value_or(0);
      out.cone_max_angle = real(cone, type, "maxAngle");
    }
  } else if (out.kind == HkxConstraintKind::LimitedHinge) {
    const auto limit = member_in(atoms, atoms_type, "angLimit");
    if (limit) {
      constexpr std::string_view type = "hkpAngLimitConstraintAtom";
      out.limit_axis = narrow(limit, type, "limitAxis").value_or(0);
      out.min_angle = real(limit, type, "minAngle").value_or(0.0f);
      out.max_angle = real(limit, type, "maxAngle").value_or(0.0f);
    }
  }
}

struct Layouts {
  const HkxClassLayout* ragdoll{nullptr};
  const HkxClassLayout* system{nullptr};
  const HkxClassLayout* body{nullptr};
  const HkxClassLayout* motion{nullptr};
  const HkxClassLayout* constraint{nullptr};
};

std::optional<Layouts> load_layouts(const HkxObjectView& ragdoll) {
  Layouts layouts;
  layouts.ragdoll = find_layout(kPhysicsRagdollClass, ragdoll.signature);
  layouts.system = find_layout("hknpPhysicsSystemData");
  layouts.body = find_layout("hknpBodyCinfo");
  layouts.motion = find_layout("hknpMotionCinfo");
  layouts.constraint = find_layout("hknpConstraintCinfo");
  if (!layouts.ragdoll || !layouts.system || !layouts.body || !layouts.motion || !layouts.constraint) return std::nullopt;
  if (!holds_layout(ragdoll, *layouts.ragdoll)) return std::nullopt;
  return layouts;
}

std::optional<std::size_t> member_in_struct(const HkxClassLayout& layout, std::size_t base, std::string_view member) {
  const auto offset = find_member_offset(layout, member);
  if (!offset) return std::nullopt;
  return base + *offset;
}

HkxStatus read_bodies(const HkxPackfile& pack, const HkxSection& section, const HkxObjectView& ragdoll,
                      const Layouts& layouts, HkxRagdollModel& model, HkxPhysicsLoadFacts& facts) {
  const std::size_t pointer_size = pack.pointer_size;

  Elements motions;
  if (const auto field = member_field(section, *layouts.system, ragdoll, "motionCinfos")) {
    if (const HkxStatus status = elements_at(section, *field, pointer_size, layouts.motion->size, motions);
        status != HkxStatus::Ok) {
      return status;
    }
  }

  Elements cinfos;
  if (const auto field = member_field(section, *layouts.system, ragdoll, "bodyCinfos")) {
    if (const HkxStatus status = elements_at(section, *field, pointer_size, layouts.body->size, cinfos);
        status != HkxStatus::Ok) {
      return status;
    }
  }
  if (cinfos.count == 0) return HkxStatus::Ok;

  facts.body_count = static_cast<std::int32_t>(cinfos.count);
  for (std::size_t i = 0; i < cinfos.count; ++i) {
    const std::size_t base = cinfos.first + i * layouts.body->size;
    const auto at = [&](std::string_view member) { return member_in_struct(*layouts.body, base, member); };
    const auto position_at = at("position");
    const auto orientation_at = at("orientation");
    if (!position_at || !orientation_at || !in_section(section, *position_at, 16) ||
        !in_section(section, *orientation_at, 16)) {
      ++facts.skipped_bodies;
      continue;
    }

    HkxRigidBody body;
    body.id = static_cast<std::int32_t>(i);
    body.position = {value_at<float>(section.data, *position_at), value_at<float>(section.data, *position_at + 4),
                     value_at<float>(section.data, *position_at + 8)};
    body.rotation = {value_at<float>(section.data, *orientation_at), value_at<float>(section.data, *orientation_at + 4),
                     value_at<float>(section.data, *orientation_at + 8),
                     value_at<float>(section.data, *orientation_at + 12)};

    std::string shape_class;
    if (const auto shape_at = at("shape")) {
      if (const auto shape = pointer_object(pack, section, *shape_at)) {
        body.shape_id = static_cast<std::int32_t>(*shape);
        shape_class = std::string(pack.objects[*shape].class_name);
      }
    }

    if (const auto motion_at = at("motionId"); motion_at && in_section(section, *motion_at, 4)) {
      const auto motion_id = value_at<std::uint32_t>(section.data, *motion_at);
      if (motion_id < motions.count) {
        const std::size_t motion = motions.first + std::size_t{motion_id} * layouts.motion->size;
        if (const auto com = member_in_struct(*layouts.motion, motion, "centerOfMassWorld");
            com && in_section(section, *com, 16)) {
          body.center_of_mass = {value_at<float>(section.data, *com), value_at<float>(section.data, *com + 4),
                                 value_at<float>(section.data, *com + 8)};
        }
      }
    }

    if (const auto name_at = at("name")) {
      if (const auto target = in_section(section, *name_at, 8) ? local_target(section, *name_at) : std::nullopt) {
        std::string name;
        if (read_string(section, *target, name) == HkxStatus::Ok) body.name = trimmed(name);
      }
    }

    model.bodies.push_back(std::move(body));
    if (shape_class == kCapsuleClass) continue;
    const std::string label = shape_class.empty() ? "(no shape reference)" : shape_class;
    ++facts.unsupported_shape_bodies;
    ++facts.unsupported_shape_classes[label];
  }
  return HkxStatus::Ok;
}

HkxStatus read_constraints(const HkxPackfile& pack, const HkxSection& section, const HkxObjectView& ragdoll,
                           const Layouts& layouts, HkxRagdollModel& model, HkxPhysicsLoadFacts& facts) {
  Elements cinfos;
  if (const auto field = member_field(section, *layouts.system, ragdoll, "constraintCinfos")) {
    if (const HkxStatus status = elements_at(section, *field, pack.pointer_size, layouts.constraint->size, cinfos);
        status != HkxStatus::Ok) {
      return status;
    }
  }
  for (std::size_t i = 0; i < cinfos.count; ++i) {
    const std::size_t base = cinfos.first + i * layouts.constraint->size;
    const auto body_a_at = member_in_struct(*layouts.constraint, base, "bodyA");
    const auto body_b_at = member_in_struct(*layouts.constraint, base, "bodyB");
    constexpr auto kMaxId = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    if (!body_a_at || !body_b_at || !in_section(section, *body_a_at, 4) || !in_section(section, *body_b_at, 4)) {
      ++facts.skipped_constraints;
      continue;
    }
    const auto body_a = value_at<std::uint32_t>(section.data, *body_a_at);
    const auto body_b = value_at<std::uint32_t>(section.data, *body_b_at);
    if (body_a > kMaxId || body_b > kMaxId) {
      ++facts.skipped_constraints;
      continue;
    }

    HkxPhysicsConstraint constraint;
    constraint.id = static_cast<std::int32_t>(i);
    constraint.body_a = static_cast<std::int32_t>(body_a);
    constraint.body_b = static_cast<std::int32_t>(body_b);
    if (const auto data_at = member_in_struct(*layouts.constraint, base, "constraintData")) {
      if (const auto data = pointer_object(pack, section, *data_at)) {
        measure_constraint(section, pack.objects[*data], constraint);
      }
    }
    model.constraints.push_back(std::move(constraint));
  }
  return HkxStatus::Ok;
}

HkxStatus read_skeleton_and_mapper(const HkxPackfile& pack, const HkxSection& section, const HkxObjectView& ragdoll,
                                   const Layouts& layouts, HkxRagdollModel& model) {
  if (const auto field = member_field(section, *layouts.ragdoll, ragdoll, "boneToBodyMap")) {
    Elements map;
    if (const HkxStatus status = elements_at(section, *field, pack.pointer_size, 4, map); status != HkxStatus::Ok) {
      return status;
    }
    for (std::size_t bone = 0; bone < map.count; ++bone) {
      const auto body = value_at<std::int32_t>(section.data, map.first + bone * 4);
      model.bone_bindings.push_back({static_cast<std::int32_t>(bone), body});
    }
  }

  const auto skeleton_field = member_field(section, *layouts.ragdoll, ragdoll, "skeleton");
  if (!skeleton_field) return HkxStatus::Ok;
  const auto skeleton_index = pointer_object(pack, section, *skeleton_field);
  if (!skeleton_index || pack.objects[*skeleton_index].class_name != kPhysicsSkeletonClass) return HkxStatus::Ok;

  HkxSkeleton skeleton;
  if (read_skeleton_object(pack, pack.objects[*skeleton_index], skeleton) != HkxStatus::Ok) return HkxStatus::Ok;
  model.skeleton = std::move(skeleton);

  std::vector<HkxSkeletonMapper> mappers;
  if (read_skeleton_mappers(pack, mappers) != HkxStatus::Ok) return HkxStatus::Ok;
  for (const HkxSkeletonMapper& mapper : mappers) {
    if (mapper.skeleton_b_object != *skeleton_index || mapper.mapping_type != 0 || mapper.rows.empty()) continue;
    for (const HkxMapperSimpleRow& row : mapper.rows) {
      HkxRagdollMapping mapping;
      mapping.bone_a = row.bone_a;
      mapping.bone_b = row.bone_b;
      mapping.a_from_b_translation = row.a_from_b.translation();
      mapping.a_from_b_rotation = row.a_from_b.rotation();
      model.mappings.push_back(mapping);
    }
    model.animation_skeleton = mapper.skeleton_a;
    break;
  }
  return HkxStatus::Ok;
}

HkxStatus extract_model(const HkxPackfile& pack, std::size_t ragdoll_index, HkxRagdollModel& model,
                        HkxPhysicsLoadFacts& facts) {
  if (pack.data_section >= pack.sections.size()) return HkxStatus::MissingSection;
  const HkxSection& section = pack.sections[pack.data_section];
  const HkxObjectView& ragdoll = pack.objects[ragdoll_index];
  const auto layouts = load_layouts(ragdoll);
  if (!layouts) return HkxStatus::BadSection;

  for (std::size_t i = 0; i < pack.objects.size(); ++i) {
    if (pack.objects[i].class_name == kCapsuleClass) model.shapes.push_back(read_capsule(pack.objects[i], i));
  }
  facts.shape_count = static_cast<std::int32_t>(model.shapes.size());

  if (const HkxStatus status = read_bodies(pack, section, ragdoll, *layouts, model, facts); status != HkxStatus::Ok) {
    return status;
  }
  facts.usable_bodies = static_cast<std::int32_t>(model.bodies.size());

  if (const HkxStatus status = read_constraints(pack, section, ragdoll, *layouts, model, facts);
      status != HkxStatus::Ok) {
    return status;
  }
  facts.constraint_count = static_cast<std::int32_t>(model.constraints.size());

  if (const HkxStatus status = read_skeleton_and_mapper(pack, section, ragdoll, *layouts, model);
      status != HkxStatus::Ok) {
    return status;
  }
  facts.mappings = static_cast<std::int32_t>(model.mappings.size());
  return HkxStatus::Ok;
}

HkxPhysicsLoadReport finish(HkxPhysicsLoadFacts facts, const std::string& problem, bool usable) {
  if (!problem.empty() && !usable) {
    facts.container_problem = problem;
  } else {
    facts.container_notice = problem;
  }
  return classify_physics_load(facts);
}

HkxPhysicsLoadReport failure(HkxPhysicsLoadFacts facts, const std::string& problem, const char* stage,
                             const char* message) {
  if (!problem.empty()) {
    facts.container_problem = problem;
  } else {
    facts.parse_stage = stage;
    facts.parse_message = message;
  }
  return classify_physics_load(facts);
}

HkxStatus read_with_problem(const HkxPackfile& pack, const std::string& problem, HkxPhysicsLoadFacts facts,
                            HkxRagdollModel& out, HkxPhysicsLoadReport& report) {
  out = HkxRagdollModel{};
  facts.file_version = pack.file_version;
  facts.pointer_size = static_cast<std::int32_t>(pack.pointer_size);
  for (const HkxObjectView& object : pack.objects) ++facts.class_counts[std::string(object.class_name)];

  const auto ragdoll = std::find_if(pack.objects.begin(), pack.objects.end(),
                                    [](const HkxObjectView& object) { return object.class_name == kPhysicsRagdollClass; });
  if (ragdoll == pack.objects.end()) {
    report = finish(std::move(facts), problem, false);
    return HkxStatus::MissingObject;
  }

  HkxRagdollModel model;
  const HkxStatus status = extract_model(pack, static_cast<std::size_t>(ragdoll - pack.objects.begin()), model, facts);
  if (status != HkxStatus::Ok) {
    report = failure(std::move(facts), problem, "ragdoll", status_text(status));
    return status;
  }

  const bool usable = !model.bodies.empty();
  report = finish(std::move(facts), problem, usable);
  if (!problem.empty() && !usable) return HkxStatus::MissingObject;
  out = std::move(model);
  return HkxStatus::Ok;
}

}  // namespace

HkxStatus read_ragdoll(const HkxPackfile& packfile, HkxRagdollModel& out, HkxPhysicsLoadReport& report) {
  return read_with_problem(packfile, {}, {}, out, report);
}

HkxStatus read_ragdoll(std::span<const std::byte> bytes, HkxRagdollModel& out, HkxPhysicsLoadReport& report) {
  HkxPhysicsLoadFacts facts;
  const std::string problem = physics_container_problem(bytes, facts.file_version, facts.pointer_size);

  HkxPackfile pack;
  const HkxStatus status = read_packfile(bytes, pack);
  if (status != HkxStatus::Ok) {
    out = HkxRagdollModel{};
    report = failure(std::move(facts), problem, "container", status_text(status));
    return status;
  }
  return read_with_problem(pack, problem, std::move(facts), out, report);
}

HkxPhysicsLoadReport inspect_physics_file(std::span<const std::byte> bytes) {
  HkxRagdollModel model;
  HkxPhysicsLoadReport report;
  read_ragdoll(bytes, model, report);
  return report;
}

}  // namespace fo4::hkx
