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

}  // namespace fo4::swf
