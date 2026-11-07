#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_bone_data_header = 4;

std::string node_name(const NifFile& nif, std::int32_t block) {
  if (block < 0 || static_cast<std::size_t>(block) >= nif.blocks().size()) return {};
  const auto& info = nif.blocks().at(static_cast<std::uint32_t>(block));
  if (info.size < 4) return {};
  auto cursor = detail::block_cursor(nif, static_cast<std::uint32_t>(block));
  const auto index = cursor.read_i32();
  if (index < 0 || static_cast<std::size_t>(index) >= nif.strings().size()) return {};
  return nif.strings()[static_cast<std::size_t>(index)];
}

void read_bone_transform(binary::Cursor& cursor, Bone& bone) {
  for (float& value : bone.center) value = cursor.read_f32();
  bone.radius = cursor.read_f32();
  for (float& value : bone.rotation) value = cursor.read_f32();
  for (float& value : bone.translation) value = cursor.read_f32();
  bone.scale = cursor.read_f32();
}

}

std::vector<Bone> read_bones(const NifFile& nif, std::uint32_t instance_block) {
  const auto& info = nif.blocks().at(instance_block);
  if (info.type != "BSSkin::Instance") return {};

  auto cursor = detail::block_cursor(nif, instance_block);
  cursor.read_i32();
  const auto data = cursor.read_i32();
  const auto bone_count = cursor.read_u32();
  if (static_cast<std::size_t>(bone_count) > cursor.remaining() / 4) {
    throw parse_error(cursor.absolute_offset(), "bone reference list runs past the end of its block");
  }

  std::vector<Bone> bones(bone_count);
  for (auto& bone : bones) bone.name = node_name(nif, cursor.read_i32());

  if (data < 0 || static_cast<std::size_t>(data) >= nif.blocks().size()) return bones;
  const auto& data_info = nif.blocks().at(static_cast<std::uint32_t>(data));
  if (data_info.type != "BSSkin::BoneData") return bones;
  if (data_info.size < k_bone_data_header) {
    throw parse_error(data_info.offset, "bone data is shorter than its bone count");
  }

  auto transforms = detail::block_cursor(nif, static_cast<std::uint32_t>(data));
  const auto count = transforms.read_u32();
  if (count != bone_count) {
    throw parse_error(transforms.absolute_offset(),
                      "bone data holds " + std::to_string(count) + " transforms for a skin with " +
                          std::to_string(bone_count) + " bones");
  }
  if (static_cast<std::size_t>(count) > transforms.remaining() / detail::k_bone_transform_bytes) {
    throw parse_error(transforms.absolute_offset(), "bone transform list runs past the end of its block");
  }
  for (auto& bone : bones) read_bone_transform(transforms, bone);
  return bones;
}

}
