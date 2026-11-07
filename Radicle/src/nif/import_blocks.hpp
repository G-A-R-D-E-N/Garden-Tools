#pragma once

#include "fo4/nif/nif.hpp"
#include "import_mesh.hpp"
#include "write_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fo4::nif::import_detail {

using write_detail::Bytes;

constexpr std::uint32_t k_nif_version = 0x14020007U;
constexpr std::uint32_t k_user_version = 12;
constexpr std::uint32_t k_bs_version = 130;
constexpr std::uint32_t k_shader_flags1 = 0x80400201U;
constexpr std::uint32_t k_skinned_shader_flag = 0x2U;
constexpr std::uint32_t k_box_bsx_flags = 130;
constexpr std::size_t k_max_shape_vertices = 65535;

Bytes ni_node(const Transform& xf, std::int32_t name, const std::vector<std::int32_t>& extras,
              const std::vector<std::int32_t>& children, std::int32_t collision = -1);
Bytes shape_block(const Mesh& mesh, std::int32_t name, std::int32_t skin_ref, std::int32_t shader_ref);
Bytes shader_block(std::int32_t name, std::int32_t texture_set, std::uint32_t flags1);
Bytes texture_set_block(std::string_view diffuse, std::string_view normal);
Bytes bsx_block(std::int32_t name, std::uint32_t flags);
Bytes skin_instance_block(std::int32_t skeleton_root, std::int32_t data, const std::vector<std::int32_t>& bones);
// Requires binds.size() to be the skin's bone count and each mesh influence index to be below it.
Bytes bone_data_block(const Mesh& mesh, const std::vector<Transform>& binds);
Bytes collision_object_block(std::int32_t physics_block);
Bytes physics_system_block(const Bytes& packfile);
Bytes box_packfile(const std::array<Vec3, 2>& bounds);
std::array<Vec3, 2> bounds_of(const std::vector<Vec3>& positions);
// Requires a block that read_nif accepts as a single box body: returns false when the collision does not read back.
bool reads_one_box_body(const NifFile& nif);

}
