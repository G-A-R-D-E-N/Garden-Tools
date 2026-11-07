#include "write_refs.hpp"

#include "fo4/binary/cursor.hpp"
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
constexpr std::size_t k_object_size = 14;
constexpr std::size_t k_object_target_at = 0;
constexpr std::size_t k_object_data_at = 6;
constexpr std::size_t k_av_collision_base = 68;

bool is_system(std::string_view type) { return type == k_physics_system || type == k_ragdoll_system; }

bool is_ref_free(std::string_view type) {
  return type == "BSShaderTextureSet" || type == "BSXFlags" || type == "NiStringExtraData" || type == "BSBound" ||
         type == "BSBehaviorGraphExtraData" || type == "BSClothExtraData" || is_system(type);
}

std::optional<std::size_t> trailing_bytes(std::string_view type) {
  if (type == "NiNode" || type == "BSLeafAnimNode" || type == "BSTriShape") return std::size_t{0};
  if (type == "BSMeshLODTriShape") return std::size_t{12};
  if (type == "BSMultiBoundNode") return std::size_t{8};
  if (type == "BSOrderedNode") return std::size_t{17};
  if (type == "BSValueNode") return std::size_t{5};
  if (type == "NiBillboardNode") return std::size_t{2};
  if (type == "NiSwitchNode") return std::size_t{6};
  return std::nullopt;
}

bool has_child_list(std::string_view type) {
  return type != "BSTriShape" && type != "BSMeshLODTriShape" && trailing_bytes(type).has_value();
}

void require_size(const Bytes& bytes, std::size_t size) {
  if (bytes.size() < size) throw parse_error(0, "block is shorter than its reference layout");
}

}  // namespace

std::uint32_t uint_at(const Bytes& bytes, std::size_t at) {
  require_size(bytes, at + 4);
  return read_int<std::uint32_t>(bytes, at);
}

std::int32_t int_at(const Bytes& bytes, std::size_t at) { return static_cast<std::int32_t>(uint_at(bytes, at)); }

std::vector<std::size_t> ref_offsets(const Model& model, std::uint32_t block) {
  const auto type = block_type(model, block);
  const auto& bytes = model.blocks[block];
  std::vector<std::size_t> refs;
  if (is_ref_free(type)) return refs;
  if (type == "BSConnectPoint::Parents" || type == "BSConnectPoint::Children") {
    if (model.version != 0x14020007U || model.user_version != 12 || model.bs_version != 130) {
      throw parse_error(0, "unsupported connect-point reference layout version");
    }
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
    if (!cursor.at_end()) throw parse_error(0, "connect-point block does not match its reference layout");
    return refs;
  }
  if (type == "BSSkin::Instance") {
    if (model.version != 0x14020007U || model.user_version != 12 || model.bs_version != 130) {
      throw parse_error(0, "unsupported skin-instance reference layout version");
    }
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
    if (!cursor.at_end()) throw parse_error(0, "skin-instance block does not match its reference layout");
    return refs;
  }
  if (type == k_collision_object) {
    require_size(bytes, k_object_size);
    return {k_object_target_at, k_object_data_at};
  }
  if (type == "BSEffectShaderProperty") {
    if (model.version != 0x14020007U || model.user_version != 12 || model.bs_version != 130) {
      throw parse_error(0, "unsupported effect-shader reference layout version");
    }
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
      if (length > 1024U * 1024U) throw parse_error(0, "effect-shader texture path exceeds 1 MiB");
      cursor.read_bytes(length);
    };
    texture();
    cursor.read_bytes(44);
    for (int i = 0; i < 4; ++i) texture();
    cursor.read_bytes(4);
    if (!cursor.at_end()) throw parse_error(0, "effect-shader block does not match its reference layout");
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
  const auto tail = trailing_bytes(type);
  if (!tail) {
    throw parse_error(0, "block type " + std::string(type) +
                             " has no reference layout, so its references cannot be followed");
  }
  const auto n = std::size_t{uint_at(bytes, 4)};
  require_size(bytes, 12 + 4 * n);
  for (std::size_t i = 0; i < n; ++i) refs.push_back(8 + 4 * i);
  refs.push_back(8 + 4 * n);
  refs.push_back(k_av_collision_base + 4 * n);
  std::size_t end = 0;
  if (type == "BSTriShape" || type == "BSMeshLODTriShape") {
    end = 118 + 4 * n + std::size_t{uint_at(bytes, 114 + 4 * n)};
    refs.push_back(88 + 4 * n);
    refs.push_back(92 + 4 * n);
    refs.push_back(96 + 4 * n);
  } else {
    const auto children = std::size_t{uint_at(bytes, 72 + 4 * n)};
    require_size(bytes, 76 + 4 * n + 4 * children);
    for (std::size_t i = 0; i < children; ++i) refs.push_back(76 + 4 * n + 4 * i);
    end = 76 + 4 * n + 4 * children;
    if (type == "BSMultiBoundNode") refs.push_back(end);
  }
  if (bytes.size() != end + *tail) {
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
