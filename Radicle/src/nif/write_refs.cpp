#include "write_refs.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/constants.hpp"
#include "fo4/error.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif::write_detail {
namespace {

constexpr std::string_view k_collision_object = "bhkNPCollisionObject";
constexpr std::string_view k_physics_system = "bhkPhysicsSystem";
constexpr std::string_view k_ragdoll_system = "bhkRagdollSystem";
constexpr std::string_view k_dynamic_tri_shape = "BSDynamicTriShape";
constexpr std::size_t k_object_size = 14;
constexpr std::size_t k_object_target_at = 0;
constexpr std::size_t k_object_data_at = 6;
constexpr std::size_t k_av_collision_base = 68;

bool is_system(std::string_view type) { return type == k_physics_system || type == k_ragdoll_system; }

bool is_ref_free(std::string_view type) {
  return type == "BSShaderTextureSet" || type == "BSXFlags" || type == "NiStringExtraData" || type == "BSBound" ||
         type == "BSBehaviorGraphExtraData" || type == "BSClothExtraData" || type == "NiTransformData" ||
         type == "NiTextKeyExtraData" || type == "BSPackedCombinedSharedGeomDataExtra" ||
         type == "BSFurnitureMarkerNode" || type == "BSMultiBoundOBB" || type == "BSMultiBoundAABB" || is_system(type);
}

bool is_shape(std::string_view type) { return is_shape_block_type(type); }

std::optional<std::size_t> trailing_bytes(std::string_view type) {
  if (type == "NiNode" || type == "BSFadeNode" || type == "BSLeafAnimNode" || type == "BSTriShape" ||
      type == "BSSubIndexTriShape" || type == k_dynamic_tri_shape) {
    return std::size_t{0};
  }
  if (type == "BSMeshLODTriShape") return std::size_t{12};
  if (type == "BSMultiBoundNode") return std::size_t{8};
  if (type == "BSOrderedNode") return std::size_t{17};
  if (type == "BSValueNode") return std::size_t{5};
  if (type == "NiBillboardNode") return std::size_t{2};
  if (type == "NiSwitchNode") return std::size_t{6};
  return std::nullopt;
}

bool has_child_list(std::string_view type) {
  return (!is_shape(type) && trailing_bytes(type).has_value()) || has_particle_child_list(type);
}

void require_size(const Bytes& bytes, std::size_t size) {
  if (bytes.size() < size) throw parse_error(0, "block is shorter than its reference layout");
}

void read_time_controller(binary::Cursor& cursor, std::vector<std::size_t>& refs) {
  refs.push_back(cursor.position());
  cursor.read_i32();
  cursor.read_bytes(18);
  refs.push_back(cursor.position());
  cursor.read_i32();
}

void read_single_interp(binary::Cursor& cursor, std::vector<std::size_t>& refs) {
  read_time_controller(cursor, refs);
  refs.push_back(cursor.position());
  cursor.read_i32();
}

std::optional<std::size_t> single_interp_extra(std::string_view type) {
  if (type == "NiTransformController" || type == "NiVisController" || type == "NiLightDimmerController" ||
      type == "NiLightRadiusController" || type == "BSFrustumFOVController" ||
      type == "BSNiAlphaPropertyTestRefController") {
    return std::size_t{0};
  }
  if (type == "BSEffectShaderPropertyFloatController" || type == "BSEffectShaderPropertyColorController" ||
      type == "BSLightingShaderPropertyFloatController" || type == "BSLightingShaderPropertyColorController") {
    return std::size_t{4};
  }
  if (type == "NiPSysModifierActiveCtlr") return std::size_t{4};
  if (type == "NiLightColorController") return std::size_t{2};
  return std::nullopt;
}

std::optional<std::size_t> timed_controller_extra(std::string_view type) {
  if (type == "BSLagBoneController") return std::size_t{12};
  if (type == "NiPSysUpdateCtlr") return std::size_t{0};
  return std::nullopt;
}

std::optional<std::size_t> leading_value_size(std::string_view type) {
  if (type == "NiFloatInterpolator") return std::size_t{4};
  if (type == "NiBoolInterpolator" || type == "NiBoolTimelineInterpolator") return std::size_t{1};
  if (type == "NiPoint3Interpolator") return std::size_t{12};
  return std::nullopt;
}

std::optional<std::size_t> blend_value_size(std::string_view type) {
  if (type == "NiBlendFloatInterpolator") return std::size_t{4};
  if (type == "NiBlendBoolInterpolator") return std::size_t{1};
  if (type == "NiBlendPoint3Interpolator") return std::size_t{12};
  return std::nullopt;
}

std::optional<std::size_t> key_value_size(std::string_view type) {
  if (type == "NiFloatData") return std::size_t{4};
  if (type == "NiBoolData") return std::size_t{1};
  if (type == "NiPosData") return std::size_t{12};
  return std::nullopt;
}

void read_blend_base(binary::Cursor& cursor, std::vector<std::size_t>& refs) {
  const auto flags = cursor.read_u8();
  const auto items = std::size_t{cursor.read_u8()};
  cursor.read_bytes(4);
  if ((flags & 1U) != 0) return;
  cursor.read_bytes(20);
  if (items > cursor.remaining() / 17) throw parse_error(0, "blend item list exceeds its block");
  for (std::size_t i = 0; i < items; ++i) {
    refs.push_back(cursor.position());
    cursor.read_i32();
    cursor.read_bytes(13);
  }
}

void read_key_group(binary::Cursor& cursor, std::size_t value_size) {
  const auto keys = cursor.read_u32();
  if (keys == 0) return;
  const auto type = cursor.read_u32();
  if (type < 1 || type > 5) throw parse_error(0, "key group interpolation type is not known");
  const std::size_t per_key = type == 2 ? 4 + 3 * value_size : type == 3 ? 16 + value_size : 4 + value_size;
  if (keys > cursor.remaining() / per_key) throw parse_error(0, "key group exceeds its block");
  cursor.read_bytes(keys * per_key);
}

void skip_counted(binary::Cursor& cursor, std::size_t element) {
  const auto count = cursor.read_u32();
  if (count > cursor.remaining() / element) throw parse_error(0, "extra data list exceeds its block");
  cursor.read_bytes(static_cast<std::size_t>(count) * element);
}

void read_extra_data(binary::Cursor& cursor, std::string_view type) {
  if (type == "BSSkin::BoneData") return skip_counted(cursor, 68);
  cursor.read_i32();
  if (type == "NiExtraData") return;
  if (type == "NiBinaryExtraData") return skip_counted(cursor, 1);
  if (type == "NiStringsExtraData") {
    const auto count = cursor.read_u32();
    for (std::uint32_t i = 0; i < count; ++i) cursor.read_bytes(cursor.read_u32());
    return;
  }
  if (type == "BSEyeCenterExtraData") return skip_counted(cursor, 4);
  if (type == "BSPositionData") return skip_counted(cursor, 2);
  skip_counted(cursor, 8);
}

void require_layout_end(const binary::Cursor& cursor, std::string_view type) {
  if (!cursor.at_end()) {
    throw parse_error(0, "block type " + std::string(type) + " does not match its reference layout");
  }
}

std::size_t read_sub_index_segments(const Bytes& bytes, std::size_t at, std::uint32_t data_size) {
  if (data_size == 0) return at;
  const auto segments = std::size_t{uint_at(bytes, at + 4)};
  const auto total = std::size_t{uint_at(bytes, at + 8)};
  std::size_t pos = at + 12;
  for (std::size_t i = 0; i < segments; ++i) {
    pos += 16 + 16 * std::size_t{uint_at(bytes, pos + 12)};
    require_size(bytes, pos);
  }
  if (segments < total) {
    const auto shared = std::size_t{uint_at(bytes, pos)};
    const auto shared_total = std::size_t{uint_at(bytes, pos + 4)};
    pos += 8 + 4 * shared;
    require_size(bytes, pos);
    for (std::size_t i = 0; i < shared_total; ++i) {
      pos += 12 + 4 * std::size_t{uint_at(bytes, pos + 8)};
      require_size(bytes, pos);
    }
    require_size(bytes, pos + 2);
    pos += 2 + std::size_t{read_int<std::uint16_t>(bytes, pos)};
  }
  return pos;
}

}  // namespace

void require_fo4_version(const Model& model, std::string_view type) {
  if (model.version != k_nif_version || model.user_version != k_nif_user_version || model.bs_version != k_nif_bs_version) {
    throw parse_error(0, "unsupported " + std::string(type) + " reference layout version");
  }
}

std::uint32_t uint_at(const Bytes& bytes, std::size_t at) {
  require_size(bytes, at + 4);
  return read_int<std::uint32_t>(bytes, at);
}

std::int32_t int_at(const Bytes& bytes, std::size_t at) { return static_cast<std::int32_t>(uint_at(bytes, at)); }

std::vector<std::size_t> ref_offsets(const Model& model, std::uint32_t block, bool allow_unlisted_node) {
  const auto type = block_type(model, block);
  const auto& bytes = model.blocks[block];
  std::vector<std::size_t> refs;
  if (is_ref_free(type)) return refs;
  if (auto particle = particle_ref_offsets(model, type, bytes)) return *particle;
  if (type == "BSConnectPoint::Parents" || type == "BSConnectPoint::Children") {
    require_fo4_version(model, type);
    const bool parents = type == "BSConnectPoint::Parents";
    binary::Cursor cursor(bytes);
    cursor.read_i32();
    if (!parents) cursor.read_u8();
    const auto count = cursor.read_u32();
    if (count > cursor.remaining() / (parents ? 40U : 4U)) {
      throw parse_error(0, "connect-point count exceeds its block");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
      if (parents) cursor.read_bytes(cursor.read_u32());
      cursor.read_bytes(cursor.read_u32());
      if (parents) cursor.read_bytes(32);
    }
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "BSDismemberSkinInstance") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    refs.push_back(cursor.position());
    cursor.read_i32();
    refs.push_back(cursor.position());
    cursor.read_i32();
    refs.push_back(cursor.position());
    cursor.read_i32();
    // Invariant: bone refs then partitions follow Skeleton Root, matching read_dismember_skin.
    const auto bones = cursor.read_u32();
    if (bones > cursor.remaining() / 4) throw parse_error(0, "dismember bone list exceeds its block");
    for (std::uint32_t i = 0; i < bones; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    const auto partitions = cursor.read_u32();
    if (partitions > cursor.remaining() / 4) throw parse_error(0, "dismember partition list exceeds its block");
    cursor.read_bytes(static_cast<std::size_t>(partitions) * 4);
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "BSSkin::Instance") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    refs.push_back(cursor.position());
    cursor.read_i32();
    refs.push_back(cursor.position());
    cursor.read_i32();
    const auto bones = cursor.read_u32();
    if (bones > cursor.remaining() / 4) throw parse_error(0, "skin-instance bone list exceeds its block");
    for (std::uint32_t i = 0; i < bones; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    const auto scales = cursor.read_u32();
    if (scales > cursor.remaining() / 12) throw parse_error(0, "skin-instance scale list exceeds its block");
    cursor.read_bytes(static_cast<std::size_t>(scales) * 12);
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == k_collision_object) {
    require_size(bytes, k_object_size);
    return {k_object_target_at, k_object_data_at};
  }
  if (type == "BSEffectShaderProperty") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    cursor.read_i32();
    const auto count = cursor.read_u32();
    if (count > cursor.remaining() / 4) throw parse_error(0, "effect-shader extra-data list exceeds its block");
    for (std::uint32_t i = 0; i < count; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    refs.push_back(cursor.position());
    cursor.read_i32();
    cursor.read_bytes(24);
    const auto texture = [&cursor] {
      const auto length = cursor.read_u32();
      if (length > k_nif_max_string_length) throw parse_error(0, "effect-shader texture path exceeds 1 MiB");
      cursor.read_bytes(length);
    };
    texture();
    cursor.read_bytes(44);
    for (int i = 0; i < 4; ++i) texture();
    cursor.read_bytes(4);
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "BSLightingShaderProperty") {
    const auto n = std::size_t{uint_at(bytes, 8)};
    require_size(bytes, 44 + 4 * n);
    for (std::size_t i = 0; i < n; ++i) refs.push_back(12 + 4 * i);
    refs.push_back(12 + 4 * n);
    refs.push_back(40 + 4 * n);
    return refs;
  }
  if (type == "NiAlphaProperty") {
    const auto n = std::size_t{uint_at(bytes, 4)};
    if (bytes.size() != 15 + 4 * n) throw parse_error(0, "NiAlphaProperty block does not match its reference layout");
    for (std::size_t i = 0; i < n; ++i) refs.push_back(8 + 4 * i);
    refs.push_back(8 + 4 * n);
    return refs;
  }
  if (type == "NiControllerManager") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_time_controller(cursor, refs);
    cursor.read_u8();
    const auto sequences = cursor.read_u32();
    if (sequences > cursor.remaining() / 4) throw parse_error(0, "controller sequence list exceeds its block");
    for (std::uint32_t i = 0; i < sequences; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    refs.push_back(cursor.position());
    cursor.read_i32();
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "NiMultiTargetTransformController") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_time_controller(cursor, refs);
    const auto targets = cursor.read_u16();
    if (targets > cursor.remaining() / 4) throw parse_error(0, "extra target list exceeds its block");
    for (std::uint16_t i = 0; i < targets; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "NiTransformInterpolator") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    cursor.read_bytes(32);
    refs.push_back(cursor.position());
    cursor.read_i32();
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "NiControllerSequence") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    cursor.read_i32();
    const auto blocks = cursor.read_u32();
    cursor.read_u32();
    if (blocks > cursor.remaining() / 29) throw parse_error(0, "controlled block list exceeds its block");
    for (std::uint32_t i = 0; i < blocks; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
      refs.push_back(cursor.position());
      cursor.read_i32();
      cursor.read_bytes(21);
    }
    cursor.read_bytes(4);
    refs.push_back(cursor.position());
    cursor.read_i32();
    cursor.read_bytes(16);
    refs.push_back(cursor.position());
    cursor.read_i32();
    cursor.read_bytes(4);
    const auto notes = cursor.read_u16();
    if (notes > cursor.remaining() / 4) throw parse_error(0, "anim note array list exceeds its block");
    for (std::uint16_t i = 0; i < notes; ++i) {
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "NiDefaultAVObjectPalette") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    refs.push_back(cursor.position());
    cursor.read_i32();
    const auto objects = cursor.read_u32();
    for (std::uint32_t i = 0; i < objects; ++i) {
      const auto length = cursor.read_u32();
      if (length > cursor.remaining()) throw parse_error(0, "palette object name exceeds its block");
      cursor.read_bytes(length);
      refs.push_back(cursor.position());
      cursor.read_i32();
    }
    require_layout_end(cursor, type);
    return refs;
  }
  if (const auto extra = single_interp_extra(type)) {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_single_interp(cursor, refs);
    cursor.read_bytes(*extra);
    require_layout_end(cursor, type);
    return refs;
  }
  if (const auto extra = timed_controller_extra(type)) {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_time_controller(cursor, refs);
    cursor.read_bytes(*extra);
    require_layout_end(cursor, type);
    return refs;
  }
  if (const auto value = leading_value_size(type)) {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    cursor.read_bytes(*value);
    refs.push_back(cursor.position());
    cursor.read_i32();
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "NiPathInterpolator") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    cursor.read_bytes(16);
    refs.push_back(cursor.position());
    cursor.read_i32();
    refs.push_back(cursor.position());
    cursor.read_i32();
    require_layout_end(cursor, type);
    return refs;
  }
  if (const auto value = blend_value_size(type)) {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_blend_base(cursor, refs);
    cursor.read_bytes(*value);
    require_layout_end(cursor, type);
    return refs;
  }
  if (const auto value = key_value_size(type)) {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_key_group(cursor, *value);
    require_layout_end(cursor, type);
    return refs;
  }
  if (type == "NiExtraData" || type == "NiBinaryExtraData" || type == "NiStringsExtraData" ||
      type == "BSEyeCenterExtraData" || type == "BSPositionData" || type == "BSBoneLODExtraData" ||
      type == "BSSkin::BoneData") {
    require_fo4_version(model, type);
    binary::Cursor cursor(bytes);
    read_extra_data(cursor, type);
    require_layout_end(cursor, type);
    return refs;
  }
  const auto tail = trailing_bytes(type);
  if (!tail && !allow_unlisted_node) {
    throw parse_error(0, "block type " + std::string(type) +
                             " has no reference layout, so its references cannot be followed");
  }
  const auto n = std::size_t{uint_at(bytes, 4)};
  require_size(bytes, 12 + 4 * n);
  for (std::size_t i = 0; i < n; ++i) refs.push_back(8 + 4 * i);
  refs.push_back(8 + 4 * n);
  refs.push_back(k_av_collision_base + 4 * n);
  std::size_t end = 0;
  if (is_shape(type)) {
    const auto data_size = uint_at(bytes, 114 + 4 * n);
    end = 118 + 4 * n + std::size_t{data_size};
    refs.push_back(88 + 4 * n);
    refs.push_back(92 + 4 * n);
    refs.push_back(96 + 4 * n);
    if (type == "BSSubIndexTriShape") end = read_sub_index_segments(bytes, end, data_size);
    if (type == k_dynamic_tri_shape) end += 4 + std::size_t{uint_at(bytes, end)};
  } else {
    const auto children = std::size_t{uint_at(bytes, 72 + 4 * n)};
    require_size(bytes, 76 + 4 * n + 4 * children);
    for (std::size_t i = 0; i < children; ++i) refs.push_back(76 + 4 * n + 4 * i);
    end = 76 + 4 * n + 4 * children;
    if (type == "BSMultiBoundNode") refs.push_back(end);
  }
  if (tail && bytes.size() != end + *tail) {
    throw parse_error(0, "block type " + std::string(type) + " does not match its reference layout");
  }
  return refs;
}

std::optional<ChildList> child_list(const Model& model, std::uint32_t block) {
  if (!has_child_list(block_type(model, block))) return std::nullopt;
  const auto& bytes = model.blocks[block];
  const auto n = std::size_t{uint_at(bytes, 4)};
  ChildList list;
  list.count_at = 72 + 4 * n;
  list.count = uint_at(bytes, list.count_at);
  list.first = 76 + 4 * n;
  require_size(bytes, list.first + 4 * list.count);
  return list;
}

}  // namespace fo4::nif::write_detail
