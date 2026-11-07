#pragma once

#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::swf {

/* Byte offset span of the MATRIX inside a PlaceObject, PlaceObject2 or PlaceObject3 body. */
struct SwfMatrixSpan {
  std::size_t begin{0};
  std::size_t end{0};
};

/* Requires the placement to carry a MATRIX; false otherwise. */
bool placement_matrix_span(std::uint16_t tag_code, std::span<const std::byte> body, SwfMatrixSpan& out);

/* Requires every field to fit 31 bits; empty when a value does not. */
std::vector<std::byte> encode_matrix(const SwfTransform& transform);

/* Requires the body to have a MATRIX span; every other byte must stay untouched. */
bool replace_placement_transform(std::uint16_t tag_code, std::span<const std::byte> body,
                                 const SwfTransform& transform, std::vector<std::byte>& out);

/* Requires a placement or removal body; every other byte must stay untouched. */
bool replace_placement_depth(std::uint16_t tag_code, std::span<const std::byte> body,
                             std::uint16_t depth, std::vector<std::byte>& out);

/* Invariant: instance_id is the display list's id for the root frame the target was built from. */
struct SwfInstanceEditTarget {
  std::uint32_t instance_id{0};
  std::string path;
  bool inside_sprite{false};
  std::uint16_t sprite_id{0};
  std::size_t sprite_tag_index{0};
  bool has_authored_matrix{false};
  /* Invariant: a root target holds a top-level tag index, a sprite child an index inside the sprite payload. */
  std::size_t transform_tag_index{0};
  /* Invariant: the tag that created the object, which is a top-level index for a root target. */
  std::size_t create_tag_index{0};
};

std::vector<SwfInstanceEditTarget> build_instance_edit_targets(const SwfFile& file,
                                                               std::uint32_t root_frame);

struct SwfInstanceTransformEdit {
  std::uint32_t instance_id{0};
  SwfTransform transform{};
};

struct SwfTransformEditResult {
  std::vector<std::size_t> replaced_tag_indices;
  std::string message;
};

/* Invariant: a refusal must leave every tag body untouched and set message. */
bool apply_instance_transforms(SwfFile& file, std::uint32_t root_frame,
                               const std::vector<SwfInstanceTransformEdit>& edits,
                               SwfTransformEditResult& out);

struct SwfDepthReassignResult {
  std::uint32_t new_instance_id{0};
  std::uint16_t source_depth{0};
  std::uint16_t destination_depth{0};
  /* Invariant: indices name the pre-insertion tag positions, so they stay stable for a later diff. */
  std::vector<std::size_t> replaced_tag_indices;
  std::vector<std::size_t> inserted_tag_indices;
  std::string message;
};

/* Invariant: a refusal must leave the tag count and every tag body untouched and set message. */
bool reassign_instance_depth(SwfFile& file, std::uint32_t root_frame, std::uint32_t instance_id,
                             std::uint16_t destination_depth, SwfDepthReassignResult& out);

/* Invariant: one authored root frame without sprite expansion, so fields the display list drops stay visible. */
struct SwfAuthoredFrameObject {
  std::uint16_t depth{0};
  std::uint16_t character_id{0};
  std::string name;
  std::string class_name;
  std::optional<std::uint16_t> ratio;
  std::optional<std::uint16_t> clip_depth;
  /* Invariant: the depth of the innermost live mask range that covers this object. */
  std::optional<std::uint16_t> masked_by_depth;
  std::uint16_t filter_count{0};
  std::optional<std::uint8_t> blend_mode;
  /* Invariant: absent until a placement on this frame's path authors a MATRIX. */
  std::optional<SwfMatrix> matrix;
  std::size_t create_tag_index{0};
  std::size_t transform_tag_index{0};

  bool operator==(const SwfAuthoredFrameObject&) const = default;
};

struct SwfAuthoredFrame {
  std::vector<SwfAuthoredFrameObject> objects;
};

/* Invariant: the identity transform when no MATRIX was authored. */
SwfTransform authored_transform(const std::optional<SwfMatrix>& matrix) noexcept;

/* Requires the frame to be completed by a ShowFrame. */
bool build_authored_frame(const std::vector<SwfTag>& tags, std::uint32_t frame,
                          SwfAuthoredFrame& out, std::string& message);

struct SwfKeyframeInsertResult {
  std::uint32_t root_frame{0};
  std::uint16_t depth{0};
  /* Invariant: the inserted tag's index after the insertion, which is the pre-insertion ShowFrame index. */
  std::size_t inserted_tag_index{0};
  std::string message;
};

/* Invariant: a refusal must leave the tag count and every tag body untouched and set message. */
bool insert_instance_transform_keyframe(SwfFile& file, std::uint32_t root_frame,
                                        std::uint32_t instance_id, SwfKeyframeInsertResult& out);

/* Requires the requested transform to fit the MATRIX fields; the frame and later frames carry it. */
bool insert_instance_transform_keyframe(SwfFile& file, std::uint32_t root_frame,
                                        std::uint32_t instance_id, const SwfTransform& transform,
                                        SwfKeyframeInsertResult& out);

/* Flat tag address: top names the top-level tag, and child names the sprite child when inside is set. */
struct SwfFlatTag {
  std::size_t top{0};
  std::size_t child{0};
  bool inside{false};
};

/* Requires flat to be the pre-order index radicle_swf_tag_at reports; nesting below a sprite child must be refused. */
bool resolve_flat_tag(const SwfFile& file, std::size_t flat, SwfFlatTag& out);

/* Requires flat_tag to name a flat placement index. */
bool set_placement_depth_flat(SwfFile& file, std::size_t flat_tag, std::uint16_t depth,
                              std::string& message);

/* Requires flat_tag to name a flat placement index and the transform to fit 31 bits. */
bool set_placement_transform_flat(SwfFile& file, std::size_t flat_tag, const SwfTransform& transform,
                                  std::string& message);

/* Requires flat_tag to name a flat PlaceObject2 index and frame to name a reached frame. */
bool insert_placement_keyframe_flat(SwfFile& file, std::size_t flat_tag, std::uint32_t frame,
                                    std::string& message);

}  // namespace fo4::swf
