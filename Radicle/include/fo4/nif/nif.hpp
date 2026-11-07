#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

struct Transform {
  float t[3]{};
  float r[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float s{1.0f};
};

struct BlockInfo {
  std::string type;
  std::size_t offset;
  std::size_t size;
};

struct Node {
  std::uint32_t block{0};
  std::string type;
  std::string name;
  std::int32_t parent{-1};
  std::vector<std::uint32_t> children;
  Transform local;
  std::optional<std::int32_t> value;
};

class NifFile {
public:
  std::uint32_t version() const noexcept { return version_; }
  std::uint32_t user_version() const noexcept { return user_version_; }
  std::uint32_t bs_version() const noexcept { return bs_version_; }
  const std::vector<std::byte>& bytes() const noexcept { return bytes_; }
  const std::vector<std::string>& strings() const noexcept { return strings_; }
  const std::vector<BlockInfo>& blocks() const noexcept { return blocks_; }
  const std::vector<std::uint32_t>& roots() const noexcept { return roots_; }
  const std::vector<std::uint32_t>& children(std::uint32_t block) const { return children_.at(block); }
  std::int32_t parent(std::uint32_t block) const { return parents_.at(block); }
  const std::vector<Node>& nodes() const noexcept { return nodes_; }
  std::int32_t node_for_block(std::uint32_t block) const noexcept {
    return block < node_of_block_.size() ? node_of_block_[block] : -1;
  }

private:
  friend NifFile read_nif(std::span<const std::byte> bytes);

  std::vector<std::byte> bytes_;
  std::uint32_t version_{0};
  std::uint32_t user_version_{0};
  std::uint32_t bs_version_{0};
  std::vector<std::string> strings_;
  std::vector<BlockInfo> blocks_;
  std::vector<std::uint32_t> roots_;
  std::vector<std::vector<std::uint32_t>> children_;
  std::vector<std::int32_t> parents_;
  std::vector<Node> nodes_;
  std::vector<std::int32_t> node_of_block_;
};

NifFile read_nif(std::span<const std::byte> bytes);

struct Bone {
  std::string name;
  float center[3]{};
  float radius{0.0f};
  float rotation[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float translation[3]{};
  float scale{1.0f};
};

struct Shader {
  std::uint32_t block{0};
  std::uint32_t type{0};
  std::string name;
  std::int32_t texture_set_ref{-1};
  std::vector<std::string> textures;
};

struct Shape {
  std::uint32_t block{0};
  std::string name;
  std::vector<float> verts;
  std::vector<float> normals;
  std::vector<float> uvs;
  std::vector<float> uvs2;
  std::vector<float> tangents;
  std::vector<float> bitangents;
  std::vector<float> colors;
  std::vector<std::uint16_t> tris;
  std::vector<float> weights;
  std::vector<std::uint8_t> bone_indices;
  std::vector<Bone> bones;
  std::int32_t parent_node{-1};
  std::int32_t skin_ref{-1};
  std::int32_t shader_ref{-1};
  std::int32_t alpha_ref{-1};
  std::string material;
  std::vector<std::string> textures;
  std::string warning;
};

std::vector<Shape> read_shapes(const NifFile& nif);
// Invariant: the four tri-shape types share the BSTriShape layout read_shape decodes.
bool is_shape_block_type(std::string_view type);
// Requires is_shape_block_type(nif.blocks().at(block).type); throws parse_error for a malformed block.
Shape read_shape(const NifFile& nif, std::uint32_t block);
Transform shape_to_global(const NifFile& nif, std::uint32_t block);
Transform block_transform(const NifFile& nif, std::uint32_t block);
std::int32_t billboard_mode(const NifFile& nif, std::uint32_t block);

std::int32_t shape_parent_node(const NifFile& nif, std::uint32_t block);

Shader read_shader(const NifFile& nif, std::uint32_t block);

std::vector<Bone> read_bones(const NifFile& nif, std::uint32_t instance_block);

// Invariant: BSXFlags is a NiIntegerExtraData, so a block is a u32 name index then the u32 flag word.
constexpr std::uint32_t k_bsx_editor_marker_bit = 1U << 5U;
// Requires a BSXFlags block; the word is the one the engine tests for is_editor_marker.
std::uint32_t read_bsx_flags(const NifFile& nif, std::uint32_t block);
bool is_editor_marker(std::uint32_t bsx_flags) noexcept;
struct ScannedConnectPoint {
  std::string parent;
  std::string name;
  float rotation_wxyz[4]{1.0f, 0.0f, 0.0f, 0.0f};
  float translation[3]{};
  float scale{1.0f};
};
struct ScannedConnectChildren {
  bool skinned{false};
  std::vector<std::string> names;
};
struct SubSegment {
  std::uint32_t start_index{0};
  std::uint32_t primitives{0};
  std::uint32_t parent_array_index{0};
  std::uint32_t unused{0};
};
struct Segment {
  std::uint32_t start_index{0};
  std::uint32_t primitives{0};
  std::uint32_t parent_array_index{0};
  std::vector<SubSegment> sub_segments;
};
struct SegmentUserData {
  std::uint32_t user_index{0};
  std::uint32_t bone_id{0};
  std::vector<float> cut_offsets;
};
struct SegmentTable {
  std::uint32_t primitives{0};
  std::uint32_t total_segments{0};
  std::vector<Segment> segments;
  std::vector<std::uint32_t> segment_starts;
  std::vector<SegmentUserData> user_data;
  std::string ssf_file;
};
std::vector<ScannedConnectPoint> read_connect_parents(const NifFile& nif);
std::vector<ScannedConnectChildren> read_connect_children(const NifFile& nif);
SegmentTable read_segments(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif
